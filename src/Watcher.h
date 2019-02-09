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

#define BTCCOM_WATCHER_AGENT "btc.com-watcher/0.2"

class PoolWatchClient;
class ClientContainer;

///////////////////////////////// ClientContainer //////////////////////////////
class ClientContainer {
protected:
  atomic<bool> running_;
  bool disableChecking_;
  vector<PoolWatchClient *> clients_;

  // libevent2
  struct event_base *base_;

  string kafkaBrokers_;

  thread threadStratumJobConsume_;

  void runThreadStratumJobConsume();
  void consumeStratumJob(rd_kafka_message_t *rkmessage);
  KafkaProducer kafkaProducer_; // produce GBT message
  KafkaConsumer kafkaStratumJobConsumer_; // consume topic: 'StratumJob'

  virtual void consumeStratumJobInternal(const string &str) = 0;
  virtual string createOnConnectedReplyString() const = 0;
  virtual PoolWatchClient *createPoolWatchClient(
      struct event_base *base,
      const string &poolName,
      const string &poolHost,
      const int16_t poolPort,
      const string &workerName) = 0;

  ClientContainer(
      const string &kafkaBrokers,
      const string &consumerTopic,
      const string &producerTopic,
      bool disableChecking);

public:
  virtual ~ClientContainer();

  bool addPools(
      const string &poolName,
      const string &poolHost,
      const int16_t poolPort,
      const string &workerName);
  virtual bool init();
  void run();
  void stop();

  void removeAndCreateClient(PoolWatchClient *client);

  static void readCallback(struct bufferevent *bev, void *ptr);
  static void eventCallback(struct bufferevent *bev, short events, void *ptr);
};

///////////////////////////////// PoolWatchClient //////////////////////////////
class PoolWatchClient {
protected:
  bool disableChecking_;
  struct bufferevent *bev_;

  bool handleMessage();
  virtual void handleStratumMessage(const string &line) = 0;

public:
  enum State { INIT = 0, CONNECTED = 1, SUBSCRIBED = 2, AUTHENTICATED = 3 };
  State state_;
  ClientContainer *container_;

  string poolName_;
  string poolHost_;
  int16_t poolPort_;
  string workerName_;

protected:
  PoolWatchClient(
      struct event_base *base,
      ClientContainer *container,
      bool disableChecking,
      const string &poolName,
      const string &poolHost,
      const int16_t poolPort,
      const string &workerName);

public:
  virtual ~PoolWatchClient();

  bool connect();

  void recvData();
  void sendData(const char *data, size_t len);
  inline void sendData(const string &str) { sendData(str.data(), str.size()); }
};

#endif
