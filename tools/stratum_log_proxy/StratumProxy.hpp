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
#include <netinet/tcp.h>

#include <iostream>
#include <utility>
#include <mutex>
#include <set>
#include <map>
#include <vector>
#include <memory>
#include <atomic>

#include <glog/logging.h>
#include <libconfig.h++>

#include <event2/dns.h>
#include <openssl/ssl.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/listener.h>
#include <event2/event.h>
#include <event2/thread.h>
#include <event2/bufferevent_ssl.h>

#include "MySQLConnection.hpp"
#include "SSLUtils.h"
#include "StratumBase.hpp"
#include "StratumAnalyzer.hpp"

using namespace std;
using namespace libconfig;

class StratumProxy {
protected:
  struct SessionBase {
    string ip_;
    uint16_t port_ = 0;
    StratumWorker worker_;
    PoolInfo poolInfo_;

    string fieldReplace(const string &fieldTemplate) const {
      string result;
      size_t pos = 0;
      while (pos < fieldTemplate.size()) {
        size_t start = fieldTemplate.find('{', pos);
        if (start == fieldTemplate.npos) {
          result += fieldTemplate.substr(pos);
          break;
        }

        size_t end = fieldTemplate.find('}', start);
        if (end == fieldTemplate.npos) {
          result += fieldTemplate.substr(pos);
          break;
        }

        result += fieldTemplate.substr(pos, start - pos);
        result += fieldValue(fieldTemplate.substr(start + 1, end - start - 1));
        pos = end + 1;
      }
      return result;
    }

    string fieldValue(const string &fieldName) const {
      if (fieldName == "ip") {
        return ip_;
      }
      if (fieldName == "port") {
        return to_string(port_);
      }
      if (fieldName == "fullname") {
        return worker_.fullName_;
      }
      if (fieldName == "wallet") {
        return worker_.wallet_;
      }
      if (fieldName == "user") {
        return worker_.userName_;
      }
      if (fieldName == "worker") {
        return worker_.workerName_;
      }
      if (fieldName == "pwd") {
        return worker_.password_;
      }
      // Match failed, return original value
      return "{" + fieldName + "}";
    }
  };

  static void
  get_ip_port(const struct sockaddr *sa, string &ip, uint16_t &port) {
    switch (sa->sa_family) {
    case AF_INET:
      ip.resize(INET_ADDRSTRLEN);
      inet_ntop(
          AF_INET,
          &(((struct sockaddr_in *)sa)->sin_addr),
          (char *)ip.data(),
          ip.size());
      port = ((struct sockaddr_in *)sa)->sin_port;
      break;

    case AF_INET6:
      ip.resize(INET6_ADDRSTRLEN);
      inet_ntop(
          AF_INET6,
          &(((struct sockaddr_in6 *)sa)->sin6_addr),
          (char *)ip.data(),
          ip.size());
      port = ((struct sockaddr_in6 *)sa)->sin6_port;
      break;
    }

    size_t pos = ip.find('\0');
    if (pos != ip.npos) {
      ip.resize(pos);
    }
  }

  struct Session : SessionBase {
    struct evdns_base *evdnsBase_ = nullptr;
    struct bufferevent *downSession_ = nullptr;
    struct bufferevent *upSession_ = nullptr;

    bool downSessionConnected_ = false;
    bool upSessionConnected_ = false;

    enum class MinerLoginState { NONE, SUCCESS, FAILED };
    MinerLoginState minerLoginState_ = MinerLoginState::NONE;

    struct evbuffer *downBuffer_ = nullptr;
    struct evbuffer *upBuffer_ = nullptr;

    StratumProxy *server_ = nullptr;
    shared_ptr<StratumAnalyzer> analyzer_;
    const LogOptions &logOptions_;

    Session(
        struct bufferevent *down,
        struct sockaddr *saddr,
        StratumProxy *server,
        const LogOptions &logOptions)
      : downSession_(down)
      , server_(server)
      , logOptions_(logOptions) {

      get_ip_port(saddr, ip_, port_);

      downBuffer_ = evbuffer_new();
      upBuffer_ = evbuffer_new();

      evdnsBase_ =
          evdns_base_new(server->base_, EVDNS_BASE_INITIALIZE_NAMESERVERS);
      if (evdnsBase_ == nullptr) {
        LOG(FATAL) << toString() << "DNS init failed";
      }

      analyzer_ = make_shared<StratumAnalyzer>(
          ip_, port_, *server_->mysqlExecQueue_, logOptions_);
      analyzer_->setOnSubmitLogin(
          bind(&Session::onSubmitLogin, this, placeholders::_1));

      if (logOptions_.sessionEvent_)
        LOG(INFO) << toString() << "session created";
    }

    ~Session() {
      if (downBuffer_)
        evbuffer_free(downBuffer_);
      if (upBuffer_)
        evbuffer_free(upBuffer_);

      if (downSession_)
        bufferevent_free(downSession_);
      if (upSession_)
        bufferevent_free(upSession_);

      if (logOptions_.sessionEvent_)
        LOG(INFO) << toString() << "session destroyed";
    }

    string toString() const {
      string result = StringFormat("%s:%u", ip_, port_);
      if (!worker_.fullName_.empty())
        result += " / " + worker_.fullName_;

      string pool = poolInfo_.url_;
      if (!poolInfo_.user_.empty() || !poolInfo_.worker_.empty()) {
        pool += " / " + poolInfo_.user_;
        if (!poolInfo_.worker_.empty())
          pool += "." + poolInfo_.worker_;
      }

      if (!pool.empty())
        result += " -> " + pool;

      return "[" + result + "] ";
    }

    void onSubmitLogin(StratumWorker worker) {
      worker_ = worker;
      minerLoginState_ = MinerLoginState::FAILED;
      if (logOptions_.authorize_)
        LOG(INFO) << toString() << "miner login, " << worker_.toString();

      // rule matching
      bool match = false;
      for (auto rule : server_->matchRules_) {
        string field = fieldReplace(rule.field_);
        match = field == rule.value_;
        if (logOptions_.ruleMatching_)
          LOG(INFO) << toString() << "[rule matching] " << field
                    << (match ? " == " : " != ") << rule.value_;
        if (match) {
          poolInfo_ = rule.pool_;
          break;
        }
      }

      if (!match) {
        if (logOptions_.ruleMatching_)
          LOG(INFO) << toString()
                    << "[rule matching] Match failed, use default pool";
        poolInfo_ = server_->defaultPool_;
      }

      poolInfo_.url_ = fieldReplace(poolInfo_.url_);
      poolInfo_.user_ = fieldReplace(poolInfo_.user_);
      poolInfo_.pwd_ = fieldReplace(poolInfo_.pwd_);
      poolInfo_.worker_ = fieldReplace(poolInfo_.worker_);

      if (logOptions_.poolConnect_)
        LOG(INFO) << toString() << "[connect] miner: " << worker_.fullName_
                  << ", pool " << poolInfo_.toString();

      analyzer_->updateMinerInfo(poolInfo_);
      string buffer = analyzer_->getSubmitLoginLine();
      if (logOptions_.upload_)
        LOG(INFO) << toString() << toString() << "upload-replaced("
                  << buffer.size() << "): " << buffer;

      buffer += analyzer_->getUploadIncompleteLine();
      evbuffer_add(upBuffer_, buffer.data(), buffer.size());

      if (!server_->connectUpstream(this)) {
        return;
      }

      analyzer_->run();
      minerLoginState_ = MinerLoginState::SUCCESS;
    }

    static void downReadCallback(struct bufferevent *bev, void *data) {
      auto session = static_cast<Session *>(data);
      auto buffer = bufferevent_get_input(bev);
      const auto &logOptions_ = session->logOptions_;

      string content(evbuffer_get_length(buffer), 0);
      evbuffer_copyout(buffer, (char *)content.data(), content.size());
      session->analyzer_->addUploadText(content);

      if (logOptions_.upload_)
        LOG(INFO) << session->toString() << "upload(" << content.size()
                  << "): " << content;

      session->downSessionConnected_ = true;
      if (session->upSessionConnected_) {
        bufferevent_write_buffer(session->upSession_, session->upBuffer_);
        evbuffer_drain(
            session->upBuffer_, evbuffer_get_length(session->upBuffer_));

        bufferevent_write_buffer(session->upSession_, buffer);
      } else if (session->minerLoginState_ == MinerLoginState::SUCCESS) {
        evbuffer_add_buffer(session->upBuffer_, buffer);
      } else {
        session->analyzer_->runOnce();
        if (session->minerLoginState_ == MinerLoginState::FAILED) {
          session->server_->removeSession(session);
          return;
        }
      }

      evbuffer_drain(buffer, evbuffer_get_length(buffer));
    }

    static void upReadCallback(struct bufferevent *bev, void *data) {
      auto session = static_cast<Session *>(data);
      auto buffer = bufferevent_get_input(bev);
      const auto &logOptions_ = session->logOptions_;

      string content(evbuffer_get_length(buffer), 0);
      evbuffer_copyout(buffer, (char *)content.data(), content.size());
      session->analyzer_->addDownloadText(content);

      if (logOptions_.download_)
        LOG(INFO) << session->toString() << "download(" << content.size()
                  << "): " << content;

      session->upSessionConnected_ = true;
      if (session->downSessionConnected_) {
        bufferevent_write_buffer(session->downSession_, session->downBuffer_);
        evbuffer_drain(
            session->downBuffer_, evbuffer_get_length(session->downBuffer_));

        bufferevent_write_buffer(session->downSession_, buffer);
      } else {
        evbuffer_add_buffer(session->downBuffer_, buffer);
      }

      evbuffer_drain(buffer, evbuffer_get_length(buffer));
    }

    static void
    downEventCallback(struct bufferevent *bev, short events, void *data) {
      auto session = static_cast<Session *>(data);
      const auto &logOptions_ = session->logOptions_;

      if (events & BEV_EVENT_CONNECTED) {
        if (logOptions_.connect_)
          LOG(INFO) << session->toString() << "downSession connected";
        session->downSessionConnected_ = true;
        bufferevent_write_buffer(session->downSession_, session->downBuffer_);
        evbuffer_drain(
            session->downBuffer_, evbuffer_get_length(session->downBuffer_));
        return;
      }

      if (events & BEV_EVENT_EOF) {
        if (logOptions_.disconnect_)
          LOG(INFO) << session->toString() << "downSession socket closed";
      } else if (events & BEV_EVENT_ERROR) {
        if (logOptions_.disconnect_)
          LOG(INFO) << session->toString()
                    << "downSession got an error on the socket: "
                    << evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR());
      } else if (events & BEV_EVENT_TIMEOUT) {
        if (logOptions_.disconnect_)
          LOG(INFO) << session->toString()
                    << "downSession socket read/write timeout, events: "
                    << events;
      } else {
        LOG(WARNING) << session->toString()
                     << "downSession unhandled socket events: " << events;
      }

      session->downSessionConnected_ = false;
      session->server_->removeSession(session);
    }

    static void
    upEventCallback(struct bufferevent *bev, short events, void *data) {
      auto session = static_cast<Session *>(data);
      const auto &logOptions_ = session->logOptions_;

      if (events & BEV_EVENT_CONNECTED) {
        if (logOptions_.connect_)
          LOG(INFO) << session->toString() << "upSession connected";
        session->upSessionConnected_ = true;
        bufferevent_write_buffer(session->upSession_, session->upBuffer_);
        evbuffer_drain(
            session->upBuffer_, evbuffer_get_length(session->upBuffer_));
        return;
      }

      if (events & BEV_EVENT_EOF) {
        if (logOptions_.disconnect_)
          LOG(INFO) << session->toString() << "upSession socket closed";
      } else if (events & BEV_EVENT_ERROR) {
        if (logOptions_.disconnect_)
          LOG(INFO) << session->toString()
                    << "upSession got an error on the socket: "
                    << evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR());
      } else if (events & BEV_EVENT_TIMEOUT) {
        if (logOptions_.disconnect_)
          LOG(INFO) << session->toString()
                    << "upSession socket read/write timeout, events: "
                    << events;
      } else {
        LOG(WARNING) << session->toString()
                     << "upSession unhandled socket events: " << events;
      }

      session->upSessionConnected_ = false;
      session->server_->removeSession(session);
    }
  };

  const Config &config_;

  bool enableTLS_ = false;
  SSL_CTX *sslCTX_ = nullptr;
  struct sockaddr_in sinListen_;
  struct event_base *base_ = nullptr;
  struct evconnlistener *listener_ = nullptr;
  map<string, PoolInfo> pools_;
  PoolInfo defaultPool_;
  vector<MatchRule> matchRules_;

  set<Session *> sessions_;
  mutex sessionsLock_;

  MySQLExecQueue *mysqlExecQueue_ = nullptr;
  LogOptions logOptions_;

public:
  StratumProxy(const Config &config)
    : config_(config) {
    //---------------------------------
    // Log options
    // TCP connections
    config.lookupValue("log.connect", logOptions_.connect_);
    config.lookupValue("log.disconnect", logOptions_.disconnect_);
    config.lookupValue("log.upload", logOptions_.upload_);
    config.lookupValue("log.download", logOptions_.download_);
    // Stratum
    config.lookupValue("log.session_event", logOptions_.sessionEvent_);
    config.lookupValue("log.authorize", logOptions_.authorize_);
    config.lookupValue("log.job_notify", logOptions_.jobNotify_);
    config.lookupValue("log.share_submit", logOptions_.shareSubmit_);
    config.lookupValue("log.share_response", logOptions_.shareResponse_);
    // Proxy
    config.lookupValue("log.add_pool", logOptions_.addPool_);
    config.lookupValue("log.add_rule", logOptions_.addRule_);
    config.lookupValue("log.pool_connect", logOptions_.poolConnect_);
    config.lookupValue("log.rule_matching", logOptions_.ruleMatching_);
    //---------------------------------

    config.lookupValue("proxy.enable_tls", enableTLS_);
    if (enableTLS_) {
      // try get SSL CTX (load SSL cert and key)
      // any error will abort the process
      sslCTX_ = get_server_SSL_CTX(
          config.lookup("proxy.tls_cert_file").c_str(),
          config.lookup("proxy.tls_key_file").c_str());
    }

    int32_t dbPort = 3306;
    config.lookupValue("mysql.port", dbPort);
    auto dbInfo = MysqlConnectInfo(
        config.lookup("mysql.host"),
        dbPort,
        config.lookup("mysql.username"),
        config.lookup("mysql.password"),
        config.lookup("mysql.dbname"));
    mysqlExecQueue_ = new MySQLExecQueue(dbInfo);
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

    string listenIP = config_.lookup("proxy.listen_addr").c_str();
    uint16_t listenPort = (unsigned int)config_.lookup("proxy.listen_port");

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

    // ------------------- pools -------------------
    const auto &pools = config_.lookup("pools");
    for (int i = 0; i < pools.getLength(); i++) {
      string poolName = pools[i].lookup("name").operator string();
      pools_[poolName] = {poolName,
                          pools[i].lookup("url").operator string(),
                          pools[i].lookup("user").operator string(),
                          pools[i].lookup("pwd").operator string(),
                          pools[i].lookup("worker").operator string()};
    }

    for (const auto &itr : pools_) {
      if (logOptions_.addPool_)
        LOG(INFO) << "[add pool] " << itr.second.toString();
      if (itr.second.host().empty() || itr.second.port() == 0) {
        if (SessionBase().fieldReplace(itr.second.url_) == itr.second.url_) {
          LOG(ERROR) << "invalid pools url: " << itr.second.url_;
          return false;
        }
      }
    }

    string defaultPoolName = config_.lookup("match.default");
    if (pools_.find(defaultPoolName) == pools_.end()) {
      LOG(ERROR) << "default pool " << defaultPoolName << " is not exists";
      return false;
    }

    defaultPool_ = pools_[defaultPoolName];
    if (logOptions_.addPool_)
      LOG(INFO) << "[default pool] " << defaultPool_.toString();

    const auto &rules = config_.lookup("match.rules");
    for (int i = 0; i < rules.getLength(); i++) {
      string poolName = rules[i].lookup("pool").operator string();
      if (pools_.find(poolName) == pools_.end()) {
        LOG(ERROR) << "pool " << poolName << " in match rules is not exists";
        return false;
      }
      matchRules_.emplace_back(
          MatchRule{rules[i].lookup("field").operator string(),
                    rules[i].lookup("value").operator string(),
                    pools_[poolName]});
    }
    for (auto rule : matchRules_) {
      if (logOptions_.addRule_)
        LOG(INFO) << "[add rule] " << rule.toString();
    }

    return true;
  }

  void run() {
    LOG(INFO) << "proxy running";
    event_base_dispatch(base_);
  }

  void stop() {
    mysqlExecQueue_->stop();
    event_base_loopexit(base_, NULL);
    LOG(INFO) << "proxy stopped";
  }

  ~StratumProxy() {
    if (listener_ != nullptr) {
      evconnlistener_free(listener_);
    }
    if (base_ != nullptr) {
      event_base_free(base_);
    }
    for (auto session : sessions_) {
      removeSession(session);
    }
    if (mysqlExecQueue_ != nullptr) {
      delete mysqlExecQueue_;
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
    StratumProxy *server = static_cast<StratumProxy *>(data);
    struct event_base *base = server->base_;

    // Theoretically we can do it on the listener fd, but this is to make
    // sure we have the same behavior if some distro does not inherit the
    // option.
    int yes = 1;
    setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(int));

    // When we want to close the connection we want to send RST instead of FIN
    // so that miners will be disconnected immediately.
    linger lingerOn{1, 0};
    setsockopt(socket, SOL_SOCKET, SO_LINGER, &lingerOn, sizeof(struct linger));

    // ---------------------- downSession ----------------------
    struct bufferevent *downBev = nullptr;

    if (server->enableTLS_) {
      SSL *downSSL = SSL_new(server->sslCTX_);
      if (downSSL == nullptr) {
        LOG(ERROR) << "Error calling SSL_new!";
        server->stop();
        return;
      }

      downBev = bufferevent_openssl_socket_new(
          base,
          socket,
          downSSL,
          BUFFEREVENT_SSL_ACCEPTING,
          BEV_OPT_CLOSE_ON_FREE | BEV_OPT_THREADSAFE);
    } else {
      downBev = bufferevent_socket_new(
          base, socket, BEV_OPT_CLOSE_ON_FREE | BEV_OPT_THREADSAFE);
    }

    // If it was NULL with flag BEV_OPT_THREADSAFE,
    // please call evthread_use_pthreads() before you call event_base_new().
    if (downBev == nullptr) {
      LOG(ERROR) << "Error constructing bufferevent! Maybe you forgot call "
                    "evthread_use_pthreads() before event_base_new().";
      server->stop();
      return;
    }

    // ---------------------- add session ----------------------
    auto session = new Session(downBev, saddr, server, server->logOptions_);
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
  }

  bool connectUpstream(Session *session) {
    struct bufferevent *&upBev = session->upSession_;
    const PoolInfo &serverInfo = session->poolInfo_;

    upBev = nullptr;

    if (serverInfo.enableTLS()) {
      SSL *upSSL = SSL_new(get_client_SSL_CTX_With_Cache());
      if (upSSL == nullptr) {
        LOG(ERROR) << "Error calling SSL_new!";
        return false;
      }

      upBev = bufferevent_openssl_socket_new(
          base_,
          -1,
          upSSL,
          BUFFEREVENT_SSL_CONNECTING,
          BEV_OPT_CLOSE_ON_FREE | BEV_OPT_THREADSAFE);
    } else {
      upBev = bufferevent_socket_new(
          base_, -1, BEV_OPT_CLOSE_ON_FREE | BEV_OPT_THREADSAFE);
    }

    // If it was NULL with flag BEV_OPT_THREADSAFE,
    // please call evthread_use_pthreads() before you call event_base_new().
    if (upBev == nullptr) {
      LOG(ERROR) << "Error constructing bufferevent! Maybe you forgot call "
                    "evthread_use_pthreads() before event_base_new().";
      return false;
    }

    bufferevent_setcb(
        upBev,
        Session::upReadCallback,
        nullptr,
        Session::upEventCallback,
        session);
    bufferevent_enable(upBev, EV_READ | EV_WRITE);

    // connect
    int res = bufferevent_socket_connect_hostname(
        upBev,
        session->evdnsBase_,
        AF_INET,
        serverInfo.host().c_str(),
        serverInfo.port());
    if (res != 0) {
      LOG(WARNING) << session->toString() << "upSession connecting failed";
      return false;
    }

    return true;
  }
};
