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

#include <arpa/inet.h>
#include <cinttypes>

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/listener.h>
#include <event2/thread.h>
#include <glog/logging.h>


static
bool tryReadLine(string &line, struct bufferevent *bufev) {
  line.clear();
  struct evbuffer *inBuf = bufferevent_get_input(bufev);

  // find eol
  struct evbuffer_ptr loc;
  loc = evbuffer_search_eol(inBuf, NULL, NULL, EVBUFFER_EOL_LF);
  if (loc.pos < 0) {
    return false;  // not found
  }

  // copies and removes the first datlen bytes from the front of buf
  // into the memory at data
  line.resize(loc.pos + 1);  // containing "\n"
  evbuffer_remove(inBuf, (void *)line.data(), line.size());

  return true;
}

static
bool resolve(const string &host, struct	in_addr *sin_addr) {
  LOG(INFO) << "resolve " << host.c_str();
  struct evutil_addrinfo *ai = NULL;
  struct evutil_addrinfo hints_in;
  memset(&hints_in, 0, sizeof(evutil_addrinfo));
  // AF_INET, v4; AF_INT6, v6; AF_UNSPEC, both v4 & v6
  hints_in.ai_family   = AF_UNSPEC;
  hints_in.ai_socktype = SOCK_STREAM;
  hints_in.ai_protocol = IPPROTO_TCP;
  hints_in.ai_flags    = EVUTIL_AI_ADDRCONFIG;

  // TODO: use non-blocking to resolve hostname
  int err = evutil_getaddrinfo(host.c_str(), NULL, &hints_in, &ai);
  if (err != 0) {
    LOG(ERROR) << "[" << host.c_str() << "] evutil_getaddrinfo err: " << err << ", " << evutil_gai_strerror(err);
    return false;
  }
  if (ai == NULL) {
    LOG(ERROR) << "[" << host.c_str() << "] evutil_getaddrinfo res is null";
    return false;
  }

  // only get the first record, ignore ai = ai->ai_next
  if (ai->ai_family == AF_INET) {
    struct sockaddr_in *sin = (struct sockaddr_in*)ai->ai_addr;
    *sin_addr = sin->sin_addr;

    char ipStr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(sin->sin_addr), ipStr, INET_ADDRSTRLEN);
    LOG(INFO) << "resolve host: " << host << ", ip: " << ipStr;
  } else if (ai->ai_family == AF_INET6) {
    // not support yet
    LOG(ERROR) << "not support ipv6 yet";
    return false;
  }
  evutil_freeaddrinfo(ai);
  return true;
}

///////////////////////////////// ClientContainer //////////////////////////////
ClientContainer::ClientContainer(const libconfig::Config &config)
  : running_(true)
  , kafkaBrokers_(config.lookup("kafka.brokers").c_str())
  , kafkaProducer_(kafkaBrokers_.c_str(), config.lookup("poolwatcher.rawgbt_topic").c_str(), 0/* partition */)
{
  // Enable multithreading and flag BEV_OPT_THREADSAFE.
  // Without it, bufferevent_socket_new() will return NULL with flag BEV_OPT_THREADSAFE.
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
  auto ptr = createPoolWatchClient(config);
  if (!ptr->connect()) {
    return false;
  }
  clients_.push_back(ptr);

  return true;
}

void ClientContainer::removeAndCreateClient(PoolWatchClient *client) {
  for (size_t i = 0; i < clients_.size(); i++) {
    if (clients_[i] == client) {
      auto ptr = createPoolWatchClient(client->config_);
      ptr->connect();
      LOG(INFO) << "reconnect " << ptr->poolName_;

      // set new object, delete old one
      clients_[i] = ptr;
      delete client;

      break;
    }
  }
}

// static func
void ClientContainer::readCallback(struct bufferevent *bev, void *ptr) {
  static_cast<PoolWatchClient *>(ptr)->recvData();
}

// static func
void ClientContainer::eventCallback(struct bufferevent *bev,
                                    short events, void *ptr) {
  PoolWatchClient *client = static_cast<PoolWatchClient *>(ptr);
  ClientContainer *container = client->container_;

  if (events & BEV_EVENT_CONNECTED) {
    client->state_ = PoolWatchClient::State::CONNECTED;
    client->onConnected();
    return;
  }

  if (events & BEV_EVENT_EOF) {
    LOG(INFO) << "upsession closed";
  }
  else if (events & BEV_EVENT_ERROR) {
    LOG(ERROR) << "got an error on the upsession: "
    << evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR());
  }
  else if (events & BEV_EVENT_TIMEOUT) {
    LOG(INFO) << "upsession read/write timeout, events: " << events;
  }
  else {
    LOG(ERROR) << "unhandled upsession events: " << events;
  }

  // update client
  container->removeAndCreateClient(client);
}


///////////////////////////////// PoolWatchClient //////////////////////////////
PoolWatchClient::PoolWatchClient(
  struct event_base *base,
  ClientContainer *container,
  const libconfig::Setting &config
)
  : container_(container)
  , config_(config)
  , poolName_(config.lookup("name").c_str())
  , poolHost_(config.lookup("host").c_str())
  , poolPort_((int)config.lookup("port"))
  , workerName_(config.lookup("worker").c_str())
{
  bev_ = bufferevent_socket_new(base, -1, BEV_OPT_CLOSE_ON_FREE | BEV_OPT_THREADSAFE);
  assert(bev_ != nullptr);

  bufferevent_setcb(bev_,
                    ClientContainer::readCallback,  NULL,
                    ClientContainer::eventCallback, this);
  bufferevent_enable(bev_, EV_READ|EV_WRITE);


  state_ = INIT;

  // set read timeout
  struct timeval readtv = {120, 0};
  bufferevent_set_timeouts(bev_, &readtv, NULL);
}

PoolWatchClient::~PoolWatchClient() {
  bufferevent_free(bev_);
}

bool PoolWatchClient::connect() {
  struct sockaddr_in sin;
  memset(&sin, 0, sizeof(sin));
  sin.sin_family = AF_INET;
  sin.sin_port   = htons(poolPort_);
  if (!resolve(poolHost_, &sin.sin_addr)) {
    return false;
  }

  LOG(INFO) << "Connection request to " << poolHost_ << ":" << poolPort_;

  // bufferevent_socket_connect(): This function returns 0 if the connect
  // was successfully launched, and -1 if an error occurred.
  int res = bufferevent_socket_connect(bev_, (struct sockaddr *)&sin, sizeof(sin));
  if (res == 0) {
    state_ = CONNECTED;
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
    return true;
  }

  return false;
}

