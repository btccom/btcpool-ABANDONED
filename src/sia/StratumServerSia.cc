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

#include <boost/make_unique.hpp>

using namespace std;

//////////////////////////////////// JobRepositorySia /////////////////////////////////
JobRepositorySia::JobRepositorySia(const char *kafkaBrokers, const char *consumerTopic, const string &fileLastNotifyTime, ServerSia *server) : 
  JobRepositoryBase(kafkaBrokers, consumerTopic, fileLastNotifyTime, server)
{
}

JobRepositorySia::~JobRepositorySia()
{

}

StratumJobEx* JobRepositorySia::createStratumJobEx(StratumJob *sjob, bool isClean){
  return new StratumJobEx(sjob, isClean);
}

void JobRepositorySia::broadcastStratumJob(StratumJob *sjob) {
  LOG(INFO) << "broadcast sia stratum job " << std::hex << sjob->jobId_;
  shared_ptr<StratumJobEx> exJob(createStratumJobEx(sjob, true));
  {
    ScopeLock sl(lock_);

    // mark all jobs as stale, should do this before insert new job
    for (auto it : exJobs_)
      it.second->markStale();

    // insert new job
    exJobs_[sjob->jobId_] = exJob;
  }

  sendMiningNotify(exJob);
}

////////////////////////////////// ServierSia ///////////////////////////////
ServerSia::~ServerSia()
{

}

unique_ptr<StratumSession> ServerSia::createConnection(struct bufferevent *bev,
                                                             struct sockaddr *saddr,
                                                             const uint32_t sessionID) {
  return boost::make_unique<StratumSessionSia>(*this, bev, saddr, sessionID);
}

JobRepository *ServerSia::createJobRepository(const char *kafkaBrokers,
                                            const char *consumerTopic,
                                           const string &fileLastNotifyTime)
{
  return new JobRepositorySia(kafkaBrokers, consumerTopic, fileLastNotifyTime, this);
}

void ServerSia::sendSolvedShare2Kafka(uint8* buf, int len) {
   kafkaProducerSolvedShare_->produce(buf, len);
}

