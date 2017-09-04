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
#ifndef POOL_WATCHER_H_
#define POOL_WATCHER_H_

#include "Common.h"
#include "Kafka.h"
#include "Utils.h"

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/listener.h>

#include <bitset>
#include <map>
#include <set>
#include <boost/thread/shared_mutex.hpp>

#include "utilities_js.hpp"
#include "bitcoin/base58.h"

#include "Stratum.h"

#define BTCCOM_WATCHER_AGENT   "btc.com-watcher/0.2"


class PoolWatchClient;
class ClientContainer;


///////////////////////////////// ClientContainer //////////////////////////////
class ClientContainer {
  atomic<bool> running_;
  vector<PoolWatchClient *> clients_;

  // libevent2
  struct event_base *base_;
  struct event *signal_event_;

  string kafkaBrokers_;
  KafkaProducer kafkaProducer_;  // produce GBT message
  KafkaConsumer kafkaStratumJobConsumer_;  // consume topic: 'StratumJob'

  StratumJob *poolStratumJob_; // the last stratum job from the pool itself
  boost::shared_mutex stratumJobMutex_;
  thread threadStratumJobConsume_;

  void runThreadStratumJobConsume();

public:
  ClientContainer(const string &kafkaBrokers);
  ~ClientContainer();

  bool addPools(const string &poolName, const string &poolHost,
                const int16_t poolPort, const string &workerName);
  bool init();
  void run();
  void stop();

  void removeAndCreateClient(PoolWatchClient *client);

  bool makeEmptyGBT(int32_t blockHeight, uint32_t nBits,
                    const string &blockPrevHash,
                    uint32_t blockTime, uint32_t blockVersion);

  static void readCallback (struct bufferevent *bev, void *ptr);
  static void eventCallback(struct bufferevent *bev, short events, void *ptr);

  boost::shared_lock<boost::shared_mutex> getPoolStratumJobReadLock();
  const StratumJob * getPoolStratumJob();
};


///////////////////////////////// PoolWatchClient //////////////////////////////
class PoolWatchClient {
  struct bufferevent *bev_;

  uint32_t extraNonce1_;
  uint32_t extraNonce2Size_;

  string lastPrevBlockHash_;

  bool handleMessage();
  void handleStratumMessage(const string &line);
  bool handleExMessage(struct evbuffer *inBuf);

public:
  enum State {
    INIT          = 0,
    CONNECTED     = 1,
    SUBSCRIBED    = 2,
    AUTHENTICATED = 3
  };
  State state_;
  ClientContainer *container_;

  string  poolName_;
  string  poolHost_;
  int16_t poolPort_;
  string  workerName_;

public:
  PoolWatchClient(struct event_base *base, ClientContainer *container,
                  const string &poolName,
                  const string &poolHost, const int16_t poolPort,
                  const string &workerName);
  ~PoolWatchClient();

  bool connect();

  void recvData();
  void sendData(const char *data, size_t len);
  inline void sendData(const string &str) {
    sendData(str.data(), str.size());
  }
};

#endif
