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

#include <cinttypes>

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/listener.h>
#include <glog/logging.h>

static
bool tryReadLine(string &line, struct bufferevent *bufev) {
  line.clear();
  struct evbuffer *inBuf = bufferevent_get_input(bufev);

  // find eol
  struct evbuffer_ptr loc;
  loc = evbuffer_search_eol(inBuf, NULL, NULL, EVBUFFER_EOL_LF);
  if (loc.pos == -1) {
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
    LOG(ERROR) << "evutil_getaddrinfo err: " << err << ", " << evutil_gai_strerror(err);
    return false;
  }
  if (ai == NULL) {
    LOG(ERROR) << "evutil_getaddrinfo res is null";
    return false;
  }

  // only get the first record, ignore ai = ai->ai_next
  if (ai->ai_family == AF_INET) {
    struct sockaddr_in *sin = (struct sockaddr_in*)ai->ai_addr;
    *sin_addr = sin->sin_addr;
  } else if (ai->ai_family == AF_INET6) {
    // not support yet
    LOG(ERROR) << "not support ipv6 yet";
    return false;
  }
  evutil_freeaddrinfo(ai);
  return true;
}

static
int32_t getBlockHeightFromCoinbase(const string &coinbase1) {
  // https://github.com/bitcoin/bips/blob/master/bip-0034.mediawiki
  const string a = coinbase1.substr(86, 2);
  const string b = coinbase1.substr(88, 2);
  const string c = coinbase1.substr(90, 2);
  const string heightHex = c + b + a;  // little-endian

  return (int32_t)strtol(heightHex.c_str(), nullptr, 16);
}


//
// input  : 89c2f63dfb970e5638aa66ae3b7404a8a9914ad80328e9fe0000000000000000
// output : 00000000000000000328e9fea9914ad83b7404a838aa66aefb970e5689c2f63d
static
string convertPrevHash(const string &prevHash) {
  assert(prevHash.length() == 64);
  string hash;
  for (int i = 7; i >= 0; i--) {
    uint32_t v = (uint32_t)strtoul(prevHash.substr(i*8, 8).c_str(), nullptr, 16);
    hash.append(Strings::Format("%08x", v));
  }
  return hash;
}



///////////////////////////////// ClientContainer //////////////////////////////
ClientContainer::ClientContainer(const string &kafkaBrokers, const string &payoutAddr)
:running_(true), kafkaBrokers_(kafkaBrokers),
kafkaProducer_(kafkaBrokers_.c_str(), KAFKA_TOPIC_STRATUM_JOB, RD_KAFKA_PARTITION_UA/* partition */),
poolPayoutAddr_(payoutAddr)
{
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
  // read pools from DB
  const string sql = "SELECT `pool_name`,`pool_host`,`pool_port`,`worker_name` FROM `pools`";

  char **row;
  MySQLResult res;
  db_.query(sql, res);

  while ((row = res.nextRow()) != nullptr) {
    auto ptr = new PoolWatchClient(base_, this,
                                   // pool_name, pool_host
                                   string(row[0]), string(row[1]),
                                   // pool_port, worker_name
                                   (int16_t)atoi(row[2]), string(row[3]));
    ptr->connect();
    clients_.push_back(ptr);

    LOG(INFO) << "watch pool: " << row[0] << ", " << row[1] << ":" << row[2] << ", " << row[3];
  }

  if (clients_.size() == 0) {
    LOG(ERROR) << "no avaiable pools";
    return false;
  }

  return true;
}

//bool ClientContainer::insertBlockInfoToDB(const string &poolName,
//                                          uint64_t jobRecvTimeMs,
//                                          int32_t blockHeight,
//                                          const string &blockPrevHash,
//                                          uint32_t blockTime) {
//  string recvTime = date("%F %T", jobRecvTimeMs/1000);
//  recvTime.append(Strings::Format(".%03d", jobRecvTimeMs % 1000));
//
//  string sql = Strings::Format("INSERT INTO `pool_block_notify` "
//                               "(`pool_name`, `job_recv_time`, `block_height`,"
//                               " `block_prev_hash`, `block_time`, `created_at`)"
//                               " VALUES (\"%s\",\"%s\",%d,\"%s\",\"%s\",\"%s\")",
//                               poolName.c_str(), recvTime.c_str(), blockHeight,
//                               blockPrevHash.c_str(),
//                               date("%F %T", blockTime).c_str(),
//                               date("%F %T", time(nullptr)).c_str());
//  return db_.execute(sql);
//}

bool ClientContainer::makeEmptyMiningNotify(const string &poolName,
                                            uint64_t jobRecvTimeMs,
                                            int32_t blockHeight,
                                            const string &blockPrevHash,
                                            uint32_t blockTime) {

}

void ClientContainer::removeAndCreateClient(PoolWatchClient *client) {
  for (size_t i = 0; i < clients_.size(); i++) {
    if (clients_[i] == client) {
      auto ptr = new PoolWatchClient(base_, this,
                                     client->poolName_, client->poolHost_,
                                     client->poolPort_, client->workerName_);
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

    // do subscribe
    string s = Strings::Format("{\"id\":1,\"method\":\"mining.subscribe\""
                               ",\"params\":[\"%s\"]}\n", BTCCOM_WATCHER_AGENT);
    client->sendData(s);
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
PoolWatchClient::PoolWatchClient(struct event_base *base, ClientContainer *container,
                                 const string &poolName,
                                 const string &poolHost, const int16_t poolPort,
                                 const string &workerName)
: container_(container), poolName_(poolName), poolHost_(poolHost),
poolPort_(poolPort), workerName_(workerName)
{
  bev_ = bufferevent_socket_new(base, -1, BEV_OPT_CLOSE_ON_FREE);
  assert(bev_ != nullptr);

  bufferevent_setcb(bev_,
                    ClientContainer::readCallback,  NULL,
                    ClientContainer::eventCallback, this);
  bufferevent_enable(bev_, EV_READ|EV_WRITE);

  extraNonce1_ = 0;
  extraNonce2Size_ = 0;

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

void PoolWatchClient::handleStratumMessage(const string &line) {
  DLOG(INFO) << poolName_ << " UpPoolWatchClient recv(" << line.size() << "): " << line;

  JsonNode jnode;
  if (!JsonNode::parse(line.data(), line.data() + line.size(), jnode)) {
    LOG(ERROR) << "decode line fail, not a json string";
    return;
  }
  JsonNode jresult = jnode["result"];
  JsonNode jerror  = jnode["error"];
  JsonNode jmethod = jnode["method"];

  if (jmethod.type() == Utilities::JS::type::Str) {
    JsonNode jparams  = jnode["params"];
    auto jparamsArr = jparams.array();

    if (jmethod.str() == "mining.notify") {
      const string prevHash = convertPrevHash(jparamsArr[1].str());

      if (lastPrevBlockHash_.empty()) {
        lastPrevBlockHash_ = prevHash;  // first set prev block hash
      }

      // stratum job prev block hash changed
      if (lastPrevBlockHash_ != prevHash) {
        const int32_t  blockHeight = getBlockHeightFromCoinbase(jparamsArr[2].str());
        const uint32_t blockTime   = jparamsArr[7].uint32_hex();

        struct timeval tv;
        gettimeofday(&tv, nullptr);
        container_->insertBlockInfoToDB(poolName_,
                                        (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000,
                                        blockHeight,
                                        prevHash, blockTime);
        lastPrevBlockHash_ = prevHash;
        LOG(INFO) << poolName_ << " prev block changed, height: " << blockHeight
        << ", prev_hash: " << prevHash;
      }
    }
    else {
      // ignore other messages
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
      LOG(ERROR) << poolName_ <<  " auth fail";
    }
    return;
  }

  if (state_ == CONNECTED) {
    //
    // {"id":1,"result":[[["mining.set_difficulty","01000002"],
    //                    ["mining.notify","01000002"]],"01000002",8],"error":null}
    //
    if (jerror.type() != Utilities::JS::type::Null) {
      LOG(ERROR) << poolName_ << " json result is null, err: " << jerror.str();
      return;
    }
    std::vector<JsonNode> resArr = jresult.array();
    if (resArr.size() < 3) {
      LOG(ERROR) << poolName_ << " result element's number is less than 3: " << line;
      return;
    }

    extraNonce1_     = resArr[1].uint32_hex();
    extraNonce2Size_ = resArr[2].uint32();
    LOG(INFO) << poolName_ << " extraNonce1: " << extraNonce1_
    << ", extraNonce2 Size: " << extraNonce2Size_;

    // subscribe successful
    state_ = SUBSCRIBED;

    // do mining.authorize
    string s = Strings::Format("{\"id\": 1, \"method\": \"mining.authorize\","
                               "\"params\": [\"%s\", \"\"]}\n",
                               workerName_.c_str());
    sendData(s);
    return;
  }

  if (state_ == SUBSCRIBED && jresult.boolean() == true) {
    // authorize successful
    state_ = AUTHENTICATED;
    LOG(INFO) << poolName_ << " auth success, name: \"" << workerName_ << "\"";
    return;
  }
}
