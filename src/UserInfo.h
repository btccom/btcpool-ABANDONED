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
#pragma once

#include <algorithm>
#include <boost/thread.hpp>

#include "utilities_js.hpp"

class StratumServer;

///////////////////////////////////// UserInfo /////////////////////////////////
// 1. update userName->userId by interval
// 2. insert worker name to db
class UserInfo {
  struct WorkerName {
    int32_t userId_;
    int64_t workerId_;
    char    workerName_[21];
    char    minerAgent_[31];

    WorkerName(): userId_(0), workerId_(0) {
      memset(workerName_, 0, sizeof(workerName_));
      memset(minerAgent_, 0, sizeof(minerAgent_));
    }
  };

  //--------------------
  pthread_rwlock_t rwlock_;
  atomic<bool> running_;
  string apiUrl_;

  // username -> userId
  std::unordered_map<string, int32_t> nameIds_;
  int32_t lastMaxUserId_;
  bool caseInsensitive_;

#ifdef USER_DEFINED_COINBASE
  // userId -> userCoinbaseInfo
  std::unordered_map<int32_t, string> idCoinbaseInfos_;
  int64_t lastTime_;
#endif

  // workerName
  mutex workerNameLock_;
  std::deque<WorkerName> workerNameQ_;
  StratumServer *server_;

  thread threadInsertWorkerName_;

  void runThreadInsertWorkerName();
  int32_t insertWorkerName();

  thread threadUpdate_;
  void runThreadUpdate();
  int32_t incrementalUpdateUsers();

public:
  UserInfo(StratumServer *server, const libconfig::Config &config);
  ~UserInfo();

  void stop();
  bool setupThreads();

  void regularUserName(string &userName);
  int32_t getUserId(string userName);

#ifdef USER_DEFINED_COINBASE
  string  getCoinbaseInfo(int32_t userId);
#endif

  void addWorker(const int32_t userId, const int64_t workerId,
                 const string &workerName, const string &minerAgent);
};
