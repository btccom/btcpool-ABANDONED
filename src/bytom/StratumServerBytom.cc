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
#include "StratumServerBytom.h"

#include "StratumSessionBytom.h"
#include "DiffController.h"

using namespace std;

///////////////////////////////////JobRepositoryBytom///////////////////////////////////

shared_ptr<StratumJobEx> JobRepositoryBytom::createStratumJobEx(
    shared_ptr<StratumJob> sjob, bool isClean) {
  return std::make_shared<StratumJobEx>(chainId_, sjob, isClean);
}

void JobRepositoryBytom::broadcastStratumJob(shared_ptr<StratumJob> sjobBase) {
  auto sjob = std::static_pointer_cast<StratumJobBytom>(sjobBase);

  bool isClean = false;
  if (sjob->blockHeader_.height > lastHeight_) {
    isClean = true;
    lastHeight_ = sjob->blockHeader_.height;
    LOG(INFO) << "received new height stratum job, height: "
              << sjob->blockHeader_.height
              << ", prevhash: " << sjob->blockHeader_.previousBlockHash.c_str();
  }
  shared_ptr<StratumJobEx> exJob(createStratumJobEx(sjob, isClean));

  if (isClean) {
    // mark all jobs as stale, should do this before insert new job
    for (auto it : exJobs_) {
      it.second->markStale();
    }
  }

  // insert new job
  exJobs_[sjob->jobId_] = exJob;

  if (isClean) {
    sendMiningNotify(exJob);
  }
}

///////////////////////////////ServerBytom///////////////////////////////
JobRepository *ServerBytom::createJobRepository(
    size_t chainId,
    const char *kafkaBrokers,
    const char *consumerTopic,
    const string &fileLastNotifyTime,
    bool niceHashForced,
    uint64_t niceHashMinDiff,
    const std::string &niceHashMinDiffZookeeperPath) {
  return new JobRepositoryBytom(
      chainId,
      this,
      kafkaBrokers,
      consumerTopic,
      fileLastNotifyTime,
      niceHashForced,
      niceHashMinDiff,
      niceHashMinDiffZookeeperPath);
}

unique_ptr<StratumSession> ServerBytom::createConnection(
    struct bufferevent *bev, struct sockaddr *saddr, const uint32_t sessionID) {
  return std::make_unique<StratumSessionBytom>(*this, bev, saddr, sessionID);
}

void ServerBytom::sendSolvedShare2Kafka(
    size_t chainId,
    uint64_t nonce,
    const string &strHeader,
    uint64_t height,
    uint64_t networkDiff,
    const StratumWorker &worker) {
  string msg = Strings::Format(
      "{\"nonce\":%u,\"header\":\"%s\","
      "\"height\":%u,\"networkDiff\":%u,\"userId\":%d,"
      "\"workerId\":%d,\"workerFullName\":\"%s\"}",
      nonce,
      strHeader,
      height,
      networkDiff,
      worker.userId(chainId),
      worker.workerHashId_,
      filterWorkerName(worker.fullName_));
  ServerBase::sendSolvedShare2Kafka(chainId, msg.data(), msg.size());
}
