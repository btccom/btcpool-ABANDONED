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
#ifndef STRATUM_SERVER_BYTOM_H_
#define STRATUM_SERVER_BYTOM_H_

#include "StratumServer.h"
#include "StratumBytom.h"

class JobRepositoryBytom;

class ServerBytom : public ServerBase<JobRepositoryBytom> {
public:
  JobRepository *createJobRepository(
      size_t chainId,
      const char *kafkaBrokers,
      const char *consumerTopic,
      const string &fileLastNotifyTime,
      bool niceHashForced,
      uint64_t niceHashMinDiff,
      const std::string &niceHashMinDiffZookeeperPath) override;

  unique_ptr<StratumSession> createConnection(
      struct bufferevent *bev,
      struct sockaddr *saddr,
      const uint32_t sessionID) override;
  void sendSolvedShare2Kafka(
      size_t chainId,
      uint64_t nonce,
      const string &strHeader,
      uint64_t height,
      uint64_t networkDiff,
      const StratumWorker &worker);
};

class JobRepositoryBytom : public JobRepositoryBase<ServerBytom> {
private:
  uint32_t lastHeight_ = 0;

public:
  using JobRepositoryBase::JobRepositoryBase;
  shared_ptr<StratumJob> createStratumJob() override {
    return std::make_shared<StratumJobBytom>();
  }
  shared_ptr<StratumJobEx>
  createStratumJobEx(shared_ptr<StratumJob> sjob, bool isClean) override;
  void broadcastStratumJob(shared_ptr<StratumJob> sjob) override;
};

#endif // STRATUM_SERVER_BYTOM_H_
