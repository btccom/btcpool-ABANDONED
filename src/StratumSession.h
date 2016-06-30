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
#include <queue>

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>

#include <glog/logging.h>

#include "bitcoin/uint256.h"
#include "utilities_js.hpp"

//////////////////////////////// StratumError ////////////////////////////////
class StratumError {
public:
  enum {
    NO_ERROR        = 0,

    UNKNOWN         = 20,
    JOB_NOT_FOUND   = 21,
    DUPLICATE_SHARE = 22,
    LOW_DIFFICULTY  = 23,
    UNAUTHORIZED    = 24,
    NOT_SUBSCRIBED  = 25,

    ILLEGAL_METHOD   = 26,
    ILLEGAL_PARARMS  = 27,
    IP_BANNED        = 28,
    INVALID_USERNAME = 29,
    INTERNAL_ERROR   = 30,
    TIME_TOO_OLD     = 31,
    TIME_TOO_NEW     = 32
  };
  static const char * toString(int err);
};

//////////////////////////////// StratumWorker ////////////////////////////////
class StratumWorker {
public:
  int32_t  userId_;
  uint64_t workerHashId_;  // substr(0, 8, HASH(wokerName))

  string fullName_;  // fullName = username.workername
  string userName_;
  string workerName_;

public:
  StratumWorker();
  void setUserIDAndNames(const int32_t userId, const string &fullName);
  string getUserName(const string &fullName);
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
    uint64_t jobid_;
    uint64_t difficulty_;     // only support strict integer difficulty
    uint256  target_;
    uint32_t nBits_;
    std::set<LocalShare> submitShares_;

    LocalJob(): jobid_(0), difficulty_(0), nBits_(0) {}

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
//  DiffController diffController_;
  State state_;
  StratumWorker worker_;
  string clientAgent_;  // eg. bfgminer/4.4.0-32-gac4e9b3
  string clientIp_;

  uint32 extraNonce1_;   // MUST be unique across all servers
  static const int kExtraNonce2Size_ = 8;  // extraNonce2 size is always 8 bytes

  std::queue<LocalJob> localJobs_;
  size_t kMaxNumLocalJobs_;

  struct evbuffer *inBuf_;
  struct evbuffer *outBuf_;
  mutex writeLock_;
  size_t lastNoEOLPos_;

  void setup();
  void setReadTimeout(const int32_t timeout);
//  void close();

  bool tryReadLine(string &line);
  void responseError(const string &idStr, int code);
  void responseTrue(const string &idStr);
  void handleLine(const string &line);
  void handleRequest(const string &idStr, const string &method, const JsonNode &jparams);

  void handleRequest_Subscribe(const string &idStr, const JsonNode &jparams);
  void handleRequest_Authorize(const string &idStr, const JsonNode &jparams);
  void handleRequest_SuggestTarget(const string &idStr, const JsonNode &jparams);
  void handleRequest_SuggestDifficulty(const string &idStr, const JsonNode &jparams);
  void handleRequest_Submit();
  void _handleRequest_SetDifficulty(uint64_t suggestDiff);


public:
  struct bufferevent* bev_;
  evutil_socket_t fd_;
  void *server_;

public:
  StratumSession(evutil_socket_t fd, struct bufferevent *bev,
                 void *server, struct sockaddr *saddr);
  ~StratumSession();

  void send(const char *data, size_t len);
  void readBuf(struct evbuffer *buf);
};

#endif
