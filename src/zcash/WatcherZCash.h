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
#ifndef POOL_WATCHER_ZCASH_H_
#define POOL_WATCHER_ZCASH_H_

#include "Watcher.h"
#include "StratumZCash.h"


///////////////////////////////// ClientContainer //////////////////////////////
class ClientContainerZCash : public ClientContainer
{
  boost::shared_mutex stratumJobMutex_;
  StratumJobZCash *poolStratumJob_; // the last stratum job from the pool itself
protected:
  void consumeStratumJobInternal(const string& str) override;
  string createOnConnectedReplyString() const override;
  PoolWatchClient* createPoolWatchClient( 
                struct event_base *base, 
                const string &poolName, const string &poolHost,
                const int16_t poolPort, const string &workerName) override;

public:
  ClientContainerZCash(const string &kafkaBrokers, const string &jobTopic, const string &gbtTopic,
                         bool disableChecking);
  ~ClientContainerZCash();

  bool sendEmptyGBT(const string &poolName,
                    int32_t blockHeight, uint32_t nBits,
                    const string &blockPrevHash,
                    uint32_t blockTime, uint32_t blockVersion);

  const StratumJobZCash * getPoolStratumJob();
  boost::shared_lock<boost::shared_mutex> getPoolStratumJobReadLock();
};


///////////////////////////////// PoolWatchClient //////////////////////////////
class PoolWatchClientZCash : public PoolWatchClient
{
  uint32_t extraNonce1_;
  uint32_t extraNonce2Size_;

  string lastPrevBlockHash_;

  void handleStratumMessage(const string &line) override;

public:
  PoolWatchClientZCash(struct event_base *base, ClientContainerZCash *container,
                  bool disableChecking,
                  const string &poolName,
                  const string &poolHost, const int16_t poolPort,
                  const string &workerName);
  ~PoolWatchClientZCash();

  ClientContainerZCash* GetContainerBitcoin(){ return static_cast<ClientContainerZCash*>(container_); }
};

#endif
