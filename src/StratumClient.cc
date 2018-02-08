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

#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

///////////////////////////////// StratumClient ////////////////////////////////
StratumClient::StratumClient(struct event_base* base,
                             const string &workerFullName)
: workerFullName_(workerFullName), isMining_(false)
{
  inBuf_ = evbuffer_new();
  bev_ = bufferevent_socket_new(base, -1, BEV_OPT_CLOSE_ON_FREE|BEV_OPT_THREADSAFE);
  assert(bev_ != nullptr);

  bufferevent_setcb(bev_,
                    StratumClientWrapper::readCallback, nullptr,
                    StratumClientWrapper::eventCallback, this);
  bufferevent_enable(bev_, EV_READ|EV_WRITE);

  state_ = INIT;
  latestDiff_ = 1;

  extraNonce1_ = 0u;
  extraNonce2Size_ = 8;
  extraNonce2_ = 0u;
}

StratumClient::~StratumClient() {
  evbuffer_free(inBuf_);
  bufferevent_free(bev_);
}

bool StratumClient::connect(struct sockaddr_in &sin) {
  // bufferevent_socket_connect(): This function returns 0 if the connect
  // was successfully launched, and -1 if an error occurred.
  int res = bufferevent_socket_connect(bev_, (struct sockaddr *)&sin, sizeof(sin));
  if (res == 0) {
    return true;
  }
  return false;
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
  if (loc.pos == -1) {
    return false;  // not found
  }

  // copies and removes the first datlen bytes from the front of buf
  // into the memory at data
  line.resize(loc.pos + 1);  // containing "\n"
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
  JsonNode jresult  = jnode["result"];
  JsonNode jerror   = jnode["error"];
  JsonNode jmethod  = jnode["method"];

  if (jmethod.type() == Utilities::JS::type::Str) {
    JsonNode jparams  = jnode["params"];
    auto jparamsArr = jparams.array();

    if (jmethod.str() == "mining.notify") {
      latestJobId_ = jparamsArr[0].str();
      DLOG(INFO) << "latestJobId_: " << latestJobId_;
    }
    else if (jmethod.str() == "mining.set_difficulty") {
      latestDiff_ = jparamsArr[0].uint64();
      DLOG(INFO) << "latestDiff_: " << latestDiff_;
    }
    else
    {
      LOG(ERROR) << "unknown method: " << line;
    }
    return;
  }

  if (state_ == AUTHENTICATED) {
    //
    // {"error": null, "id": 2, "result": true}
    //
    if (jerror.type()  != Utilities::JS::type::Null ||
        jresult.type() != Utilities::JS::type::Bool ||
        jresult.boolean() != true) {
//      LOG(ERROR) << "json result is null, err: " << jerror.str() << ", line: " << line;
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

    extraNonce1_     = resArr[1].uint32_hex();
    extraNonce2Size_ = resArr[2].int32();
    DLOG(INFO) << "extraNonce1_: " << extraNonce1_ << ", extraNonce2Size_: " << extraNonce2Size_;

    // mining.authorize
    state_ = SUBSCRIBED;
    string s = Strings::Format("{\"id\": 1, \"method\": \"mining.authorize\","
                               "\"params\": [\"\%s\", \"\"]}\n",
                               workerFullName_.c_str());
    sendData(s);
    return;
  }

  if (state_ == SUBSCRIBED && jresult.boolean() == true) {
    state_ = AUTHENTICATED;
    return;
  }
}

void StratumClient::submitShare() {
  if (state_ != AUTHENTICATED)
    return;

  extraNonce2_++;
  string extraNonce2Str;
  // little-endian
  Bin2Hex((uint8_t *)&extraNonce2_, extraNonce2Size_, extraNonce2Str);

  // simulate miner
  string s;
  s = Strings::Format("{\"params\": [\"%s\",\"%s\",\"%s\",\"%08x\",\"%08x\"]"
                      ",\"id\":4,\"method\": \"mining.submit\"}\n",
                      workerFullName_.c_str(),
                      latestJobId_.c_str(),
                      extraNonce2Str.c_str(),
                      (uint32_t)time(nullptr) /* ntime */,
                      (uint32_t)time(nullptr) /* nonce */);
  sendData(s);
}

void StratumClient::sendData(const char *data, size_t len) {
  // add data to a bufferevent’s output buffer
  bufferevent_lock(bev_);
  bufferevent_write(bev_, data, len);
  bufferevent_unlock(bev_);
  DLOG(INFO) << "send(" << len << "): " << data;
}



////////////////////////////// StratumClientWrapper ////////////////////////////
StratumClientWrapper::StratumClientWrapper(const char *host,
                                           const uint32_t port,
                                           const uint32_t numConnections,
                                           const string &userName,
                                           const string &minerNamePrefix)
: running_(true), base_(event_base_new()), numConnections_(numConnections),
userName_(userName), minerNamePrefix_(minerNamePrefix)
{
  memset(&sin_, 0, sizeof(sin_));
  sin_.sin_family = AF_INET;
  inet_pton(AF_INET, host, &(sin_.sin_addr));
  sin_.sin_port = htons(port);

  if (minerNamePrefix_.empty())
    minerNamePrefix_ = "simulator";
}

StratumClientWrapper::~StratumClientWrapper() {
  stop();

  if (threadSubmitShares_.joinable())
    threadSubmitShares_.join();

  for (auto &conn : connections_) {
    delete conn;
  }

  event_base_free(base_);
}

void StratumClientWrapper::stop() {
  if (!running_)
    return;

  running_ = false;
  event_base_loopexit(base_, NULL);

  LOG(INFO) << "StratumClientWrapper::stop...";
}

void StratumClientWrapper::eventCallback(struct bufferevent *bev,
                                         short events, void *ptr) {
  StratumClient *client = static_cast<StratumClient *>(ptr);

  if (events & BEV_EVENT_CONNECTED) {
    client->state_ = StratumClient::State::CONNECTED;
    // subscribe
    client->sendData("{\"id\":1,\"method\":\"mining.subscribe\",\"params\":[\"__simulator__/0.1\"]}\n");
  }
  else if (events & BEV_EVENT_ERROR) {
    /* An error occured while connecting. */
    // TODO
    LOG(ERROR) << "event error: " << evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR());
  }
}

void StratumClientWrapper::readCallback(struct bufferevent* bev, void *connection) {
  StratumClient *client = static_cast<StratumClient *>(connection);
  client->readBuf(bufferevent_get_input(bev));
}

void StratumClientWrapper::run() {
  //
  // create clients
  //
  for (size_t i = 0; i < numConnections_; i++) {
    const string workerFullName = Strings::Format("%s.%s-%05d",
                                                  userName_.c_str(),
                                                  minerNamePrefix_.c_str(),
                                                  i);
    StratumClient *client = new StratumClient(base_, workerFullName);

    if (!client->connect(sin_)) {
      LOG(ERROR) << "client connnect failure: " << workerFullName;
      return;
    }
    connections_.insert(client);
  }

  threadSubmitShares_ = thread(&StratumClientWrapper::runThreadSubmitShares, this);

  // event loop
  event_base_dispatch(base_);

  LOG(INFO) << "StratumClientWrapper::run() stop";
}

void StratumClientWrapper::runThreadSubmitShares() {
  time_t lastSendTime = 0;

  while (running_) {
    if (lastSendTime + 10 > time(nullptr)) {
      sleep(1);
      continue;
    }

    submitShares();
    lastSendTime = time(nullptr);
  }
}

void StratumClientWrapper::submitShares() {
  for (auto &conn : connections_) {
    conn->submitShare();
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
  buf.resize(4096);  // we assume 4096 is big enough

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
  if (loc.pos == -1) {
    return;  // not found
  }

  // copies and removes the first datlen bytes from the front of buf
  // into the memory at data
  line.resize(loc.pos + 1);  // containing "\n"
  evbuffer_remove(inBuf_, (void *)line.data(), line.size());

  LOG(INFO) << "line: " << line;
}

