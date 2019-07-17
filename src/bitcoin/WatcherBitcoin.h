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
#ifndef POOL_WATCHER_BITCOIN_H_
#define POOL_WATCHER_BITCOIN_H_

#include "Watcher.h"
#include "StratumBitcoin.h"

///////////////////////////////// ClientContainer //////////////////////////////
class ClientContainerBitcoin : public ClientContainer {
  bool disableChecking_;
  KafkaSimpleConsumer kafkaStratumJobConsumer_; // consume topic: 'StratumJob'
  thread threadStratumJobConsume_;

  boost::shared_mutex stratumJobMutex_;
  shared_ptr<StratumJobBitcoin>
      poolStratumJob_; // the last stratum job from the pool itself

protected:
  bool initInternal() override;
  void runThreadStratumJobConsume();
  void consumeStratumJob(rd_kafka_message_t *rkmessage);
  void handleNewStratumJob(const string &str);

  PoolWatchClient *
  createPoolWatchClient(const libconfig::Setting &config) override;

public:
  ClientContainerBitcoin(const libconfig::Config &config);
  ~ClientContainerBitcoin();

  bool sendEmptyGBT(
      const string &poolName,
      int32_t blockHeight,
      uint32_t nBits,
      const string &blockPrevHash,
      uint32_t blockTime,
      uint32_t blockVersion);

  const shared_ptr<StratumJobBitcoin> getPoolStratumJob();
  boost::shared_lock<boost::shared_mutex> getPoolStratumJobReadLock();
  bool disableChecking() { return disableChecking_; }
};

///////////////////////////////// PoolWatchClient //////////////////////////////
class PoolWatchClientBitcoin : public PoolWatchClient {
  uint32_t extraNonce1_;
  uint32_t extraNonce2Size_;

  string lastPrevBlockHash_;
  bool disableChecking_;

  void handleStratumMessage(const string &line) override;

public:
  PoolWatchClientBitcoin(
      struct event_base *base,
      ClientContainerBitcoin *container,
      const libconfig::Setting &config);
  ~PoolWatchClientBitcoin();

  void onConnected() override;

  ClientContainerBitcoin *GetContainerBitcoin() {
    return static_cast<ClientContainerBitcoin *>(container_);
  }
};

#endif
