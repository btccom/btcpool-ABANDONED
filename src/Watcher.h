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
#include <event2/bufferevent_ssl.h>
#include <event2/dns.h>
#include <libconfig.h++>

#include <bitset>
#include <map>
#include <set>
#include <memory>
#include <boost/thread/shared_mutex.hpp>

// see PoolWatcherAgent in StratumSession.cc
#define BTCCOM_WATCHER_AGENT "__PoolWatcher__"
#define BTCCOM_GRANDPOOL_WATCHER_AGENT "__PoolGrandPoolWatcher__"

class PoolWatchClient;
class ClientContainer;

///////////////////////////////// ClientContainer //////////////////////////////
class ClientContainer {
protected:
  atomic<bool> running_;
  vector<shared_ptr<PoolWatchClient>> clients_;

  // libevent2
  struct event_base *base_;

  string kafkaBrokers_;
  KafkaProducer kafkaProducer_; // produce GBT message

  IdGenerator gen_;

  virtual PoolWatchClient *
  createPoolWatchClient(const libconfig::Setting &config) = 0;
  virtual bool initInternal() = 0;

public:
  ClientContainer(const libconfig::Config &config);
  virtual ~ClientContainer();

  bool addPools(const libconfig::Setting &config);
  bool init();
  void run();
  void stop();

  void removeAndCreateClient(PoolWatchClient *client);
  uint64_t generateJobId() { return gen_.next(); }
};

///////////////////////////////// PoolWatchClient //////////////////////////////
class PoolWatchClient {
protected:
  bool enableTLS_;
  struct evdns_base *evdnsBase_;
  struct event *reconnectEvent_;
  struct bufferevent *bev_;

  bool handleMessage();
  virtual void handleStratumMessage(const string &line) = 0;

public:
  enum State {
    INIT = 0,
    CONNECTED = 1,
    SUBSCRIBED = 2,
    AUTHENTICATED = 3,
    TODO_RECONNECT = 4
  };

  State state_;
  ClientContainer *container_;

  const libconfig::Setting &config_;
  string poolName_;
  string poolHost_;
  uint16_t poolPort_;
  string workerName_;

  time_t upTime_;

protected:
  PoolWatchClient(
      struct event_base *base,
      ClientContainer *container,
      const libconfig::Setting &config);

public:
  virtual ~PoolWatchClient();

  bool connect();
  virtual void onConnected() = 0;

  void recvData();
  void sendData(const char *data, size_t len);
  inline void sendData(const string &str) { sendData(str.data(), str.size()); }

  static void readCallback(struct bufferevent *bev, void *ptr);
  static void eventCallback(struct bufferevent *bev, short events, void *ptr);
  static void reconnectCallback(evutil_socket_t fd, short events, void *ptr);

  static void triggerReconnect(PoolWatchClient *client);
};

#endif
