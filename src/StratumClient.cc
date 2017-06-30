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
: workerFullName_(workerFullName)
{
  inBuf_ = evbuffer_new();
  bev_ = bufferevent_socket_new(base, -1, BEV_OPT_CLOSE_ON_FREE|BEV_OPT_THREADSAFE);
  assert(bev_ != nullptr);

  bufferevent_setcb(bev_,
                    StratumClientWrapper::readCallback, nullptr,
                    StratumClientWrapper::eventCallback, this);
  bufferevent_enable(bev_, EV_READ|EV_WRITE);

  state_ = INIT;
  extraNonce2Size_ = 16;
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
    else if (jmethod.str() == "mining.set_target") {
      latestTarget_ = uint256S(jparamsArr[0].str());
      DLOG(INFO) << "latestTarget_: " << latestTarget_.ToString();
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
    // Response:
    // {"id": 1, "result": ["SESSION_ID", "NONCE_1"], "error": null}
    //
    if (jerror.type() != Utilities::JS::type::Null) {
      LOG(ERROR) << "json result is null, err: " << jerror.str();
      return;
    }
    auto resArr = jresult.array();
    if (resArr.size() != 2) {
      LOG(ERROR) << "result element's number is NOT 2: " << line;
      return;
    }

    // for zecpool:
    // SESSION_ID is null, NONCE_1 hex length is 32 bytes
    extraNonce1Str_  = resArr[1].str();
    extraNonce2Size_ = 32 - (int32_t)extraNonce1Str_.length()/2;
    DLOG(INFO) << "extraNonce1_: " << extraNonce1Str_ << ", extraNonce2Size_: " << extraNonce2Size_;

    // Request:
    // mining.authorize
    // {"id": 2, "method": "mining.authorize", "params": ["WORKER_NAME", "WORKER_PASSWORD"]}
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

  static uint64_t extraNonce2 = 0;

  string extraNonce2Str = Strings::Format("%llx", extraNonce2++);
  while (extraNonce2Str.length() < (size_t)extraNonce2Size_ * 2) {
    extraNonce2Str.append("0");
  }
  assert(extraNonce2Str.length() == (size_t)extraNonce2Size_ * 2);

  const string solutionStr = "fd40050119702e14c2c20a178f81b5b9cd40b9b33a2fc15513eb48da1e58f7933ad58482196db5558190d1d9260a8abaf624286ce5c65b313676ebc3b2bec41c1af20f255605246567bf513cc16a6dd9ffda38afb2b15504f000fb58dc9fa35490a41df9ce8af5c53cd5b58a2102da9eaa1550d575eb26da4dbe75df0c3c9d05170f9cfafe461774d5afa823a325e5a5c1081df232f11f44daa22c0a8ea501fc53acc9d019415366d492400140ac6d40c23c34727ba202f697d9c2b96fda78f55a42e6f641680c899225066370487b5ec4725ee6ff063bdb722d9eb57b333b537d24b49931dcb4de77191cd5a2de0b73bb6bdfdd74e3934f119967749eb3ac07da258b6c49c8c0c7a77122f58f63888932462b37298634d40613147faddb85513fb5f89a4d11fd815908bccd6eb1c795b89c606384836c6fd11963d0a00212b58b1a56d19fa5583665cd01405e9a30007e437303aed2d4a14df53911503442a2b354f98f536ff9d71371313637f73c59c116a3a4165b1c517758d3c0831aef2697154e3a458a1545c03cfa70b9cfbf7afbe726423b98c50d0f73ec7983bb4cbd4859feaff1efad0565132399063831b1173777ead4a7d5ffa9dbb13920e4431c284f0bf12c51e503a6b4ea165c7bfe276012eaae5dce0fb1628d45e8087e6d93ab6567fc6b4f2219b5101b5bb2cf8c5ce508ceeaf1a95a08fe299e0a4c8ae99dd5ae5dd82a8587c533d3df76495ead8f2a98a9ad9bd0cfebda90272f0cd4efce70f614133e0d15bd756524d21fb8bcbc4f63e631033c86fb173d3197ad3c72e2826b94751539e4fd798acd3218aae5159656e053116e0733f485e8d3c8eab23b65d50b2e1f665731df494b3b3016822b0e92dd1cbddf2b741223fa6e1d50a614f7cec284101fbe56fd919635e5512497517849e2fe39c7b0f2ea016f62a4fb20fbc6aa0190ef04850b38734e996391ba3a6b05c57cd080ea16781281bd31eb83f5ab64e033fa596e1d76bf0e47079900be46d8db11595070c78f0871908c72d026eb4d8316cd9fe2c6ff606db9ecf0760b92af1cbdc4ea05b0be69ea9ed24d4261a4efd55638f5de4f2f62b91cce31def5118875507d9261edc5ac9f6580fe6c3f275b13a3b9389df5da8405d822b6108da83894d9ec43031434bd121bad4fff27a590efac0e36c0f20c870596a7a0ecd3016ca92346ad93ee15aac341d65a8588875e91c4b3fc95ad04fa8e1274caf6c0f85c50772f215610abe553693d2394320da91eed1c095742f543432c18f4f8a8b3d6e1062962d4febda6d97badcf1709a2011ee756d7f8b1553224de680affa7ff6ff174f497069b20aadff77bb811cecddf222d30d372822e81c2ae6323f101de23cc6518e86c074ffcfe5dea444a9414586142913ba2a4c9b47d03d969675b4b77048c3f4255d025d891d0e351beaea9b5d8de78976f33012f7e7e528ad7fc41c4746d32a19ec7421de3ff16b449f4956a80ebecdf143e6a3ea4af2b051eca9127526796c797cfe6f61ad6fa7ecf80327eebd915aa05dfca2f6b74bc89ff2ba11c0e78dde4c0928bed4b06a1560794d5511d5b7271a995390a4def8bfc5b711bd324c85e4d901894ed23eeae26acc94fa27348ee1be5f0f3d1c89c7d65c5b1c51c4a9ce982109c86c80c9b249425d1c569002a81aecff1580a1ea0d64aed206526047f4d5404e846c9dd58fc4f0eaf387c172515b9339500480fad5d1fa3df3a6cf04faff91dd94f23b183f4ec0c2c4134c4e42d324a9d4db38db844c70ff1f09feeca3b4fc1a5b5b20a57de1aac82bfb19335483df2cc96b6d9d93ee43547643b32bdfc99507926ea7da8d0ddad2fd0f9d3dd71b9f28e2cb9584d2e2b83e4260351075b1027e5189cb0a24ac8d2fc28ba";

  // mining.submit
  // Request:
  // {"id": 4, "method": "mining.submit", "params": ["WORKER_NAME", "JOB_ID", "TIME", "NONCE_2", "EQUIHASH_SOLUTION"]}
  string s;
  const uint32_t now = (uint32_t)time(nullptr);
  s = Strings::Format("{\"params\": [\"%s\",\"%s\",\"%08x\",\"%s\",\"%s\"]"
                      ",\"id\":4,\"method\": \"mining.submit\"}\n",
                      workerFullName_.c_str(),
                      latestJobId_.c_str(),
                      SwapUint(now),
                      extraNonce2Str.c_str(), solutionStr.c_str());
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
    // mining.subscribe()
    // Request:
    // {"id": 1, "method": "mining.subscribe", "params": ["MINER_USER_AGENT", "SESSION_ID", "CONNECT_HOST", CONNECT_PORT]}
    client->sendData("{\"id\":1,\"method\":\"mining.subscribe\",\"params\":[\"__simulator__/0.1\", null, null, null]}\n");
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
    if (lastSendTime + 6 > time(nullptr)) {
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

