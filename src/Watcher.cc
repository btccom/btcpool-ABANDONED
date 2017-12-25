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
#include <glog/logging.h>

#include "bitcoin/chainparams.h"
#include "bitcoin/utilstrencodings.h"
#include "bitcoin/added_functions.h"

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
ClientContainer::ClientContainer(const string &kafkaBrokers)
:running_(true), kafkaBrokers_(kafkaBrokers),
kafkaProducer_(kafkaBrokers_.c_str(), KAFKA_TOPIC_RAWGBT, 0/* partition */),
kafkaStratumJobConsumer_(kafkaBrokers_.c_str(), KAFKA_TOPIC_STRATUM_JOB, 0/*patition*/),
poolStratumJob_(nullptr)
{
  base_ = event_base_new();
  assert(base_ != nullptr);
}

ClientContainer::~ClientContainer() {
  event_base_free(base_);
}

boost::shared_lock<boost::shared_mutex> ClientContainer::getPoolStratumJobReadLock() {
  return boost::shared_lock<boost::shared_mutex>(stratumJobMutex_);
}

const StratumJob * ClientContainer::getPoolStratumJob() {
  return poolStratumJob_;
}

void ClientContainer::runThreadStratumJobConsume() {
  LOG(INFO) << "start stratum job consume thread";
  
  const int32_t kTimeoutMs = 1000;

  while (running_) {
    rd_kafka_message_t *rkmessage;
    rkmessage = kafkaStratumJobConsumer_.consumer(kTimeoutMs);

    // timeout, most of time it's not nullptr and set an error:
    //          rkmessage->err == RD_KAFKA_RESP_ERR__PARTITION_EOF
    if (rkmessage == nullptr) {
      continue;
    }

    consumeStratumJob(rkmessage);

    /* Return message to rdkafka */
    rd_kafka_message_destroy(rkmessage);
  }

  LOG(INFO) << "stop jstratum job consume thread";
}

void ClientContainer::consumeStratumJob(rd_kafka_message_t *rkmessage) {
    // check error
    if (rkmessage->err) {
      if (rkmessage->err == RD_KAFKA_RESP_ERR__PARTITION_EOF) {
        // Reached the end of the topic+partition queue on the broker.
        // Not really an error.
        //      LOG(INFO) << "consumer reached end of " << rd_kafka_topic_name(rkmessage->rkt)
        //      << "[" << rkmessage->partition << "] "
        //      << " message queue at offset " << rkmessage->offset;
        // acturlly
        return;
      }
    
      LOG(ERROR) << "consume error for topic " << rd_kafka_topic_name(rkmessage->rkt)
      << "[" << rkmessage->partition << "] offset " << rkmessage->offset
      << ": " << rd_kafka_message_errstr(rkmessage);
    
      if (rkmessage->err == RD_KAFKA_RESP_ERR__UNKNOWN_PARTITION ||
          rkmessage->err == RD_KAFKA_RESP_ERR__UNKNOWN_TOPIC) {
        LOG(FATAL) << "consume fatal";
      }
      return;
    }
  
    StratumJob *sjob = new StratumJob();
    bool res = sjob->unserializeFromJson((const char *)rkmessage->payload,
                                         rkmessage->len);
    if (res == false) {
      LOG(ERROR) << "unserialize stratum job fail";
      delete sjob;
      return;
    }
    // make sure the job is not expired.
    if (jobId2Time(sjob->jobId_) + 60 < time(nullptr)) {
      LOG(ERROR) << "too large delay from kafka to receive topic 'StratumJob'";
      delete sjob;
      return;
    }

    DLOG(INFO) << "[POOL] stratum job received, height: " << sjob->height_
              << ", prevhash: " << sjob->prevHash_.ToString()
              << ", nBits: " << sjob->nBits_;

    {
      // get a write lock before change this->poolStratumJob_
      // it will unlock by itself in destructor.
      boost::unique_lock<boost::shared_mutex> writeLock(stratumJobMutex_);

      uint256 oldPrevHash;

      if (poolStratumJob_ != nullptr) {
        oldPrevHash = poolStratumJob_->prevHash_;
        delete poolStratumJob_;
      }

      poolStratumJob_ = sjob;

      if (oldPrevHash != sjob->prevHash_) {
        LOG(INFO) << "[POOL] prev block changed, height: " << sjob->height_
                  << ", prevhash: " << sjob->prevHash_.ToString()
                  << ", nBits: " << sjob->nBits_;
      }
    }
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

  /* setup threadStratumJobConsume */
  {
    const int32_t kConsumeLatestN = 1;
    
    // we need to consume the latest one
    map<string, string> consumerOptions;
    consumerOptions["fetch.wait.max.ms"] = "10";
    if (kafkaStratumJobConsumer_.setup(RD_KAFKA_OFFSET_TAIL(kConsumeLatestN),
        &consumerOptions) == false) {
      LOG(INFO) << "setup stratumJobConsume fail";
      return false;
    }
  
    if (!kafkaStratumJobConsumer_.checkAlive()) {
      LOG(ERROR) << "kafka brokers is not alive";
      return false;
    }

    threadStratumJobConsume_ = thread(&ClientContainer::runThreadStratumJobConsume, this);
  }

  return true;
}

// do add pools before init()
bool ClientContainer::addPools(const string &poolName, const string &poolHost,
                               const int16_t poolPort, const string &workerName) {
  auto ptr = new PoolWatchClient(base_, this,
                                 poolName, poolHost, poolPort, workerName);
  if (!ptr->connect()) {
    return false;
  }
  clients_.push_back(ptr);

  return true;
}

bool ClientContainer::makeEmptyGBT(int32_t blockHeight, uint32_t nBits,
                                   const string &blockPrevHash,
                                   uint32_t blockTime, uint32_t blockVersion) {

  // generate empty GBT
  string gbt;
  gbt += Strings::Format("{\"result\":{");

  gbt += Strings::Format("\"previousblockhash\":\"%s\"", blockPrevHash.c_str());
  gbt += Strings::Format(",\"height\":%d", blockHeight);
  const CChainParams& chainparams = Params();
  gbt += Strings::Format(",\"coinbasevalue\":%" PRId64"",
                         GetBlockSubsidy(blockHeight, chainparams.GetConsensus()));
  gbt += Strings::Format(",\"bits\":\"%08x\"", nBits);
  const uint32_t minTime = blockTime - 60*10;  // just set 10 mins ago
  gbt += Strings::Format(",\"mintime\":%" PRIu32"", minTime);
  gbt += Strings::Format(",\"curtime\":%" PRIu32"", blockTime);
  gbt += Strings::Format(",\"version\":%" PRIu32"", blockVersion);
  gbt += Strings::Format(",\"transactions\":[]");  // empty transactions

  gbt += Strings::Format("}}");

  const uint256 gbtHash = Hash(gbt.begin(), gbt.end());

  string sjob = Strings::Format("{\"created_at_ts\":%u,"
                                "\"block_template_base64\":\"%s\","
                                "\"gbthash\":\"%s\"}",
                                (uint32_t)time(nullptr),
                                EncodeBase64(gbt).c_str(),
                                gbtHash.ToString().c_str());

  // submit to Kafka
  kafkaProducer_.produce(sjob.c_str(), sjob.length());

  LOG(INFO) << "sumbit to Kafka, msg len: " << sjob.length();
  LOG(INFO) << "empty gbt: " << gbt;

  return true;
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
  DLOG(INFO) << "<" << poolName_ << "> UpPoolWatchClient recv(" << line.size() << "): " << line;

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
        const uint32_t nBits       = jparamsArr[6].uint32_hex();
        const uint32_t nVersion    = jparamsArr[5].uint32_hex();

        lastPrevBlockHash_ = prevHash;
        LOG(INFO) << "<" << poolName_ << "> prev block changed, height: " << blockHeight
                               << ", prev_hash: " << prevHash
                               << ", nBits: " << nBits;

        //////////////////////////////////////////////////////////////////////////
        // To ensure the external job is not deviation from the blockchain.
        // 
        // eg. a Bitcoin pool may receive a Bitcoin Cash job from a external
        // stratum server, because the stratum server is automatic switched
        // between Bitcoin and Bitcoin Cash depending on profit.
        //////////////////////////////////////////////////////////////////////////
        {
          // get a read lock before lookup this->poolStratumJob_
          // it will unlock by itself in destructor.
          auto readLock = container_->getPoolStratumJobReadLock();
          const StratumJob *poolStratumJob = container_->getPoolStratumJob();

          if (poolStratumJob == nullptr) {
            LOG(WARNING) << "<" << poolName_ << "> discard the job: pool stratum job is empty";
            return;
          }

          if (blockHeight == poolStratumJob->height_) {
            LOG(INFO) << "<" << poolName_ << "> discard the job: height is same as pool."
                                      << " pool height: " << poolStratumJob->height_
                                      << ", the job height: " << blockHeight;
            return;
          }

          if (blockHeight != poolStratumJob->height_ + 1) {
            LOG(WARNING) << "<" << poolName_ << "> discard the job: height jumping too much."
                                      << " pool height: " << poolStratumJob->height_
                                      << ", the job height: " << blockHeight;
            return;
          }

          if (nBits != poolStratumJob->nBits_) {
            LOG(WARNING) << "<" << poolName_ << "> discard the job: nBits different from pool job."
                                      << " pool nBits: " << poolStratumJob->nBits_
                                      << ", the job nBits: " << nBits;
            return;
          }
        }

        container_->makeEmptyGBT(blockHeight, nBits, prevHash, blockTime, nVersion);

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
      LOG(ERROR) << "<" << poolName_ << "> json result is null, err: " << jerror.str();
      return;
    }
    std::vector<JsonNode> resArr = jresult.array();
    if (resArr.size() < 3) {
      LOG(ERROR) << "<" << poolName_ << "> result element's number is less than 3: " << line;
      return;
    }

    extraNonce1_     = resArr[1].uint32_hex();
    extraNonce2Size_ = resArr[2].uint32();
    LOG(INFO) << "<" << poolName_ << "> extraNonce1: " << extraNonce1_
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
    LOG(INFO) << "<" << poolName_ << "> auth success, name: \"" << workerName_ << "\"";
    return;
  }
}
