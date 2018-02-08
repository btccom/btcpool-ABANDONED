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

#include "bitcoin/uint256.h"
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
  //
  // max diff: 2^62
  //
  // Cannot large than 2^62.
  // If kMaxDiff_ be 2^63, `kMaxDiff_*2` will be zero and
  // DiffController::_calcCurDiff() will infinite loop.
  static const uint64 kMaxDiff_ = 4611686018427387904ull;
  // min diff
  static const uint64 kMinDiff_ = 64;

  static const time_t kDiffWindow_    = 900;   // time window, seconds, 60*N
  static const time_t kRecordSeconds_ = 10;    // every N seconds as a record
#ifdef NDEBUG
  // If not debugging, set default to 16384
  static const uint64 kDefaultDiff_   = 16384;  // default diff, 2^N
#else
  // debugging enabled
  static const uint64 kDefaultDiff_   = 128;  // default diff, 2^N
#endif	/* NDEBUG */

private:
  time_t startTime_;  // first job send time
  uint64  minDiff_;
  uint64  curDiff_;
  int32_t shareAvgSeconds_;
  int32_t curHashRateLevel_;

  StatsWindow<double> sharesNum_;  // share count
  StatsWindow<uint64> shares_;     // share

  void setCurDiff(uint64 curDiff); // set current diff with bounds checking
  uint64 _calcCurDiff();
  int adjustHashRateLevel(const double hashRateT);
  double minerCoefficient(const time_t now, const int64_t idx);

  inline bool isFullWindow(const time_t now) {
    return now >= startTime_ + kDiffWindow_;
  }

public:
  DiffController(const int32_t shareAvgSeconds) :
  startTime_(0),
  minDiff_(kMinDiff_), curDiff_(kDefaultDiff_), curHashRateLevel_(0),
  sharesNum_(kDiffWindow_/kRecordSeconds_), /* every N seconds as a record */
  shares_   (kDiffWindow_/kRecordSeconds_)
  {
    if (shareAvgSeconds >= 1 && shareAvgSeconds <= 60) {
      shareAvgSeconds_ = shareAvgSeconds;
    } else {
      shareAvgSeconds_ = 8;
    }
  }

  ~DiffController() {}

  // recalc miner's diff before send an new stratum job
  uint64 calcCurDiff();

  // we need to add every share, so we can calc worker's hashrate
  void addAcceptedShare(const uint64 share);

  // maybe worker has it's own min diff
  void setMinDiff(uint64 minDiff);

  // use when handle cmd: mining.suggest_difficulty & mining.suggest_target
  void resetCurDiff(uint64 curDiff);
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
    uint64_t exNonce2_;  // extra nonce2 fixed 8 bytes
    uint32_t nonce_;     // nonce in block header
    uint32_t time_;      // nTime in block header

    LocalShare(uint64_t exNonce2, uint32_t nonce, uint32_t time):
    exNonce2_(exNonce2), nonce_(nonce), time_(time) {}

    LocalShare & operator=(const LocalShare &other) {
      exNonce2_ = other.exNonce2_;
      nonce_    = other.nonce_;
      time_     = other.time_;
      return *this;
    }

    bool operator<(const LocalShare &r) const {
      if (exNonce2_ < r.exNonce2_ ||
          (exNonce2_ == r.exNonce2_ && nonce_ < r.nonce_) ||
          (exNonce2_ == r.exNonce2_ && nonce_ == r.nonce_ && time_ < r.time_)) {
        return true;
      }
      return false;
    }
  };

  // latest stratum jobs of this session
  struct LocalJob {
    uint64_t jobId_;
    uint64_t jobDifficulty_;     // difficulty of this job
    uint32_t blkBits_;
    uint8_t  shortJobId_;
    std::set<LocalShare> submitShares_;
    std::vector<uint8_t> agentSessionsDiff2Exp_;

    LocalJob(): jobId_(0), jobDifficulty_(0), blkBits_(0), shortJobId_(0) {}

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
  static const int kExtraNonce2Size_ = 8;  // extraNonce2 size is always 8 bytes

  uint64_t currDiff_;
  std::deque<LocalJob> localJobs_;
  size_t kMaxNumLocalJobs_;

  struct evbuffer *inBuf_;
  bool   isLongTimeout_;
  uint8_t shortJobIdIdx_;

  // nicehash has can't use short JobID
  bool isNiceHashClient_;

  AgentSessions *agentSessions_;

  atomic<bool> isDead_;

  // invalid share counter
  StatsWindow<int64_t> invalidSharesCounter_;

  uint8_t allocShortJobId();

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

  LocalJob *findLocalJob(uint8_t shortJobId);

  void handleExMessage_RegisterWorker     (const string *exMessage);
  void handleExMessage_UnRegisterWorker   (const string *exMessage);
  void handleExMessage_SubmitShare        (const string *exMessage);
  void handleExMessage_SubmitShareWithTime(const string *exMessage);

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
                            const uint32_t nonce, uint32_t nTime,
                            bool isAgentSession,
                            DiffController *sessionDiffController);
  uint32_t getSessionId() const;
};


///////////////////////////////// AgentSessions ////////////////////////////////
class AgentSessions {
  //
  // sessionId is vector's index
  //
  // session ID range: [0, 65535], so vector max size is 65536
  vector<int64_t> workerIds_;
  vector<DiffController *> diffControllers_;
  vector<uint8_t> curDiff2ExpVec_;
  int32_t shareAvgSeconds_;
  uint8_t kDefaultDiff2Exp_;

  StratumSession *stratumSession_;

public:
  AgentSessions(const int32_t shareAvgSeconds, StratumSession *stratumSession);
  ~AgentSessions();

  int64_t getWorkerId(const uint16_t sessionId);

  void handleExMessage_SubmitShare     (const string *exMessage, const bool isWithTime);
  void handleExMessage_RegisterWorker  (const string *exMessage);
  void handleExMessage_UnRegisterWorker(const string *exMessage);

  void calcSessionsJobDiff(vector<uint8_t> &sessionsDiff2Exp);
  void getSessionsChangedDiff(const vector<uint8_t> &sessionsDiff2Exp,
                              string &data);
  void getSetDiffCommand(map<uint8_t, vector<uint16_t> > &diffSessionIds,
                         string &data);
};

#endif
