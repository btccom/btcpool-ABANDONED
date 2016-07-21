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

///////////////////////////////// StratumClient ////////////////////////////////
StratumClient::StratumClient(struct event_base* base,
                             const string &workerFullName)
: workerFullName_(workerFullName), isMining_(false)
{
  bev_ = bufferevent_socket_new(base, -1, BEV_OPT_CLOSE_ON_FREE);
  inBuf_ = evbuffer_new();
  lastNoEOLPos_ = 0;

//  bufferevent_setcb(bev_,
//                    StratumClientWrapper::readCallback,
//                    StratumClientWrapper::writeCallback,
//                    StratumClientWrapper::eventCallback, this);
//  bufferevent_enable(bev_, EV_READ|EV_WRITE);
  bufferevent_setcb(bev_,
                    StratumClientWrapper::readCallback,
                    nullptr,
                    StratumClientWrapper::eventCallback, this);
  bufferevent_enable(bev_, EV_READ);

  state_ = INIT;
  latestDiff_ = 1;
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
  const size_t bufLen = evbuffer_get_length(inBuf_);
  if (bufLen == 0)
    return false;

  // try to search EOL: "\n"
  // evbuffer_search(): returns an evbuffer_ptr containing the position of the string
  struct evbuffer_ptr p;
  evbuffer_ptr_set(inBuf_, &p, lastNoEOLPos_, EVBUFFER_PTR_SET);
  p = evbuffer_search(inBuf_, "\n", 1, &p);

  // the 'pos' field of the result is -1 if the string was not found.
  // can't find EOL, ingore and return
  if (p.pos == -1) {
    lastNoEOLPos_ = bufLen - 1;  // set pos to the end of buffer
    return false;
  }

  LOG(INFO) << "p.pos: " << p.pos << ", bufLen: " << bufLen;
  // found EOL
  lastNoEOLPos_ = 0;  // reset to zero
  const size_t lineLen = p.pos + 1;  // containing "\n"

  // copies and removes the first datlen bytes from the front of buf into the memory at data
  line.resize(lineLen);
  evbuffer_remove(inBuf_, (void *)line.data(), lineLen);
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
      latestDiff_ = jparamsArr[0].uint32();
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
    extraNonce1_ = resArr[1].uint32_hex();
    DLOG(INFO) << "extraNonce1_: " << extraNonce1_;
    // ignore extraNonce2 size

    // mining.authorize
    state_ = SUBSCRIBED;
    string s = Strings::Format("{\"id\": 1, \"method\": \"mining.authorize\","
                               "\"params\": [\"\%s\", \"\"]}\n",
                               workerFullName_.c_str());
    send(s);
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

  // simulate miner
  string s;
  s = Strings::Format("{\"params\": [\"%s\",\"%s\",\"%016x\",\"%08x\",\"%08x\"]"
                      ",\"id\":4,\"method\": \"mining.submit\"}\n",
                      workerFullName_.c_str(),
                      latestJobId_.c_str(),
                      extraNonce2_++,
                      (uint32_t)time(nullptr) /* ntime */,
                      (uint32_t)time(nullptr) /* nonce */);
  send(s);
}

void StratumClient::send(const char *data, size_t len) {
  // add data to a bufferevent’s output buffer
  bufferevent_write(bev_, data, len);
  DLOG(INFO) << "send(" << len << "): " << data;
}



////////////////////////////// StratumClientWrapper ////////////////////////////
StratumClientWrapper::StratumClientWrapper(const char *host,
                                           const uint32_t port,
                                           const uint32_t numConnections,
                                           const string &userName)
: running_(true), base_(event_base_new()), numConnections_(numConnections), userName_(userName)
{
  memset(&sin_, 0, sizeof(sin_));
  sin_.sin_family = AF_INET;
  inet_pton(AF_INET, host, &(sin_.sin_addr));
  sin_.sin_port = htons(port);
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
    client->send("{\"id\": 1, \"method\": \"mining.subscribe\", \"params\": []}\n");
  }
  else if (events & BEV_EVENT_ERROR) {
    /* An error occured while connecting. */
    // TODO
    LOG(ERROR) << "event error: " << events;
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
    const string workerFullName = Strings::Format("%s.worker.%05d", userName_.c_str(), i);
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








