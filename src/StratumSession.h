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

#include <queue>

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>

#include <glog/logging.h>

#include "bitcoin/uint256.h"


////////////////////////////////// Connection //////////////////////////////////
class Connection {
public:
  struct bufferevent* bev_;
  evutil_socket_t fd_;
  void *server_;

public:
  Connection(evutil_socket_t fd, struct bufferevent* bev, void* server);
  void send(const char* data, size_t numBytes);
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


//////////////////////////////// StratumSession ////////////////////////////////
class StratumSession : public Connection {
  // mining state
  enum State {
    CONNECTED     = 0,
    SUBSCRIBED    = 1,
    AUTHENTICATED = 2,
    SUB_AND_AUTH  = 3,
    CLOSING       = 4,
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

  // extraNonce2 size is always 8 bytes
  uint32 extraNonce1_;  // MUST be unique across all servers

  std::queue<LocalJob> localJobs_;
  size_t kMaxNumLocalJobs_;

public:
  size_t lastNoEOLPos_;

  StratumSession(evutil_socket_t fd, struct bufferevent* bev, void* server);
  void readLine(const char *data, const size_t len);
  void setup();
};

#endif
