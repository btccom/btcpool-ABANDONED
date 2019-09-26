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

#include "StratumServerSia.h"

#include "StratumSessionSia.h"
#include "DiffController.h"

using namespace std;

//////////////////////////////////// JobRepositorySia
////////////////////////////////////
JobRepositorySia::JobRepositorySia(
    size_t chainId,
    ServerSia *server,
    const char *kafkaBrokers,
    const char *consumerTopic,
    const string &fileLastNotifyTime,
    bool niceHashForced,
    uint64_t niceHashMinDiff,
    const std::string &niceHashMinDiffZookeeperPath)
  : JobRepositoryBase(
        chainId,
        server,
        kafkaBrokers,
        consumerTopic,
        fileLastNotifyTime,
        niceHashForced,
        niceHashMinDiff,
        niceHashMinDiffZookeeperPath) {
}

JobRepositorySia::~JobRepositorySia() {
}

shared_ptr<StratumJobEx> JobRepositorySia::createStratumJobEx(
    shared_ptr<StratumJob> sjob, bool isClean) {
  return std::make_shared<StratumJobEx>(chainId_, sjob, isClean);
}

void JobRepositorySia::broadcastStratumJob(shared_ptr<StratumJob> sjob) {
  LOG(INFO) << "broadcast sia stratum job " << std::hex << sjob->jobId_;
  shared_ptr<StratumJobEx> exJob(createStratumJobEx(sjob, true));

  // mark all jobs as stale, should do this before insert new job
  for (auto it : exJobs_)
    it.second->markStale();

  // insert new job
  exJobs_[sjob->jobId_] = exJob;

  sendMiningNotify(exJob);
}

////////////////////////////////// ServierSia ///////////////////////////////
unique_ptr<StratumSession> ServerSia::createConnection(
    struct bufferevent *bev, struct sockaddr *saddr, const uint32_t sessionID) {
  return std::make_unique<StratumSessionSia>(*this, bev, saddr, sessionID);
}

JobRepository *ServerSia::createJobRepository(
    size_t chainId,
    const char *kafkaBrokers,
    const char *consumerTopic,
    const string &fileLastNotifyTime,
    bool niceHashForced,
    uint64_t niceHashMinDiff,
    const std::string &niceHashMinDiffZookeeperPath) {
  return new JobRepositorySia(
      chainId,
      this,
      kafkaBrokers,
      consumerTopic,
      fileLastNotifyTime,
      niceHashForced,
      niceHashMinDiff,
      niceHashMinDiffZookeeperPath);
}
