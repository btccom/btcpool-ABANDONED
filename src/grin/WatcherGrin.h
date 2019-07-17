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

#include "MySQLConnection.h"

#include <queue>
#include <unordered_map>

class StratumJobGrin;
class PoolWatchClientGrin;

class ClientContainerGrin : public ClientContainer {
public:
  explicit ClientContainerGrin(const libconfig::Config &config);

  PoolWatchClient *
  createPoolWatchClient(const libconfig::Setting &config) override;
  bool initInternal() override;

  bool sendJobToKafka(const StratumJobGrin &job, PoolWatchClientGrin *client);
  MysqlConnectInfo &getMysqlInfo() { return poolDB_; }

private:
  void runThreadSolvedShareConsume();
  void consumeSolvedShare(rd_kafka_message_t *rkmessage);

  KafkaSimpleConsumer kafkaSolvedShareConsumer_; // consume solved_share_topic
  thread threadSolvedShareConsume_;

  struct JobCache {
    uint64_t nodeJobId;
    size_t clientId;
  };

  static const size_t kMaxJobCacheSize_ = 5000;
  std::mutex jobCacheLock_;
  std::queue<string> jobCacheQueue_;
  std::unordered_map<string, JobCache> jobCacheMap_;

  MysqlConnectInfo poolDB_; // save blocks to table `found_blocks`
};

class PoolWatchClientGrin : public PoolWatchClient {
public:
  PoolWatchClientGrin(
      struct event_base *base,
      ClientContainer *container,
      const libconfig::Setting &config);

  void onConnected() override;

protected:
  void handleStratumMessage(const string &line) override;

private:
  string password_;
};
