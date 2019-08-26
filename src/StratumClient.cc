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
#include "StratumClient.h"
#include "Utils.h"
#include "ssl/SSLUtils.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>
#include <event2/bufferevent_ssl.h>

#include <random>

static map<string, StratumClient::Factory> gStratumClientFactories;
bool StratumClient::registerFactory(const string &chainType, Factory factory) {
  return gStratumClientFactories.emplace(chainType, move(factory)).second;
}

///////////////////////////////// StratumClient ////////////////////////////////
StratumClient::StratumClient(
    bool enableTLS,
    struct event_base *base,
    const string &workerFullName,
    const string &workerPasswd,
    const libconfig::Config &config)
  : enableTLS_(enableTLS)
  , workerFullName_(workerFullName)
  , workerPasswd_(workerPasswd)
  , sharesPerTx_(0)
  , isMining_(false) {
  inBuf_ = evbuffer_new();

  evdnsBase_ = evdns_base_new(base, EVDNS_BASE_INITIALIZE_NAMESERVERS);
  if (evdnsBase_ == nullptr) {
    LOG(FATAL) << "DNS init failed";
  }

  if (enableTLS_) {
    LOG(INFO) << "<" << workerFullName_ << "> TLS enabled";

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
  assert(bev_ != nullptr);

  bufferevent_setcb(
      bev_,
      StratumClientWrapper::readCallback,
      nullptr,
      StratumClientWrapper::eventCallback,
      this);
  bufferevent_enable(bev_, EV_READ | EV_WRITE);

  state_ = INIT;
  latestDiff_ = 1;

  sessionId_ = 0u;
  extraNonce2Size_ = 8;

  config.lookupValue("simulator.shares_per_tx", sharesPerTx_);
  if (sharesPerTx_ == 0) {
    sharesPerTx_ = 1;
  }

  // use random extraNonce2_
  // It will help Ethereum and other getwork chains to avoid duplicate shares
  static std::random_device rd;
  static std::mt19937 gen(rd());
  static std::uniform_int_distribution<uint64_t> dis(0, 0xFFFFFFFFFFFFFFFFu);
  extraNonce2_ = dis(gen);
}

StratumClient::~StratumClient() {
  evbuffer_free(inBuf_);
  bufferevent_free(bev_);
  evdns_base_free(evdnsBase_, 0);
}

bool StratumClient::connect(const string &host, uint16_t port) {
  LOG(INFO) << "Connection request to " << host << ":" << port;

  // bufferevent_socket_connect_hostname(): This function returns 0 if the
  // connect was successfully launched, and -1 if an error occurred.
  int res = bufferevent_socket_connect_hostname(
      bev_, evdnsBase_, AF_INET, host.c_str(), (int)port);
  if (res == 0) {
    return true;
  }

  return false;
}

void StratumClient::sendHelloData() {
  sendData(
      "{\"id\":1,\"method\":\"mining.subscribe\",\"params\":[\"__simulator__/"
      "0.1\"]}\n");
}

void StratumClient::readBuf(struct evbuffer *buf) {
  // moves all data from src to the end of dst
  evbuffer_add_buffer(inBuf_, buf);

  string line;
  while (tryReadLine(line)) {
    handleLine(line);
  }
}

bool StratumClient::tryReadLine(string &line) {
  line.clear();

  // find eol
  struct evbuffer_ptr loc;
  loc = evbuffer_search_eol(inBuf_, nullptr, nullptr, EVBUFFER_EOL_LF);
  if (loc.pos < 0) {
    return false; // not found
  }

  // copies and removes the first datlen bytes from the front of buf
  // into the memory at data
  line.resize(loc.pos + 1); // containing "\n"
  evbuffer_remove(inBuf_, (void *)line.data(), line.size());
  return true;
}

void StratumClient::handleLine(const string &line) {
  DLOG(INFO) << "recv(" << line.size() << "): " << line;

  JsonNode jnode;
  if (!JsonNode::parse(line.data(), line.data() + line.size(), jnode)) {
    LOG(ERROR) << "decode line fail, not a json string";
    return;
  }
  JsonNode jresult = jnode["result"];
  JsonNode jerror = jnode["error"];
  JsonNode jmethod = jnode["method"];

  if (jmethod.type() == Utilities::JS::type::Str) {
    JsonNode jparams = jnode["params"];
    auto jparamsArr = jparams.array();

    if (jmethod.str() == "mining.notify") {
      latestJobId_ = jparamsArr[0].str();
      DLOG(INFO) << "latestJobId_: " << latestJobId_;
    } else if (jmethod.str() == "mining.set_difficulty") {
      latestDiff_ = jparamsArr[0].uint64();
      DLOG(INFO) << "latestDiff_: " << latestDiff_;
    } else {
      LOG(ERROR) << "unknown method: " << line;
    }
    return;
  }

  if (state_ == AUTHENTICATED) {
    //
    // {"error": null, "id": 2, "result": true}
    //
    if (jerror.type() != Utilities::JS::type::Null ||
        jresult.type() != Utilities::JS::type::Bool ||
        jresult.boolean() != true) {
      //      LOG(ERROR) << "json result is null, err: " << jerror.str() << ",
      //      line: " << line;
    }
    return;
  }

  if (state_ == CONNECTED) {
    //
    // {"id":1,"result":[[["mining.set_difficulty","01000002"],
    //                    ["mining.notify","01000002"]],"01000002",8],"error":null}
    //
    if (jerror.type() != Utilities::JS::type::Null) {
      LOG(ERROR) << "json result is null, err: " << jerror.str();
      return;
    }
    auto resArr = jresult.array();
    if (resArr.size() != 3) {
      LOG(ERROR) << "result element's number is NOT 3: " << line;
      return;
    }

    sessionId_ = resArr[1].uint32_hex();
    extraNonce2Size_ = resArr[2].int32();
    DLOG(INFO) << "sessionId_: " << sessionId_
               << ", extraNonce2Size_: " << extraNonce2Size_;

    // mining.authorize
    state_ = SUBSCRIBED;
    string s = Strings::Format(
        "{\"id\": 1, \"method\": \"mining.authorize\","
        "\"params\": [\"%s\", \"%s\"]}\n",
        workerFullName_,
        workerPasswd_);
    sendData(s);
    return;
  }

  if (state_ == SUBSCRIBED && jresult.boolean() == true) {
    state_ = AUTHENTICATED;
    return;
  }
}

string StratumClient::constructShare() {
  extraNonce2_++;
  string extraNonce2Str;
  // little-endian
  Bin2Hex((uint8_t *)&extraNonce2_, extraNonce2Size_, extraNonce2Str);

  // simulate miner
  string s = Strings::Format(
      "{\"params\": [\"%s\",\"%s\",\"%s\",\"%08x\",\"%08x\"]"
      ",\"id\":4,\"method\": \"mining.submit\"}\n",
      workerFullName_,
      latestJobId_,
      extraNonce2Str,
      (uint32_t)time(nullptr) /* ntime */,
      (uint32_t)time(nullptr) /* nonce */);
  return s;
}

void StratumClient::submitShare() {
  if (state_ != AUTHENTICATED)
    return;

  for (uint32_t i = 0; i < sharesPerTx_; ++i) {
    sendData(constructShare());
  }
}

void StratumClient::sendData(const char *data, size_t len) {
  // add data to a bufferevent’s output buffer
  bufferevent_write(bev_, data, len);
  DLOG(INFO) << "send(" << len << "): " << data;
}

////////////////////////////// StratumClientWrapper ////////////////////////////
StratumClientWrapper::StratumClientWrapper(
    bool enableTLS,
    const string &host,
    const uint32_t port,
    const uint32_t numConnections,
    const string &userName,
    const string &minerNamePrefix,
    const string &passwd,
    const string &type,
    const libconfig::Config &config)
  : running_(true)
  , enableTLS_(enableTLS)
  , host_(host)
  , port_(port)
  , base_(event_base_new())
  , numConnections_(numConnections)
  , userName_(userName)
  , minerNamePrefix_(minerNamePrefix)
  , passwd_(passwd)
  , type_(type)
  , config_(config) {

  if (minerNamePrefix_.empty())
    minerNamePrefix_ = "simulator";
}

StratumClientWrapper::~StratumClientWrapper() {
  stop();

  event_free(sigint_);
  event_free(sigterm_);
  event_free(timer_);

  // It has to be cleared here to free client events before event base
  connections_.clear();

  event_base_free(base_);
}

void StratumClientWrapper::stop() {
  if (!running_)
    return;

  running_ = false;
  event_base_loopexit(base_, NULL);

  LOG(INFO) << "StratumClientWrapper::stop...";
}

void StratumClientWrapper::eventCallback(
    struct bufferevent *bev, short events, void *ptr) {
  StratumClient *client = static_cast<StratumClient *>(ptr);

  if (events & BEV_EVENT_CONNECTED) {
    client->state_ = StratumClient::State::CONNECTED;
    // subscribe
    client->sendHelloData();
  } else if (events & BEV_EVENT_ERROR) {
    /* An error occured while connecting. */
    // TODO
    LOG(ERROR) << "event error: "
               << evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR());
  }
}

void StratumClientWrapper::readCallback(
    struct bufferevent *bev, void *connection) {
  StratumClient *client = static_cast<StratumClient *>(connection);
  client->readBuf(bufferevent_get_input(bev));
}

void StratumClientWrapper::timerCallback(
    evutil_socket_t fd, short event, void *ptr) {
  auto wrapper = static_cast<StratumClientWrapper *>(ptr);
  wrapper->submitShares();
}

void StratumClientWrapper::signalCallback(
    evutil_socket_t fd, short event, void *ptr) {
  auto wrapper = static_cast<StratumClientWrapper *>(ptr);
  wrapper->stop();
}

void StratumClientWrapper::run() {
  //
  // create clients
  //
  for (size_t i = 0; i < numConnections_; i++) {
    const string workerFullName =
        Strings::Format("%s.%s-%05d", userName_, minerNamePrefix_, i);
    auto client = createClient(enableTLS_, base_, workerFullName, passwd_);

    if (!client->connect(host_, port_)) {
      LOG(ERROR) << "client connnect failure: " << workerFullName;
      return;
    }
    connections_.push_back(move(client));
  }

  // create timer
  timer_ = event_new(
      base_, -1, EV_PERSIST, StratumClientWrapper::timerCallback, this);
  // Submit a share every 15 seconds (in probability) for each connection.
  // After the timer is triggered, a connection will be randomly selected to
  // submit a share.
  int sleepTime = 15000000 / connections_.size();
  struct timeval interval {
    sleepTime / 1000000, sleepTime % 1000000
  };
  event_add(timer_, &interval);

  // create signals
  sigterm_ = event_new(
      base_,
      SIGTERM,
      EV_SIGNAL | EV_PERSIST,
      StratumClientWrapper::signalCallback,
      this);
  event_add(sigterm_, nullptr);
  sigint_ = event_new(
      base_,
      SIGINT,
      EV_SIGNAL | EV_PERSIST,
      StratumClientWrapper::signalCallback,
      this);
  event_add(sigint_, nullptr);

  // event loop
  event_base_dispatch(base_);

  LOG(INFO) << "StratumClientWrapper::run() stop";
}

void StratumClientWrapper::submitShares() {
  // randomly select a connection to submit a share.
  static std::random_device rd;
  static std::mt19937 gen(rd());
  static std::uniform_int_distribution<size_t> dis(0, connections_.size() - 1);

  size_t i = dis(gen);
  connections_[i]->submitShare();
}

unique_ptr<StratumClient> StratumClientWrapper::createClient(
    bool enableTLS,
    struct event_base *base,
    const string &workerFullName,
    const string &workerPasswd) {
  auto iter = gStratumClientFactories.find(type_);
  if (iter != gStratumClientFactories.end() && iter->second) {
    return iter->second(enableTLS, base, workerFullName, workerPasswd, config_);
  } else {
    return nullptr;
  }
}

//////////////////////////////// TCPClientWrapper //////////////////////////////
TCPClientWrapper::TCPClientWrapper() {
  sockfd_ = socket(AF_INET, SOCK_STREAM, 0);
  assert(sockfd_ >= 0);

  inBuf_ = evbuffer_new();
}

TCPClientWrapper::~TCPClientWrapper() {
  evbuffer_free(inBuf_);
  close(sockfd_);
}

bool TCPClientWrapper::connect(const char *host, const int port) {
  struct sockaddr_in sin;
  memset(&sin, 0, sizeof(sin));

  sin.sin_family = AF_INET;
  inet_pton(AF_INET, host, &(sin.sin_addr));
  sin.sin_port = htons(port);

  if (::connect(sockfd_, (struct sockaddr *)&sin, sizeof(sin)) == 0) {
    return true;
  }

  LOG(ERROR) << "connect fail: " << strerror(errno);
  return false;
}

void TCPClientWrapper::send(const char *data, const size_t len) {
  ::send(sockfd_, data, len, 0);
  //  DLOG(INFO) << "send: " << data;
}

void TCPClientWrapper::recv() {
  string buf;
  buf.resize(4096); // we assume 4096 is big enough

  ssize_t bytes = ::recv(sockfd_, (void *)buf.data(), buf.size(), 0);
  if (bytes == -1) {
    LOG(ERROR) << "recv fail: " << strerror(errno);
    return;
  }
  if (bytes == 0) {
    return;
  }
  buf.resize(bytes);

  // put data to evbuffer
  evbuffer_add(inBuf_, buf.data(), buf.size());

  //  DLOG(INFO) << "recv: " << buf;
}

void TCPClientWrapper::getLine(string &line) {
  line.clear();
  if (evbuffer_get_length(inBuf_) == 0)
    recv();

  // find eol
  struct evbuffer_ptr loc;
  loc = evbuffer_search_eol(inBuf_, nullptr, nullptr, EVBUFFER_EOL_LF);
  if (loc.pos < 0) {
    return; // not found
  }

  // copies and removes the first datlen bytes from the front of buf
  // into the memory at data
  line.resize(loc.pos + 1); // containing "\n"
  evbuffer_remove(inBuf_, (void *)line.data(), line.size());

  LOG(INFO) << "line: " << line;
}
