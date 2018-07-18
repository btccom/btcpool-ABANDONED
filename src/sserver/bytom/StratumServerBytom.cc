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
using namespace std;


///////////////////////////////////JobRepositoryBytom///////////////////////////////////
StratumJobEx* JobRepositoryBytom::createStratumJobEx(StratumJob *sjob, bool isClean){
  return new StratumJobExNoInit(sjob, isClean);
}

///////////////////////////////ServerBytom///////////////////////////////
JobRepository *ServerBytom::createJobRepository(const char *kafkaBrokers,
                                                const char *consumerTopic,
                                                const string &fileLastNotifyTime,
                                                Server *server)
{
  return new JobRepositoryBytom(kafkaBrokers, consumerTopic, fileLastNotifyTime, this);
}

StratumSession *ServerBytom::createSession(evutil_socket_t fd, struct bufferevent *bev,
                                           Server *server, struct sockaddr *saddr,
                                           const int32_t shareAvgSeconds,
                                           const uint32_t sessionID)
{
  return new StratumSessionBytom(fd, bev, server, saddr,
                                 server->kShareAvgSeconds_,
                                 sessionID);
}

void ServerBytom::sendSolvedShare2Kafka(uint64_t nonce, const string &strHeader,
                                      uint64_t height, uint64_t networkDiff, const StratumWorker &worker)
{
  string msg = Strings::Format("{\"nonce\":%lu,\"header\":\"%s\","
                               "\"height\":%lu,\"networkDiff\":%" PRIu64 ",\"userId\":%ld,"
                               "\"workerId\":%" PRId64 ",\"workerFullName\":\"%s\"}",
                               nonce, strHeader.c_str(),
                               height, networkDiff, worker.userId_,
                               worker.workerHashId_, filterWorkerName(worker.fullName_).c_str());
  kafkaProducerSolvedShare_->produce(msg.c_str(), msg.length());
}