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
#ifndef STRATUM_CLIENT_H_
#define STRATUM_CLIENT_H_

#include "Common.h"

#include <netinet/in.h>
#include <sys/socket.h>
#include <deque>
#include <vector>

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/dns.h>

#include <glog/logging.h>
#include <arith_uint256.h>
#include <uint256.h>
#include "utilities_js.hpp"

#include <type_traits>

namespace libconfig {
class Config;
}

///////////////////////////////// StratumClient ////////////////////////////////
class StratumClient {
protected:
  bool enableTLS_;
  struct bufferevent *bev_;
  struct evbuffer *inBuf_;
  struct evdns_base *evdnsBase_;

  uint32_t sessionId_; // session ID
  int32_t extraNonce2Size_;
  uint64_t extraNonce2_;
  string workerFullName_;
  string workerPasswd_;
  uint32_t sharesPerTx_;
  bool isMining_;
  string latestJobId_;
  uint64_t latestDiff_;
  arith_uint256 latestTarget_;

  bool tryReadLine(string &line);
  virtual void handleLine(const string &line);

public:
  // mining state
  enum State { INIT = 0, CONNECTED = 1, SUBSCRIBED = 2, AUTHENTICATED = 3 };
  atomic<State> state_;

  using Factory = function<unique_ptr<StratumClient>(
      bool,
      struct event_base *,
      const string &,
      const string &,
      const libconfig::Config &)>;
  static bool registerFactory(const string &chainType, Factory factory);
  template <typename T>
  static bool registerFactory(const string &chainType) {
    static_assert(
        std::is_base_of<StratumClient, T>::value,
        "Factory is not constructing the correct type");
    return registerFactory(
        chainType,
        [](bool enableTLS,
           struct event_base *base,
           const string &workerFullName,
           const string &workerPasswd,
           const libconfig::Config &config) {
          return std::make_unique<T>(
              enableTLS, base, workerFullName, workerPasswd, config);
        });
  }

public:
  StratumClient(
      bool enableTLS,
      struct event_base *base,
      const string &workerFullName,
      const string &workerPasswd,
      const libconfig::Config &config);
  virtual ~StratumClient();

  bool connect(const string &host, uint16_t port);
  virtual void sendHelloData();

  void sendData(const char *data, size_t len);
  inline void sendData(const string &str) { sendData(str.data(), str.size()); }

  void readBuf(struct evbuffer *buf);
  void submitShare();
  virtual string constructShare();
};

////////////////////////////// StratumClientWrapper ////////////////////////////
class StratumClientWrapper {
  bool running_;
  bool enableTLS_;
  string host_;
  uint16_t port_;
  struct event_base *base_;
  struct event *timer_;
  struct event *sigterm_;
  struct event *sigint_;
  uint32_t numConnections_;
  string userName_; // miner usename
  string minerNamePrefix_;
  string passwd_; // miner password, used to set difficulty
  string type_;
  const libconfig::Config &config_;
  std::vector<unique_ptr<StratumClient>> connections_;

  void submitShares();

public:
  StratumClientWrapper(
      bool enableTLS,
      const string &host,
      const uint32_t port,
      const uint32_t numConnections,
      const string &userName,
      const string &minerNamePrefix,
      const string &passwd,
      const string &type,
      const libconfig::Config &config);
  ~StratumClientWrapper();

  static void readCallback(struct bufferevent *bev, void *connection);
  static void eventCallback(struct bufferevent *bev, short events, void *ptr);
  static void timerCallback(evutil_socket_t fd, short event, void *ptr);
  static void signalCallback(evutil_socket_t fd, short event, void *ptr);

  void stop();
  void run();

  unique_ptr<StratumClient> createClient(
      bool enableTLS,
      struct event_base *base,
      const string &workerFullName,
      const string &workerPasswd);
};

//////////////////////////////// TCPClientWrapper //////////////////////////////
// simple tcp wrapper, use for test
class TCPClientWrapper {
  int sockfd_;
  struct evbuffer *inBuf_;

  void recv();

public:
  TCPClientWrapper();
  ~TCPClientWrapper();

  bool connect(const char *host, const int port);
  void send(const char *data, const size_t len);
  inline void send(const string &s) { send(s.data(), s.size()); }
  void getLine(string &line);
};

#endif
