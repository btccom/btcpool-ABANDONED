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
template<class T>
class Server {
  struct sockaddr_in sin_;
  struct event_base* base_;
  struct event* signal_event_;
  struct evconnlistener* listener_;

  map<evutil_socket_t, T*> connections_;

public:
  Server();
  ~Server();

  bool setup(const char *ip, const unsigned short port);
  void run();
  void stop();

  void sendToAllClients(const char* data, size_t len);
  void addConnection(evutil_socket_t fd, T* connection);
  void removeConnection(evutil_socket_t fd);

  static void listenerCallback(
                               struct evconnlistener* listener
                               ,evutil_socket_t socket
                               ,struct sockaddr* saddr
                               ,int socklen
                               ,void* server
                               );

//  static void signalCallback(evutil_socket_t sig, short events, void* server);
  static void writeCallback(struct bufferevent*, void* server);
  static void readCallback(struct bufferevent*, void* connection);
  static void eventCallback(struct bufferevent*, short, void* server);
};

//---------------------

template<class T>
Server<T>::Server(): base_(NULL), listener_(NULL), signal_event_(NULL)
{
}

template<class T>
Server<T>::~Server() {
  if(signal_event_ != NULL) {
    event_free(signal_event_);
  }
  if(listener_ != NULL) {
    evconnlistener_free(listener_);
  }
  if(base_ != NULL) {
    event_base_free(base_);
  }
}

template<class T>
bool Server<T>::setup(const char *ip, const unsigned short port) {

  base_ = event_base_new();
  if(!base_) {
    LOG(ERROR) << "server: cannot create base";
    return false;
  }

  memset(&sin_, 0, sizeof(sin_));
  sin_.sin_family = AF_INET;
  sin_.sin_port   = htons(port);
  sin_.sin_addr.s_addr = htonl(INADDR_ANY);
  if (ip && inet_pton(AF_INET, ip, &sin_.sin_addr) == 0) {
    LOG(ERROR) << "invalid ip: " << ip;
    return false;
  }

  listener_ = evconnlistener_new_bind(base_,
                                     Server::listenerCallback,
                                     (void*)this,
                                     LEV_OPT_REUSEABLE|LEV_OPT_CLOSE_ON_FREE,
                                     -1, (struct sockaddr*)&sin_, sizeof(sin_));
  if(!listener_) {
    LOG(ERROR) << "cannot create listener: " << ip << ":" << port;
    return false;
  }

//  signal_event_ = evsignal_new(base_, SIGINT, signalCallback, (void*)this);
//  if(!signal_event_ || event_add(signal_event_, NULL) < 0) {
//    LOG(ERROR) << "cannot create signal event";
//    return false;
//  }
  return true;
}

template<class T>
void Server<T>::run() {
  if(base_ != NULL) {
//    event_base_loop(base_, EVLOOP_NONBLOCK);
    event_base_dispatch(base_);
  }
}

template<class T>
void Server<T>::addConnection(evutil_socket_t fd, T* connection) {
  connections_.insert(std::pair<evutil_socket_t, T*>(fd, connection));
}

template<class T>
void Server<T>::removeConnection(evutil_socket_t fd) {
  connections_.erase(fd);
}

template<class T>
void Server<T>::sendToAllClients(const char* data, size_t len) {
  typename map<evutil_socket_t, T*>::iterator it = connections_.begin();
  while(it != connections_.end()) {
    it->second->send(data, len);
    ++it;
  }
}

template<class T>
void Server<T>::listenerCallback(struct evconnlistener* listener,
                                        evutil_socket_t fd,
                                        struct sockaddr* saddr,
                                        int socklen, void* data)
{
  Server<T>* server = static_cast<Server<T>* >(data);
  struct event_base* base = (struct event_base*)server->base_;
  struct bufferevent* bev;

  bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);
  if(!bev) {
    event_base_loopbreak(base);
    LOG(ERROR) << "error constructing bufferevent!";
    return;
  }

  T* conn = new T(fd, bev, (void*)server);
  server->addConnection(fd, conn);

  bufferevent_setcb(bev,
                    Server::readCallback,
                    Server::writeCallback,
                    Server::eventCallback, (void*)conn);
  bufferevent_enable(bev, EV_WRITE);
  bufferevent_enable(bev, EV_READ);
}

template<class T>
void Server<T>::stop() {
  LOG(INFO) << "stop tcp server event loop";
  event_base_loopexit(base_, NULL);
}

//template<class T>
//void Server<T>::signalCallback(evutil_socket_t sig, short events, void* data) {
//  Server<T>* server = static_cast<Server<T> *>(data);
//  struct event_base* base = server->base;
//  struct timeval delay = {2,0};
//  LOG(ERROR) << "caught an interrupt signal; exiting cleanly in two seconds";
//  event_base_loopexit(base, &delay);
//}

template<class T>
void Server<T>::writeCallback(struct bufferevent* bev, void* data) {
  struct evbuffer* output = bufferevent_get_output(bev);
  if(evbuffer_get_length(output) == 0) {
    return;
  }
//  printf("write callback.\n");
}

template<class T>
void Server<T>::readCallback(struct bufferevent* bev, void* connection) {
  T* conn = static_cast<T*>(connection);
  struct evbuffer* buf = bufferevent_get_input(bev);
  const size_t bufLen = evbuffer_get_length(buf);

  // try to search end of line: "\n"
  // evbuffer_search(): returns an evbuffer_ptr containing the position of the string
  struct evbuffer_ptr p;
  if (conn->lastNoEOLPos_) {
    evbuffer_ptr_set(buf, &p, conn->lastNoEOLPos_, EVBUFFER_PTR_SET);
    p = evbuffer_search(buf, "\n", 1, &p);
  } else {
    p = evbuffer_search(buf, "\n", 1, NULL);
  }

  // The 'pos' field of the result is -1 if the string was not found.
  if (p.pos == -1) {
    conn->lastNoEOLPos_ = bufLen;
    // can't find EOL, ingore and return
    return;
  }
  LOG(INFO) << "p.pos: " << p.pos << ", bufLen: " << bufLen;

  // found EOL
  conn->lastNoEOLPos_ = 0;
  const size_t lineLen = p.pos + 1;  // containing "\n"

  string readbuf;
  readbuf.resize(lineLen);
  // copies and removes the first datlen bytes from the front of buf into the memory at data
  evbuffer_remove(buf, (void *)readbuf.data(), lineLen);
  
  conn->readLine(readbuf.data(), readbuf.size());
}

template<class T>
void Server<T>::eventCallback(struct bufferevent* bev, short events, void* data) {
  T* conn = static_cast<T*>(data);
  Server<T>* server = static_cast<Server<T>* >(conn->server_);
  
  if (events & BEV_EVENT_EOF) {
    server->removeConnection(conn->fd_);
    bufferevent_free(bev);
  }
  else if (events & BEV_EVENT_ERROR) {
    LOG(ERROR) << "got an error on the connection: " << strerror(errno);
  }
  else {
    LOG(ERROR) << "unhandled";
  }
}




////////////////////////////////// StratumServer ///////////////////////////////
class StratumServer {
  atomic<bool> running_;
  Server<StratumSession> server_;

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
