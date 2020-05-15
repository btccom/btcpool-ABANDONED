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
#include "Watcher.h"

#include <unistd.h>
#include <arpa/inet.h>
#include <cinttypes>

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/listener.h>
#include <event2/thread.h>
#include <glog/logging.h>

#include "ssl/SSLUtils.h"

static bool tryReadLine(string &line, struct bufferevent *bufev) {
  line.clear();
  struct evbuffer *inBuf = bufferevent_get_input(bufev);

  // find eol
  struct evbuffer_ptr loc;
  loc = evbuffer_search_eol(inBuf, NULL, NULL, EVBUFFER_EOL_LF);
  if (loc.pos < 0) {
    return false; // not found
  }

  // copies and removes the first datlen bytes from the front of buf
  // into the memory at data
  line.resize(loc.pos + 1); // containing "\n"
  evbuffer_remove(inBuf, (void *)line.data(), line.size());

  return true;
}

///////////////////////////////// ClientContainer //////////////////////////////
ClientContainer::ClientContainer(const libconfig::Config &config)
  : running_(true)
  , kafkaBrokers_(config.lookup("kafka.brokers").c_str())
  , kafkaProducer_(
        kafkaBrokers_.c_str(),
        config.lookup("poolwatcher.rawgbt_topic").c_str(),
        0 /* partition */)
  , gen_{0} {
  // Enable multithreading and flag BEV_OPT_THREADSAFE.
  // Without it, bufferevent_socket_new() will return NULL with flag
  // BEV_OPT_THREADSAFE.
  evthread_use_pthreads();

  base_ = event_base_new();
  assert(base_ != nullptr);
}

ClientContainer::~ClientContainer() {
  event_base_free(base_);
}

void ClientContainer::run() {
  event_base_dispatch(base_);
}

void ClientContainer::stop() {
  if (!running_)
    return;

  LOG(INFO) << "stop event loop";
  running_ = false;
  event_base_loopexit(base_, NULL);
}

bool ClientContainer::init() {
  // check pools
  if (clients_.size() == 0) {
    LOG(ERROR) << "no avaiable pools";
    return false;
  }

  /* setup kafka */
  {
    map<string, string> options;
    // set to 1 (0 is an illegal value here), deliver msg as soon as possible.
    options["queue.buffering.max.ms"] = "1";
    if (!kafkaProducer_.setup(&options)) {
      LOG(ERROR) << "kafka producer setup failure";
      return false;
    }
    if (!kafkaProducer_.checkAlive()) {
      LOG(ERROR) << "kafka producer is NOT alive";
      return false;
    }
  }

  return initInternal();
}

// do add pools before init()
bool ClientContainer::addPools(const libconfig::Setting &config) {
  auto ptr = shared_ptr<PoolWatchClient>(createPoolWatchClient(config));
  if (!ptr->connect()) {
    return false;
  }
  clients_.push_back(ptr);

  return true;
}

void ClientContainer::removeAndCreateClient(PoolWatchClient *client) {
  for (size_t i = 0; i < clients_.size(); i++) {
    if (clients_[i].get() == client) {
      auto ptr =
          shared_ptr<PoolWatchClient>(createPoolWatchClient(client->config_));
      ptr->connect();
      LOG(INFO) << "reconnect " << ptr->poolName_;

      // set new object, the old one will be auto free
      clients_[i] = ptr;

      break;
    }
  }
}

///////////////////////////////// PoolWatchClient //////////////////////////////
PoolWatchClient::PoolWatchClient(
    struct event_base *base,
    ClientContainer *container,
    const libconfig::Setting &config)
  : enableTLS_(false)
  , container_(container)
  , config_(config)
  , poolName_(config.lookup("name").c_str())
  , poolHost_(config.lookup("host").c_str())
  , poolPort_((int)config.lookup("port"))
  , workerName_(config.lookup("worker").c_str())
  , upTime_(time(nullptr)) {
  config.lookupValue("enable_tls", enableTLS_);

  evdnsBase_ = evdns_base_new(base, EVDNS_BASE_INITIALIZE_NAMESERVERS);
  if (evdnsBase_ == nullptr) {
    LOG(FATAL) << "DNS init failed";
  }

  reconnectEvent_ = evtimer_new(base, PoolWatchClient::reconnectCallback, this);
  if (reconnectEvent_ == nullptr) {
    LOG(FATAL) << "reconnect event init failed";
  }

  if (enableTLS_) {
    LOG(INFO) << "<" << poolName_ << "> TLS enabled";

    SSL *ssl = SSL_new(get_client_SSL_CTX_With_Cache());
    if (ssl == nullptr) {
      LOG(FATAL) << "SSL init failed: " << get_ssl_err_string();
    }

    bev_ = bufferevent_openssl_socket_new(
        base,
        -1,
        ssl,
        BUFFEREVENT_SSL_CONNECTING,
        BEV_OPT_CLOSE_ON_FREE | BEV_OPT_THREADSAFE);
  } else {
    bev_ = bufferevent_socket_new(
        base, -1, BEV_OPT_CLOSE_ON_FREE | BEV_OPT_THREADSAFE);
  }

  if (bev_ == nullptr) {
    LOG(FATAL) << "bufferevent init failed";
  }

  bufferevent_setcb(
      bev_,
      PoolWatchClient::readCallback,
      NULL,
      PoolWatchClient::eventCallback,
      this);
  bufferevent_enable(bev_, EV_READ | EV_WRITE);

  state_ = INIT;

  // set read timeout
  struct timeval readtv = {120, 0};
  bufferevent_set_timeouts(bev_, &readtv, NULL);
}

PoolWatchClient::~PoolWatchClient() {
  bufferevent_free(bev_);
  event_free(reconnectEvent_);
  evdns_base_free(evdnsBase_, 0);
}

bool PoolWatchClient::connect() {
  LOG(INFO) << "Connection request to " << poolHost_ << ":" << poolPort_;

  // bufferevent_socket_connect_hostname(): This function returns 0 if the
  // connect was successfully launched, and -1 if an error occurred.
  int res = bufferevent_socket_connect_hostname(
      bev_, evdnsBase_, AF_INET, poolHost_.c_str(), (int)poolPort_);
  if (res == 0) {
    return true;
  }

  return false;
}

void PoolWatchClient::sendData(const char *data, size_t len) {
  // add data to a bufferevent’s output buffer
  bufferevent_write(bev_, data, len);
  DLOG(INFO) << "PoolWatchClient send(" << len << "): " << data;
}

void PoolWatchClient::recvData() {
  while (handleMessage()) {
  }
}

bool PoolWatchClient::handleMessage() {
  string line;
  if (tryReadLine(line, bev_)) {
    handleStratumMessage(line);

    if (state_ == TODO_RECONNECT) {
      triggerReconnect(this);
      return false;
    }

    return true;
  }

  return false;
}

// static func
void PoolWatchClient::readCallback(struct bufferevent *bev, void *ptr) {
  static_cast<PoolWatchClient *>(ptr)->recvData();
}

// static func
void PoolWatchClient::eventCallback(
    struct bufferevent *bev, short events, void *ptr) {
  PoolWatchClient *client = static_cast<PoolWatchClient *>(ptr);

  DLOG(INFO) << "PoolWatchClient::eventCallback: <" << client->poolName_ << "> "
             << events;

  if (events & BEV_EVENT_CONNECTED) {
    client->state_ = CONNECTED;
    client->onConnected();
    return;
  }

  if (events & BEV_EVENT_EOF) {
    LOG(INFO) << "upsession closed";
  } else if (events & BEV_EVENT_ERROR) {
    int dnsError = bufferevent_socket_get_dns_error(bev);
    if (dnsError) {
      LOG(ERROR) << "got a DNS error on the upsession: "
                 << evutil_gai_strerror(dnsError);
    } else {
      LOG(ERROR) << "got an error on the upsession: "
                 << evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR());
    }
  } else if (events & BEV_EVENT_TIMEOUT) {
    LOG(INFO) << "upsession read/write timeout, events: " << events;
  } else {
    LOG(ERROR) << "unhandled upsession events: " << events;
  }

  triggerReconnect(client);
}

// static fun
void PoolWatchClient::triggerReconnect(PoolWatchClient *client) {
  timeval reconnectTimeout{0, 0};
  time_t sleepTime = 10 - (time(nullptr) - client->upTime_);
  if (sleepTime > 0) {
    LOG(WARNING) << "Connection broken too fast, sleep " << sleepTime
                 << " seconds";
    reconnectTimeout.tv_sec = sleepTime;
  }
  evtimer_add(client->reconnectEvent_, &reconnectTimeout);
}

void PoolWatchClient::reconnectCallback(
    evutil_socket_t fd, short events, void *ptr) {
  PoolWatchClient *client = static_cast<PoolWatchClient *>(ptr);
  ClientContainer *container = client->container_;

  if (events & EV_TIMEOUT) {
    // update client
    container->removeAndCreateClient(client);
  } else {
    LOG(ERROR)
        << "reconnect event is not supposed to be triggered without EV_TIMEOUT";
  }
}
