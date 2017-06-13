/*
 The MIT License (MIT)

 Copyright (c) [2016] [BTC.COM]

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
 */
#ifndef STRATUM_SESSION_H_
#define STRATUM_SESSION_H_

#include "Common.h"

#include <netinet/in.h>
#include <deque>

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>

#include <glog/logging.h>

#include "zcash/uint256.h"
#include "utilities_js.hpp"
#include "Stratum.h"
#include "Statistics.h"


#define CMD_MAGIC_NUMBER      0x7Fu
// types
#define CMD_REGISTER_WORKER   0x01u             // Agent -> Pool
#define CMD_SUBMIT_SHARE      0x02u             // Agent -> Pool, without block time
#define CMD_SUBMIT_SHARE_WITH_TIME  0x03u       // Agent -> Pool
#define CMD_UNREGISTER_WORKER 0x04u             // Agent -> Pool
#define CMD_MINING_SET_DIFF   0x05u             // Pool  -> Agent

// agent
#define AGENT_MAX_SESSION_ID   0xFFFEu  // 0xFFFEu = 65534

#define BTCCOM_MINER_AGENT_PREFIX "btccom-agent/"

// invalid share sliding window size
#define INVALID_SHARE_SLIDING_WINDOWS_SIZE       60  // unit: seconds
#define INVALID_SHARE_SLIDING_WINDOWS_MAX_LIMIT  20  // max number

class Server;
class StratumJobEx;
class DiffController;
class StratumSession;
class AgentSessions;

//////////////////////////////// DiffController ////////////////////////////////
class DiffController {
public:
  static const arith_uint256 KMinTarget_ =
  arith_uint256("007fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");

  static const int32_t kDiffWindow_    = 900;   // time window, seconds, 60*N
  static const int32_t kRecordSeconds_ = 10;    // every N seconds as a record
#ifdef NDEBUG
  // if not debugging
  static const arith_uint256 kDefaultTarget_  =
  arith_uint256("0007ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
#else
  // debugging enabled
  static const arith_uint256 kDefaultTarget_  =
  arith_uint256("007fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
#endif	/* NDEBUG */

private:
  time_t  startTime_;         // first job send time
  arith_uint256 minTarget_;   // miner's min target, use KMinTarget_ as default value
  arith_uint256 curTarget_;
  int32_t shareAvgSeconds_;

  StatsWindow<double> sharesNum_;  // share count

  arith_uint256 _calcCurTarget();

  inline bool isFullWindow(const time_t now) {
    return now >= startTime_ + kDiffWindow_;
  }

public:
  DiffController(const int32_t shareAvgSeconds) :
  startTime_(0),
  minTarget_(KMinTarget_), curTarget_(kDefaultTarget_),
  sharesNum_(kDiffWindow_/kRecordSeconds_) /* every N seconds as a record */
  {
    if (shareAvgSeconds >= 1 && shareAvgSeconds <= 60) {
      shareAvgSeconds_ = shareAvgSeconds;
    } else {
      shareAvgSeconds_ = 5;  // out of range, use default
      LOG(WARNING) << "share avg seconds out of range, use default value: 5";
    }
  }

  ~DiffController() {}

  // recalc miner's target/bits before send an new stratum job
  uint32_t calcCurBits();

  // add one share
  void addAcceptedShare();

  // maybe worker has it's own min diff
  void setMinTarget(arith_uint256 target);

  // use when handle cmd: mining.suggest_target
  void resetCurTarget(arith_uint256 target);
};



//////////////////////////////// StratumSession ////////////////////////////////
class StratumSession {
public:
  // mining state
  enum State {
    CONNECTED     = 0,
    SUBSCRIBED    = 1,
    AUTHENTICATED = 2
  };

  // shares submitted by this session, for duplicate share check
  struct LocalShare {
    // NONCE 1 is fixed 16 bytes
    // NONCE 2 is fixed 16 bytes, use two uint64_t instead
    uint64_t nonce2_1_;
    uint64_t nonce2_2_;
    uint32_t nTime_;

    LocalShare(uint64_t nonce2_1, uint64_t nonce2_2, uint32_t time):
    nonce2_1_(nonce2_1), nonce2_2_(nonce2_2), nTime_(time) {}

    LocalShare & operator=(const LocalShare &other) {
      nonce2_1_ = other.nonce2_1_;
      nonce2_2_ = other.nonce2_2_;
      nTime_    = other.nTime_;
      return *this;
    }

    bool operator<(const LocalShare &r) const {
      if (nonce2_1_ < r.nonce2_1_ ||
          (nonce2_1_ == r.nonce2_1_ && nonce2_2_ < r.nonce2_2_) ||
          (nonce2_1_ == r.nonce2_1_ && nonce2_2_ == r.nonce2_2_ && nTime_ < r.nTime_)) {
        return true;
      }
      return false;
    }
  };

  // latest stratum jobs of this session
  struct LocalJob {
    uint64_t jobId_;
    uint32_t jobBits_;  // job difficulty
    uint32_t blkBits_;
    uint16_t shortJobId_;
    std::set<LocalShare> submitShares_;

    LocalJob(): jobId_(0u), jobBits_(0u), blkBits_(0u), shortJobId_(0u) {}

    bool addLocalShare(const LocalShare &localShare) {
      auto itr = submitShares_.find(localShare);
      if (itr != submitShares_.end()) {
        return false;  // already exist
      }
      submitShares_.insert(localShare);
      return true;
    }
  };

  //----------------------
private:
  int32_t shareAvgSeconds_;
  DiffController diffController_;
  State state_;
  StratumWorker worker_;
  string   clientAgent_;  // eg. bfgminer/4.4.0-32-gac4e9b3
  string   clientIp_;
  uint32_t clientIpInt_;

  uint32_t extraNonce1_;   // MUST be unique across all servers

  arith_uint256 currTarget_;
  std::deque<LocalJob> localJobs_;
  size_t kMaxNumLocalJobs_;

  struct evbuffer *inBuf_;
  bool   isLongTimeout_;
  uint16_t shortJobIdIdx_;

  atomic<bool> isDead_;

  // invalid share counter
  StatsWindow<int64_t> invalidSharesCounter_;

  uint16_t allocShortJobId();

  void setup();
  void setReadTimeout(const int32_t timeout);

  bool handleMessage();  // handle all messages: ex-message and stratum message

  void responseError(const string &idStr, int code);
  void responseTrue(const string &idStr);

  bool tryReadLine(string &line);
  void handleLine(const string &line);
  void handleRequest(const string &idStr, const string &method, const JsonNode &jparams);

  void handleRequest_Subscribe        (const string &idStr, const JsonNode &jparams);
  void handleRequest_Authorize        (const string &idStr, const JsonNode &jparams);
  void handleRequest_Submit           (const string &idStr, const JsonNode &jparams);
  void handleRequest_SuggestTarget    (const string &idStr, const JsonNode &jparams);
  void handleRequest_SuggestDifficulty(const string &idStr, const JsonNode &jparams);
  void handleRequest_MultiVersion     (const string &idStr, const JsonNode &jparams);
  void _handleRequest_SetDifficulty(uint64_t suggestDiff);
  void _handleRequest_AuthorizePassword(const string &password);

  LocalJob *findLocalJob(uint16_t shortJobId);

public:
  struct bufferevent* bev_;
  evutil_socket_t fd_;
  Server *server_;

public:
  StratumSession(evutil_socket_t fd, struct bufferevent *bev,
                 Server *server, struct sockaddr *saddr,
                 const int32_t shareAvgSeconds, const uint32_t extraNonce1);
  ~StratumSession();

  void markAsDead();
  bool isDead();

  void sendSetDifficulty(const uint64_t difficulty);
  void sendMiningNotify(shared_ptr<StratumJobEx> exJobPtr, bool isFirstJob=false);
  void sendData(const char *data, size_t len);
  inline void sendData(const string &str) {
    sendData(str.data(), str.size());
  }
  void readBuf(struct evbuffer *buf);

  void handleExMessage_AuthorizeAgentWorker(const int64_t workerId,
                                            const string &clientAgent,
                                            const string &workerName);
  void handleRequest_Submit(const string &idStr,
                            const uint8_t shortJobId, const uint64_t extraNonce2,
                            const uint32_t nonce, uint32_t nTime);
  uint32_t getSessionId() const;
};

#endif
