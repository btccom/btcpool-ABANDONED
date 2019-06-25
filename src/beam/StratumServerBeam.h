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

#include "CommonBeam.h"

#include <set>
#include "StratumServer.h"
#include "StratumBeam.h"

class JobRepositoryBeam;

class ServerBeam : public ServerBase<JobRepositoryBeam> {
  bool noncePrefixCheck_ = true;

public:
  bool setupInternal(const libconfig::Config &config) override;
  void checkAndUpdateShare(
      size_t chainId,
      ShareBeam &share,
      shared_ptr<StratumJobEx> exjob,
      const string &output,
      const std::set<uint64_t> &jobDiffs,
      const string &workFullName,
      uint256 &computedShareHash);
  void sendSolvedShare2Kafka(
      size_t chainId,
      const ShareBeam &share,
      const string &input,
      const string &output,
      const StratumWorker &worker,
      const uint256 &blockHash);

  JobRepository *createJobRepository(
      size_t chainId,
      const char *kafkaBrokers,
      const char *consumerTopic,
      const string &fileLastNotifyTime) override;

  unique_ptr<StratumSession> createConnection(
      struct bufferevent *bev,
      struct sockaddr *saddr,
      const uint32_t sessionID) override;

  bool noncePrefixCheck() { return noncePrefixCheck_; }
};

class JobRepositoryBeam : public JobRepositoryBase<ServerBeam> {
public:
  JobRepositoryBeam(
      size_t chainId,
      ServerBeam *server,
      const char *kafkaBrokers,
      const char *consumerTopic,
      const string &fileLastNotifyTime);
  virtual ~JobRepositoryBeam();

  shared_ptr<StratumJob> createStratumJob() override {
    return make_shared<StratumJobBeam>();
  }
  shared_ptr<StratumJobEx>
  createStratumJobEx(shared_ptr<StratumJob> sjob, bool isClean) override;
  void broadcastStratumJob(shared_ptr<StratumJob> sjob) override;

private:
  uint32_t lastHeight_;
};
