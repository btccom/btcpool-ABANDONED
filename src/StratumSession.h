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

class Server;
class StratumJobEx;
class DiffController;

//////////////////////////////// DiffController ////////////////////////////////
class DiffController {
  static const int32_t kMinDiff_       = 1;     // min diff
  static const int32_t kDefaultDiff_   = 1024;  // default diff, 2^N
  static const int32_t kDiffWindow_    = 900;   // time window, seconds, 60*N
  static const int32_t kRecordSeconds_ = 10;    // every N seconds as a record

  time_t startTime_;  // first job send time
  uint64  minDiff_;
  uint64  curDiff_;
  int32_t shareAvgSeconds_;
  int32_t curHashRateLevel_;

  StatsWindow<double> sharesNum_;  // share count
  StatsWindow<uint64> shares_;     // share

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



//
// cgminer support methods:
//   mining.notify
//   mining.set_difficulty
//   client.reconnect
//   client.get_version
//   client.show_message
//   mining.ping
//

//////////////////////////////// StratumSession ////////////////////////////////
class StratumSession {
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
    uint256  jobTarget_;
    std::set<LocalShare> submitShares_;

    LocalJob(): jobId_(0), jobDifficulty_(0){}

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
  DiffController diffController_;
  State state_;
  StratumWorker worker_;
  string clientAgent_;  // eg. bfgminer/4.4.0-32-gac4e9b3
  string clientIp_;

  uint32 extraNonce1_;   // MUST be unique across all servers
  static const int kExtraNonce2Size_ = 8;  // extraNonce2 size is always 8 bytes

  uint64_t currDiff_;
  std::deque<LocalJob> localJobs_;
  size_t kMaxNumLocalJobs_;

  struct evbuffer *inBuf_;
  struct evbuffer *outBuf_;
  mutex  writeLock_;
  size_t lastNoEOLPos_;
  bool   isPoolWatcher_;

  void setup();
  void setReadTimeout(const int32_t timeout);
//  void close();

  bool tryReadLine(string &line);
  void responseError(const string &idStr, int code);
  void responseTrue(const string &idStr);
  void handleLine(const string &line);
  void handleRequest(const string &idStr, const string &method, const JsonNode &jparams);

  void handleRequest_Subscribe        (const string &idStr, const JsonNode &jparams);
  void handleRequest_Authorize        (const string &idStr, const JsonNode &jparams);
  void handleRequest_Submit           (const string &idStr, const JsonNode &jparams);
  void handleRequest_SuggestTarget    (const string &idStr, const JsonNode &jparams);
  void handleRequest_SuggestDifficulty(const string &idStr, const JsonNode &jparams);
  void _handleRequest_SetDifficulty(uint64_t suggestDiff);

  LocalJob *findLocalJob(uint64_t jobId);

public:
  struct bufferevent* bev_;
  evutil_socket_t fd_;
  Server *server_;

public:
  StratumSession(evutil_socket_t fd, struct bufferevent *bev,
                 Server *server, struct sockaddr *saddr,
                 const int32_t shareAvgSeconds);
  ~StratumSession();

  void sendSetDifficulty(const uint64_t difficulty);
  void sendMiningNotify(shared_ptr<StratumJobEx> exJobPtr);
  void send(const char *data, size_t len);
  inline void send(const string &str) {
    send(str.data(), str.size());
  }
  void readBuf(struct evbuffer *buf);
};

#endif
