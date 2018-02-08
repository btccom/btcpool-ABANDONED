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

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>

#include <glog/logging.h>

#include "bitcoin/uint256.h"
#include "utilities_js.hpp"


///////////////////////////////// StratumClient ////////////////////////////////
class StratumClient {
  struct bufferevent *bev_;
  struct evbuffer *inBuf_;

  uint32_t extraNonce1_;  // session ID
  int32_t  extraNonce2Size_;
  uint64_t extraNonce2_;
  string workerFullName_;
  bool isMining_;
  string   latestJobId_;
  uint64_t latestDiff_;

  bool tryReadLine(string &line);
  void handleLine(const string &line);

public:
  // mining state
  enum State {
    INIT          = 0,
    CONNECTED     = 1,
    SUBSCRIBED    = 2,
    AUTHENTICATED = 3
  };
  atomic<State> state_;

public:
  StratumClient(struct event_base *base, const string &workerFullName);
  ~StratumClient();

  bool connect(struct sockaddr_in &sin);

  void sendData(const char *data, size_t len);
  inline void sendData(const string &str) {
    sendData(str.data(), str.size());
  }

  void readBuf(struct evbuffer *buf);
  void submitShare();
};



////////////////////////////// StratumClientWrapper ////////////////////////////
class StratumClientWrapper {
  atomic<bool> running_;
  struct event_base *base_;
  struct sockaddr_in sin_;
  uint32_t numConnections_;
  string userName_;   // miner usename
  string minerNamePrefix_;

  std::set<StratumClient *> connections_;

  thread threadSubmitShares_;
  void runThreadSubmitShares();

public:
  StratumClientWrapper(const char *host, const uint32_t port,
                       const uint32_t numConnections,
                       const string &userName, const string &minerNamePrefix);
  ~StratumClientWrapper();

  static void readCallback (struct bufferevent* bev, void *connection);
  static void eventCallback(struct bufferevent *bev, short events, void *ptr);

  void stop();
  void run();

  void submitShares();
};



//////////////////////////////// TCPClientWrapper //////////////////////////////
// simple tcp wrapper, use for test
class TCPClientWrapper {
  struct sockaddr_in servAddr_;  // server addr
  int sockfd_;
  struct evbuffer *inBuf_;

  void recv();

public:
  TCPClientWrapper();
  ~TCPClientWrapper();

  bool connect(const char *host, const int port);
  void send(const char *data, const size_t len);
  inline void send(const string &s) {
    send(s.data(), s.size());
  }
  void getLine(string &line);
};

#endif
