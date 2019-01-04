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

#include "Watcher.h"
#include "StratumBeam.h"

#include <queue>

class PoolWatchClientBeam;

///////////////////////////////// ClientContainer //////////////////////////////
class ClientContainerBeam : public ClientContainer
{
protected:
  struct JobCache {
    string jobId_;
    StratumJobBeam sJob_;
    size_t clientId_;
  };

  KafkaConsumer kafkaSolvedShareConsumer_;  // consume solved_share_topic
  thread threadSolvedShareConsume_;

  const size_t kMaxJobCacheSize_ = 5000;
  std::queue<string> jobCacheKeyQ_;
  map<string, JobCache> jobCacheMap_;
  std::mutex jobCacheLock_;
  
  PoolWatchClient* createPoolWatchClient(const libconfig::Setting &config) override;
  bool initInternal() override;
  void runThreadSolvedShareConsume();
  void consumeSolvedShare(rd_kafka_message_t *rkmessage);

public:
  ClientContainerBeam(const libconfig::Config &config);
  ~ClientContainerBeam();

  bool sendJobToKafka(const string jobId, const StratumJobBeam &job, PoolWatchClientBeam *client);
};


///////////////////////////////// PoolWatchClient //////////////////////////////
class PoolWatchClientBeam : public PoolWatchClient
{
protected:
  std::mutex wantSubmittedSharesLock_;
  string wantSubmittedShares_;

  void handleStratumMessage(const string &line) override;

public:
  PoolWatchClientBeam(
    struct event_base *base,
    ClientContainerBeam *container,
    const libconfig::Setting &config
  );
  ~PoolWatchClientBeam();

  void onConnected() override;
  void submitShare(string submitJson);

  ClientContainerBeam* GetContainerBeam(){ return static_cast<ClientContainerBeam*>(container_); }
};
