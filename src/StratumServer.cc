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
JobRepository::JobRepository(const char *kafkaBrokers):
kafkaConsumer_(kafkaBrokers, KAFKA_TOPIC_STRATUM_JOB, 0/*patition*/)
{
}

JobRepository::~JobRepository() {
}

shared_ptr<StratumJobEx> JobRepository::getStratumJobEx(const uint64_t jobId) {
  ScopeLock sl(lock_);
  auto itr = exJobs_.find(jobId);
  if (itr != exJobs_.end()) {
    return itr->second;
  }
  return nullptr;
}


////////////////////////////////// StratumJobEx ////////////////////////////////
StratumJobEx::StratumJobEx(StratumJob *sjob):
sjob_(sjob), isClean_(false), state_(MINING)
{
}

StratumJobEx::~StratumJobEx() {
  if (sjob_) {
    delete sjob_;
    sjob_ = nullptr;
  }
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
                                       const vector<char> &coinbaseTx,
                                       const vector<uint256> &merkleBranch,
                                       const uint256 &hashPrevBlock,
                                       const uint32 nBits, const int nVersion,
                                       const uint32 nTime, const uint32 nonce) {
  header->hashPrevBlock = hashPrevBlock;
  header->nVersion      = nVersion;
  header->nBits         = nBits;
  header->nTime         = nTime;
  header->nNonce        = nonce;

  // hashMerkleRoot
  Hash(coinbaseTx.begin(), coinbaseTx.end(), header->hashMerkleRoot);
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
  while (running_) {
    server_.run();
  }
}

///////////////////////////////////// Server ///////////////////////////////////
Server::Server(): base_(NULL), listener_(NULL), signal_event_(NULL),
kafkaProducerSolvedShare_(nullptr), kafkaProducerShareLog_(nullptr)
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
}

bool Server::setup(const char *ip, const unsigned short port, const char *kafkaBrokers) {
  kafkaProducerSolvedShare_ = new KafkaProducer(kafkaBrokers,
                                                KAFKA_TOPIC_SOLVED_SHARE,
                                                RD_KAFKA_PARTITION_UA);
  kafkaProducerShareLog_ = new KafkaProducer(kafkaBrokers,
                                             KAFKA_TOPIC_SHARE_LOG,
                                             RD_KAFKA_PARTITION_UA);
  jobRepository_ = new JobRepository(kafkaBrokers);

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
}

void Server::addConnection(evutil_socket_t fd, StratumSession *connection) {
  connections_.insert(std::pair<evutil_socket_t, StratumSession *>(fd, connection));
}

void Server::removeConnection(evutil_socket_t fd) {
  auto itr = connections_.find(fd);
  if (itr == connections_.end())
    return;

  delete itr->second;
  connections_.erase(itr);
}

void Server::sendToAllClients(const char* data, size_t len) {
  auto it = connections_.begin();
  while(it != connections_.end()) {
    it->second->send(data, len);
    ++it;
  }
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

  StratumSession* conn = new StratumSession(fd, bev, server, saddr);
  server->addConnection(fd, conn);

  bufferevent_setcb(bev,
                    Server::readCallback,
                    NULL,  /* we use bufferevent, so don't need to watch write events */
                    Server::eventCallback, (void*)conn);
  bufferevent_enable(bev, EV_READ);
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

int Server::submitShare(const uint64_t jobId,
                        const uint32 extraNonce1, const string &extraNonce2Hex,
                        const uint32_t nTime, const uint32_t nonce,
                        const uint256 &jobTarget, const string &workName) {
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

  vector<char> coinbaseBin;
  exJobPtr->generateCoinbaseTx(&coinbaseBin, extraNonce1, extraNonce2Hex);

  CBlockHeader header;
  exJobPtr->generateBlockHeader(&header, coinbaseBin, sjob->merkleBranch_,
                                sjob->prevHash_, sjob->nBits_, sjob->nVersion_,
                                nTime, nonce);
  uint256 blkHash = header.GetHash();

  if (blkHash <= sjob->networkTarget_) {
    // TODO: broadcast block solved share
  }

  // print out high diff share, 2^10 = 1024
  if ((blkHash >> 10) <= sjob->networkTarget_) {
    LOG(INFO) << "high diff share, blkhash: " << blkHash.ToString()
    << ", diff: " << TargetToBdiff(blkHash)
    << ", networkDiff: " << TargetToBdiff(sjob->networkTarget_)
    << ", by: " << workName;
  }

  if (blkHash > jobTarget) {
    return StratumError::LOW_DIFFICULTY;
  }

  // reach here means an valid share
  return StratumError::NO_ERROR;
}

