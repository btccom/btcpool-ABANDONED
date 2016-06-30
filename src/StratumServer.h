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
#ifndef STRATUM_SERVER_H_
#define STRATUM_SERVER_H_

#include "Common.h"

#include <arpa/inet.h>
#include <sys/socket.h>

#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/listener.h>
#include <event2/util.h>
#include <event2/event.h>

#include <glog/logging.h>

#include "StratumSession.h"


///////////////////////////////////// Server ///////////////////////////////////
class Server {
  struct sockaddr_in sin_;
  struct event_base* base_;
  struct event* signal_event_;
  struct evconnlistener* listener_;

  map<evutil_socket_t, StratumSession *> connections_;

public:
  Server();
  ~Server();

  bool setup(const char *ip, const unsigned short port);
  void run();
  void stop();

  void sendToAllClients(const char *data, size_t len);
  void addConnection(evutil_socket_t fd, StratumSession *connection);
  void removeConnection(evutil_socket_t fd);

  static void listenerCallback(struct evconnlistener* listener,
                               evutil_socket_t socket,
                               struct sockaddr* saddr,
                               int socklen, void* server);
  static void readCallback (struct bufferevent *, void *connection);
  static void eventCallback(struct bufferevent *, short, void *connection);
};


////////////////////////////////// StratumServer ///////////////////////////////
class StratumServer {
  atomic<bool> running_;

  Server server_;
  string ip_;
  unsigned short port_;

public:
  StratumServer(const char *ip, const unsigned short port);
  ~StratumServer();

  bool init();
  void stop();
  void run();
};


#endif
