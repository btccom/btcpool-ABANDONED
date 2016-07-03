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
#include "StratumServer.h"

#include "Common.h"
#include "Kafka.h"
#include "Utils.h"


////////////////////////////////// JobRepository ///////////////////////////////
JobRepository::JobRepository(const char *kafkaBrokers, Server *server):
running_(true),
kafkaConsumer_(kafkaBrokers, KAFKA_TOPIC_STRATUM_JOB, 0/*patition*/),
server_(server),
kMaxJobsLifeTime_(300),
kMiningNotifyInterval_(30),  // TODO: make as config arg
lastJobSendTime_(0)
{
  assert(kMiningNotifyInterval_ < kMaxJobsLifeTime_);
}

JobRepository::~JobRepository() {
  if (threadConsume_.joinable())
    threadConsume_.join();
}

shared_ptr<StratumJobEx> JobRepository::getStratumJobEx(const uint64_t jobId) {
  ScopeLock sl(lock_);
  auto itr = exJobs_.find(jobId);
  if (itr != exJobs_.end()) {
    return itr->second;
  }
  return nullptr;
}

shared_ptr<StratumJobEx> JobRepository::getLatestStratumJobEx() {
  ScopeLock sl(lock_);
  if (exJobs_.size()) {
    return exJobs_.rbegin()->second;
  }
  LOG(WARNING) << "getLatestStratumJobEx fail";
  return nullptr;
}

void JobRepository::stop() {
  if (!running_) {
    return;
  }
  running_ = false;
  LOG(INFO) << "stop job repository";
}

bool JobRepository::setupThreadConsume() {
  const int32_t kConsumeLatestN = 1;
  // we need to consume the latest one
  if (kafkaConsumer_.setup(RD_KAFKA_OFFSET_TAIL(kConsumeLatestN)) == false) {
    LOG(INFO) << "setup consumer fail";
    return false;
  }

  threadConsume_ = thread(&JobRepository::runThreadConsume, this);
  return true;
}

void JobRepository::runThreadConsume() {
  LOG(INFO) << "start job repository consume thread";

  const int32_t timeoutMs = 1000;
  while (running_) {
    rd_kafka_message_t *rkmessage;
    rkmessage = kafkaConsumer_.consumer(timeoutMs);

    // timeout, most of time it's not nullptr and set an error:
    //          rkmessage->err == RD_KAFKA_RESP_ERR__PARTITION_EOF
    if (rkmessage == nullptr) {
      continue;
    }

    // consume stratum job
    consumeStratumJob(rkmessage);
    rd_kafka_message_destroy(rkmessage);  /* Return message to rdkafka */

    // check if we need to send mining notify
    checkAndSendMiningNotify();

    tryCleanExpiredJobs();
  }
  LOG(INFO) << "stop job repository consume thread";
}

void JobRepository::consumeStratumJob(rd_kafka_message_t *rkmessage) {
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
    LOG(FATAL) << "too large delay from kafka to receive topic 'StratumJob'";
    delete sjob;
    return;
  }
  // here you could use Map.find() without lock, it's sure
  // that everyone is using this Map readonly now
  if (exJobs_.find(sjob->jobId_) != exJobs_.end()) {
    LOG(FATAL) << "jobId already existed";
    delete sjob;
    return;
  }

  bool isClean = false;
  if (latestPrevBlockHash_ != sjob->prevHash_) {
    isClean = true;
    latestPrevBlockHash_ = sjob->prevHash_;
    LOG(INFO) << "received new height statum job, height: " << sjob->height_
    << ", prevhash: " << sjob->prevHash_.ToString();
  }

  shared_ptr<StratumJobEx> exJob = std::make_shared<StratumJobEx>(sjob, isClean);
  {
    ScopeLock sl(lock_);

    if (isClean) {
      // mark all jobs as stale, should do this before insert new job
      for (auto it : exJobs_) {
        it.second->markStale();
      }
    }

    // insert new job
    exJobs_[sjob->jobId_] = exJob;
  }

  // if job has clean flag, call server to send job
  if (isClean) {
    sendMiningNotify(exJob);
    return;
  }
}

void JobRepository::markAllJobsAsStale() {
  ScopeLock sl(lock_);
  for (auto it : exJobs_) {
    it.second->markStale();
  }
}

void JobRepository::checkAndSendMiningNotify() {
  // last job is 'expried', send a new one
  if (exJobs_.size() &&
      lastJobSendTime_ + kMiningNotifyInterval_ <= time(nullptr))
  {
    sendMiningNotify(exJobs_.rbegin()->second);
  }
}

void JobRepository::sendMiningNotify(shared_ptr<StratumJobEx> exJob) {
  static uint64_t lastJobId = 0;
  if (lastJobId == exJob->sjob_->jobId_) {
    LOG(ERROR) << "no new jobId, ignore to send mining notify";
    return;
  }

  // send job to all clients
  server_->sendMiningNotifyToAll(exJob);
  lastJobSendTime_ = time(nullptr);

  lastJobId = exJob->sjob_->jobId_;
}

void JobRepository::tryCleanExpiredJobs() {
  ScopeLock sl(lock_);

  const uint32_t nowTs = (uint32_t)time(nullptr);
  while (exJobs_.size()) {
    // Maps (and sets) are sorted, so the first element is the smallest,
    // and the last element is the largest.
    auto itr = exJobs_.begin();

    const time_t jobTime = (time_t)(itr->first >> 32);
    if (nowTs < jobTime + kMaxJobsLifeTime_) {
      break;  // not expired
    }

    // remove expired job
    exJobs_.erase(itr);

    LOG(INFO) << "remove expired stratum job, id: " << itr->first
    << ", time: " << date("%F %T", jobTime);
  }
}


////////////////////////////////// StratumJobEx ////////////////////////////////
StratumJobEx::StratumJobEx(StratumJob *sjob, bool isClean):
isClean_(isClean), state_(MINING), sjob_(sjob)
{
  assert(sjob != nullptr);
  makeMiningNotifyStr();
}

StratumJobEx::~StratumJobEx() {
  if (sjob_) {
    delete sjob_;
    sjob_ = nullptr;
  }
}

void StratumJobEx::makeMiningNotifyStr() {
  string merkleBranchStr;
  {
    merkleBranchStr.reserve(sjob_->merkleBranch_.size() * 67);
    for (size_t i = 0; i < sjob_->merkleBranch_.size(); i++) {
      //
      // do NOT use GetHex() or uint256.ToString(), need to dump the memory
      //
      string merklStr;
      Bin2Hex(sjob_->merkleBranch_[i].begin(), 32, merklStr);
      merkleBranchStr.append("\"" + merklStr + "\",");
    }
    if (merkleBranchStr.length()) {
      merkleBranchStr.resize(merkleBranchStr.length() - 1);  // remove last ','
    }
  }

  miningNotify_ = Strings::Format("{\"id\":null,\"method\":\"mining.notify\",\"params\":["
                                  "\"%llu\",\"%s\""
                                  ",\"%s\",\"%s\""
                                  ",[%s]"
                                  ",\"%08x\",\"%08x\",\"%08x\",%s"
                                  "]}\n",
                                  sjob_->jobId_, sjob_->prevHashBeStr_.c_str(),
                                  sjob_->coinbase1_.c_str(), sjob_->coinbase2_.c_str(),
                                  merkleBranchStr.c_str(),
                                  sjob_->nVersion_, sjob_->nBits_, sjob_->nTime_,
                                  isClean_ ? "true" : "false");
}

void StratumJobEx::markStale() {
  ScopeLock sl(lock_);
  state_ = STALE;
}

bool StratumJobEx::isStale() {
  ScopeLock sl(lock_);
  return (state_ == STALE);
}

void StratumJobEx::generateCoinbaseTx(std::vector<char> *coinbaseBin,
                                      const uint32 extraNonce1,
                                      const string &extraNonce2Hex) {
  string coinbaseHex;
  const string extraNonceStr = Strings::Format("%08x%s", extraNonce1, extraNonce2Hex.c_str());
  coinbaseHex.append(sjob_->coinbase1_);
  coinbaseHex.append(extraNonceStr);
  coinbaseHex.append(sjob_->coinbase2_);
  Hex2Bin((const char *)coinbaseHex.c_str(), *coinbaseBin);
}

void StratumJobEx::generateBlockHeader(CBlockHeader *header,
                                       const uint32 extraNonce1,
                                       const string &extraNonce2Hex,
                                       const vector<uint256> &merkleBranch,
                                       const uint256 &hashPrevBlock,
                                       const uint32 nBits, const int nVersion,
                                       const uint32 nTime, const uint32 nonce) {
  std::vector<char> coinbaseBin;
  generateCoinbaseTx(&coinbaseBin, extraNonce1, extraNonce2Hex);

  header->hashPrevBlock = hashPrevBlock;
  header->nVersion      = nVersion;
  header->nBits         = nBits;
  header->nTime         = nTime;
  header->nNonce        = nonce;

  // hashMerkleRoot
  Hash(coinbaseBin.begin(), coinbaseBin.end(), header->hashMerkleRoot);
  for (const uint256 & step : merkleBranch) {
    header->hashMerkleRoot = Hash(header->hashMerkleRoot.begin(),
                                  header->hashMerkleRoot.end(),
                                  step.begin(), step.end());
  }
}

////////////////////////////////// StratumServer ///////////////////////////////
StratumServer::StratumServer(const char *ip, const unsigned short port,
                             const char *kafkaBrokers)
:running_(true), ip_(ip), port_(port), kafkaBrokers_(kafkaBrokers)
{
  // TODO: serverId_
}

StratumServer::~StratumServer() {
}

bool StratumServer::init() {
  if (!server_.setup(ip_.c_str(), port_, kafkaBrokers_.c_str())) {
    LOG(ERROR) << "fail to setup server";
    return false;
  }
  return true;
}

void StratumServer::stop() {
  if (!running_) {
    return;
  }
  running_ = false;
  server_.stop();
  LOG(INFO) << "stop stratum server";
}

void StratumServer::run() {
  server_.run();
}

///////////////////////////////////// Server ///////////////////////////////////
Server::Server(): base_(nullptr), signal_event_(nullptr), listener_(nullptr),
kafkaProducerShareLog_(nullptr), kafkaProducerSolvedShare_(nullptr),
kShareAvgSeconds_(8), // TODO: read from cfg
jobRepository_(nullptr)
{
}

Server::~Server() {
  if (signal_event_ != nullptr) {
    event_free(signal_event_);
  }
  if (listener_ != nullptr) {
    evconnlistener_free(listener_);
  }
  if (base_ != nullptr) {
    event_base_free(base_);
  }
  if (kafkaProducerShareLog_ != nullptr) {
    delete kafkaProducerShareLog_;
  }
  if (kafkaProducerSolvedShare_ != nullptr) {
    delete kafkaProducerSolvedShare_;
  }
  if (jobRepository_ != nullptr) {
    delete jobRepository_;
  }
}

bool Server::setup(const char *ip, const unsigned short port, const char *kafkaBrokers) {
  kafkaProducerSolvedShare_ = new KafkaProducer(kafkaBrokers,
                                                KAFKA_TOPIC_SOLVED_SHARE,
                                                RD_KAFKA_PARTITION_UA);
  kafkaProducerShareLog_ = new KafkaProducer(kafkaBrokers,
                                             KAFKA_TOPIC_SHARE_LOG,
                                             RD_KAFKA_PARTITION_UA);
  jobRepository_ = new JobRepository(kafkaBrokers, this);
  if (!jobRepository_->setupThreadConsume()) {
    return false;
  }

  base_ = event_base_new();
  if(!base_) {
    LOG(ERROR) << "server: cannot create base";
    return false;
  }

  memset(&sin_, 0, sizeof(sin_));
  sin_.sin_family = AF_INET;
  sin_.sin_port   = htons(port);
  sin_.sin_addr.s_addr = htonl(INADDR_ANY);
  if (ip && inet_pton(AF_INET, ip, &sin_.sin_addr) == 0) {
    LOG(ERROR) << "invalid ip: " << ip;
    return false;
  }

  listener_ = evconnlistener_new_bind(base_,
                                      Server::listenerCallback,
                                      (void*)this,
                                      LEV_OPT_REUSEABLE|LEV_OPT_CLOSE_ON_FREE,
                                      -1, (struct sockaddr*)&sin_, sizeof(sin_));
  if(!listener_) {
    LOG(ERROR) << "cannot create listener: " << ip << ":" << port;
    return false;
  }
  return true;
}

void Server::run() {
  if(base_ != NULL) {
    //    event_base_loop(base_, EVLOOP_NONBLOCK);
    event_base_dispatch(base_);
  }
}

void Server::stop() {
  LOG(INFO) << "stop tcp server event loop";
  event_base_loopexit(base_, NULL);
  jobRepository_->stop();
}

void Server::sendMiningNotifyToAll(shared_ptr<StratumJobEx> exJobPtr) {
  ScopeLock sl(connsLock_);
  for (const auto itr : connections_) {
    itr.second->sendMiningNotify(exJobPtr);
  }
}

void Server::addConnection(evutil_socket_t fd, StratumSession *connection) {
  ScopeLock sl(connsLock_);
  connections_.insert(std::pair<evutil_socket_t, StratumSession *>(fd, connection));
}

void Server::removeConnection(evutil_socket_t fd) {
  ScopeLock sl(connsLock_);
  auto itr = connections_.find(fd);
  if (itr == connections_.end())
    return;

  delete itr->second;
  connections_.erase(itr);
}

void Server::listenerCallback(struct evconnlistener* listener,
                              evutil_socket_t fd,
                              struct sockaddr *saddr,
                              int socklen, void* data)
{
  Server *server = static_cast<Server *>(data);
  struct event_base  *base = (struct event_base*)server->base_;
  struct bufferevent *bev;

  bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);
  if(!bev) {
    event_base_loopbreak(base);
    LOG(ERROR) << "error constructing bufferevent!";
    return;
  }

  StratumSession* conn = new StratumSession(fd, bev, server, saddr,
                                            server->kShareAvgSeconds_);
  bufferevent_setcb(bev,
                    Server::readCallback,
                    NULL,  /* we use bufferevent, so don't need to watch write events */
                    Server::eventCallback, (void*)conn);
  bufferevent_enable(bev, EV_READ);

  server->addConnection(fd, conn);
}

void Server::readCallback(struct bufferevent* bev, void *connection) {
  StratumSession *conn = static_cast<StratumSession *>(connection);
  conn->readBuf(bufferevent_get_input(bev));
}

void Server::eventCallback(struct bufferevent* bev, short events,
                              void *connection) {
  StratumSession *conn = static_cast<StratumSession *>(connection);
  Server       *server = static_cast<Server *>(conn->server_);

  if (events & BEV_EVENT_EOF) {
    server->removeConnection(conn->fd_);
  }
  else if (events & (BEV_EVENT_TIMEOUT|BEV_EVENT_READING)) {
    LOG(INFO) << "connection read timeout";
    server->removeConnection(conn->fd_);
  }
  else if (events & BEV_EVENT_ERROR) {
    LOG(ERROR) << "got an error on the connection: " << strerror(errno);
    server->removeConnection(conn->fd_);
  }
  else {
    LOG(ERROR) << "unhandled events: " << events;
  }
}

int Server::checkShare(const uint64_t jobId,
                       const uint32 extraNonce1, const string &extraNonce2Hex,
                       const uint32_t nTime, const uint32_t nonce,
                       const uint256 &jobTarget, const string &workFullName) {
  shared_ptr<StratumJobEx> exJobPtr = jobRepository_->getStratumJobEx(jobId);
  if (exJobPtr == nullptr) {
    return StratumError::JOB_NOT_FOUND;
  }
  StratumJob *sjob = exJobPtr->sjob_;

  if (exJobPtr->isStale()) {
    return StratumError::JOB_NOT_FOUND;
  }
  if (nTime <= sjob->minTime_) {
    return StratumError::TIME_TOO_OLD;
  }
  if (nTime > sjob->nTime_ + 600) {
    return StratumError::TIME_TOO_NEW;
  }

  CBlockHeader header;
  exJobPtr->generateBlockHeader(&header, extraNonce1, extraNonce2Hex,
                                sjob->merkleBranch_, sjob->prevHash_,
                                sjob->nBits_, sjob->nVersion_, nTime, nonce);
  uint256 blkHash = header.GetHash();

  if (blkHash <= sjob->networkTarget_) {
    // TODO: broadcast block solved share
    LOG(INFO) << ">>>> found a new block: " << blkHash.ToString()
    << ", jobId: " << jobId << ", by: " << workFullName
    << " <<<<";

    jobRepository_->markAllJobsAsStale();
  }

  // print out high diff share, 2^10 = 1024
  if ((blkHash >> 10) <= sjob->networkTarget_) {
    LOG(INFO) << "high diff share, blkhash: " << blkHash.ToString()
    << ", diff: " << TargetToBdiff(blkHash)
    << ", networkDiff: " << TargetToBdiff(sjob->networkTarget_)
    << ", by: " << workFullName;
  }

  if (blkHash > jobTarget) {
    return StratumError::LOW_DIFFICULTY;
  }

  // reach here means an valid share
  return StratumError::NO_ERROR;
}

void Server::sendShare2Kafka(const uint8_t *data, size_t len) {
  // TODO:
  // should be thread safe
}
