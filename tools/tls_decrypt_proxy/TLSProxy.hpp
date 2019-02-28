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

#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <err.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>

#include <iostream>
#include <utility>
#include <mutex>
#include <set>
#include <memory>
#include <atomic>

#include <glog/logging.h>
#include <libconfig.h++>

#include <openssl/ssl.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/listener.h>
#include <event2/event.h>
#include <event2/thread.h>
#include <event2/bufferevent_ssl.h>

#include "SSLUtils.h"

using namespace std;
using namespace libconfig;

class TLSProxy {
protected:
  struct Session {
    struct bufferevent *downSession_ = nullptr;
    struct bufferevent *upSession_ = nullptr;

    atomic<bool> downSessionConnected;
    atomic<bool> upSessionConnected;

    struct evbuffer *downBuffer_ = nullptr;
    struct evbuffer *upBuffer_ = nullptr;

    TLSProxy *server_ = nullptr;

    Session(struct bufferevent *down, struct bufferevent *up, TLSProxy *server)
      : downSession_(down)
      , upSession_(up)
      , server_(server) {
      downSessionConnected = false;
      upSessionConnected = false;

      downBuffer_ = evbuffer_new();
      upBuffer_ = evbuffer_new();

      LOG(INFO) << "session created";
    }

    ~Session() {
      evbuffer_free(downBuffer_);
      evbuffer_free(upBuffer_);

      bufferevent_free(downSession_);
      bufferevent_free(upSession_);

      LOG(INFO) << "session destroyed";
    }

    static void downReadCallback(struct bufferevent *bev, void *data) {
      auto session = static_cast<Session *>(data);
      auto buffer = bufferevent_get_input(bev);

      string content(evbuffer_get_length(buffer), 0);
      evbuffer_copyout(buffer, (char *)content.data(), content.size());
      LOG(INFO) << "upload(" << content.size() << "): " << content;

      if (session->upSessionConnected) {
        bufferevent_write_buffer(session->upSession_, session->upBuffer_);
        evbuffer_drain(
            session->upBuffer_, evbuffer_get_length(session->upBuffer_));

        bufferevent_write_buffer(session->upSession_, buffer);
      } else {
        evbuffer_add_buffer(session->upBuffer_, buffer);
      }
    }

    static void upReadCallback(struct bufferevent *bev, void *data) {
      auto session = static_cast<Session *>(data);
      auto buffer = bufferevent_get_input(bev);

      string content(evbuffer_get_length(buffer), 0);
      evbuffer_copyout(buffer, (char *)content.data(), content.size());
      LOG(INFO) << "download(" << content.size() << "): " << content;

      if (session->downSessionConnected) {
        bufferevent_write_buffer(session->downSession_, session->downBuffer_);
        evbuffer_drain(
            session->downBuffer_, evbuffer_get_length(session->downBuffer_));

        bufferevent_write_buffer(session->downSession_, buffer);
      } else {
        evbuffer_add_buffer(session->downBuffer_, buffer);
      }
    }

    static void
    downEventCallback(struct bufferevent *bev, short events, void *data) {
      auto session = static_cast<Session *>(data);

      if (events & BEV_EVENT_CONNECTED) {
        LOG(INFO) << "downSession TLS connected";
        session->downSessionConnected = true;
        bufferevent_write_buffer(session->downSession_, session->downBuffer_);
        evbuffer_drain(
            session->downBuffer_, evbuffer_get_length(session->downBuffer_));
        return;
      }

      if (events & BEV_EVENT_EOF) {
        LOG(INFO) << "downSession socket closed";
      } else if (events & BEV_EVENT_ERROR) {
        LOG(INFO) << "downSession got an error on the socket: "
                  << evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR());
      } else if (events & BEV_EVENT_TIMEOUT) {
        LOG(INFO) << "downSession socket read/write timeout, events: "
                  << events;
      } else {
        LOG(WARNING) << "downSession unhandled socket events: " << events;
      }

      session->downSessionConnected = false;
      session->server_->removeSession(session);
    }

    static void
    upEventCallback(struct bufferevent *bev, short events, void *data) {
      auto session = static_cast<Session *>(data);

      if (events & BEV_EVENT_CONNECTED) {
        LOG(INFO) << "upSession TLS connected";
        session->upSessionConnected = true;
        bufferevent_write_buffer(session->upSession_, session->upBuffer_);
        evbuffer_drain(
            session->upBuffer_, evbuffer_get_length(session->upBuffer_));
        return;
      }

      if (events & BEV_EVENT_EOF) {
        LOG(INFO) << "upSession socket closed";
      } else if (events & BEV_EVENT_ERROR) {
        LOG(INFO) << "upSession got an error on the socket: "
                  << evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR());
      } else if (events & BEV_EVENT_TIMEOUT) {
        LOG(INFO) << "upSession socket read/write timeout, events: " << events;
      } else {
        LOG(WARNING) << "upSession unhandled socket events: " << events;
      }

      session->upSessionConnected = false;
      session->server_->removeSession(session);
    }
  };

  const Config &config_;
  bool lineBasedStream_ = true;

  SSL_CTX *sslCTX_ = nullptr;
  struct sockaddr_in sinListen_;
  struct sockaddr_in sinConnect_;
  struct event_base *base_ = nullptr;
  struct evconnlistener *listener_ = nullptr;

  set<Session *> sessions_;
  mutex sessionsLock_;

public:
  TLSProxy(const Config &config)
    : config_(config) {
    config.lookupValue("tls_proxy.line_based_stream", lineBasedStream_);

    // try get SSL CTX (load SSL cert and key)
    // any error will abort the process
    sslCTX_ = get_server_SSL_CTX(
        config.lookup("tls_proxy.tls_cert_file").c_str(),
        config.lookup("tls_proxy.tls_key_file").c_str());
  }

  bool setup() {
    // ------------------- TCP Listen -------------------

    // Enable multithreading and flag BEV_OPT_THREADSAFE.
    // Without it, bufferevent_socket_new() will return NULL with flag
    // BEV_OPT_THREADSAFE.
    evthread_use_pthreads();

    base_ = event_base_new();
    if (!base_) {
      LOG(ERROR) << "server: cannot create base";
      return false;
    }

    string listenIP = config_.lookup("tls_proxy.listen_addr").c_str();
    uint16_t listenPort = (unsigned int)config_.lookup("tls_proxy.listen_port");

    memset(&sinListen_, 0, sizeof(sinListen_));
    sinListen_.sin_family = AF_INET;
    sinListen_.sin_port = htons(listenPort);
    sinListen_.sin_addr.s_addr = htonl(INADDR_ANY);
    if (listenIP.empty() ||
        inet_pton(AF_INET, listenIP.c_str(), &sinListen_.sin_addr) == 0) {
      LOG(ERROR) << "invalid ip: " << listenIP;
      return false;
    }

    listener_ = evconnlistener_new_bind(
        base_,
        listenerCallback,
        (void *)this,
        LEV_OPT_REUSEABLE | LEV_OPT_CLOSE_ON_FREE,
        -1,
        (struct sockaddr *)&sinListen_,
        sizeof(sinListen_));
    if (!listener_) {
      LOG(ERROR) << "cannot create listener: " << listenIP << ":" << listenPort;
      return false;
    }

    // ------------------- TCP Connect -------------------
    string connectIP = config_.lookup("tls_proxy.connect_addr").c_str();
    uint16_t connectPort =
        (unsigned int)config_.lookup("tls_proxy.connect_port");

    memset(&sinConnect_, 0, sizeof(sinConnect_));
    sinConnect_.sin_family = AF_INET;
    sinConnect_.sin_port = htons(connectPort);
    sinConnect_.sin_addr.s_addr = htonl(INADDR_ANY);
    if (connectIP.empty() ||
        inet_pton(AF_INET, connectIP.c_str(), &sinConnect_.sin_addr) == 0) {
      LOG(ERROR) << "invalid ip: " << connectIP;
      return false;
    }

    return true;
  }

  void run() {
    LOG(INFO) << "proxy running";
    event_base_dispatch(base_);
  }

  void stop() {
    event_base_loopexit(base_, NULL);
    LOG(INFO) << "proxy stopped";
  }

  ~TLSProxy() {
    if (listener_ != nullptr) {
      evconnlistener_free(listener_);
    }
    if (base_ != nullptr) {
      event_base_free(base_);
    }
  }

  void removeSession(Session *session) {
    lock_guard<mutex> sl(sessionsLock_);
    sessions_.erase(session);
    delete session;
  }

  static void listenerCallback(
      struct evconnlistener *listener,
      evutil_socket_t socket,
      struct sockaddr *saddr,
      int socklen,
      void *data) {
    TLSProxy *server = static_cast<TLSProxy *>(data);
    struct event_base *base = (struct event_base *)server->base_;

    // ---------------------- downSession ----------------------
    SSL *downSSL = SSL_new(server->sslCTX_);
    if (downSSL == nullptr) {
      LOG(ERROR) << "Error calling SSL_new!";
      server->stop();
      return;
    }

    struct bufferevent *downBev = bufferevent_openssl_socket_new(
        base,
        socket,
        downSSL,
        BUFFEREVENT_SSL_ACCEPTING,
        BEV_OPT_CLOSE_ON_FREE | BEV_OPT_THREADSAFE);

    // If it was NULL with flag BEV_OPT_THREADSAFE,
    // please call evthread_use_pthreads() before you call event_base_new().
    if (downBev == nullptr) {
      LOG(ERROR) << "Error constructing bufferevent! Maybe you forgot call "
                    "evthread_use_pthreads() before event_base_new().";
      server->stop();
      return;
    }

    // ---------------------- upSession ----------------------
    SSL *upSSL = SSL_new(get_client_SSL_CTX_With_Cache());
    if (upSSL == nullptr) {
      LOG(ERROR) << "Error calling SSL_new!";
      bufferevent_free(downBev);
      server->stop();
      return;
    }

    struct bufferevent *upBev = bufferevent_openssl_socket_new(
        base,
        -1,
        upSSL,
        BUFFEREVENT_SSL_CONNECTING,
        BEV_OPT_CLOSE_ON_FREE | BEV_OPT_THREADSAFE);

    // If it was NULL with flag BEV_OPT_THREADSAFE,
    // please call evthread_use_pthreads() before you call event_base_new().
    if (upBev == nullptr) {
      LOG(ERROR) << "Error constructing bufferevent! Maybe you forgot call "
                    "evthread_use_pthreads() before event_base_new().";
      bufferevent_free(downBev);
      server->stop();
      return;
    }

    // ---------------------- add session ----------------------
    auto session = new Session(downBev, upBev, server);
    {
      lock_guard<mutex> sl(server->sessionsLock_);
      server->sessions_.insert(session);
    }

    // set callback functions
    bufferevent_setcb(
        downBev,
        Session::downReadCallback,
        nullptr,
        Session::downEventCallback,
        session);
    // By default, a newly created bufferevent has writing enabled.
    bufferevent_enable(downBev, EV_READ | EV_WRITE);

    bufferevent_setcb(
        upBev,
        Session::upReadCallback,
        nullptr,
        Session::upEventCallback,
        session);
    bufferevent_enable(upBev, EV_READ | EV_WRITE);

    // connect
    int res = bufferevent_socket_connect(
        upBev,
        (struct sockaddr *)&server->sinConnect_,
        sizeof(server->sinConnect_));
    if (res != 0) {
      LOG(WARNING) << "upSession connecting failed";
      server->removeSession(session);
    }
  }
};
