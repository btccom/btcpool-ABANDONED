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
#include "StratumSession.h"
#include "Utils.h"
#include "utilities_js.hpp"

#include <arpa/inet.h>
#include <boost/algorithm/string.hpp>

#include "StratumServer.h"


//////////////////////////////// DiffController ////////////////////////////////
void DiffController::setMinTarget(arith_uint256 target) {
  if (target < KMinTarget_) {
    target = KMinTarget_;
  }
  minTarget_ = target;
}

void DiffController::resetCurTarget(arith_uint256 target) {
  // don't less than system min target
  if (target < KMinTarget_) {
    target = KMinTarget_;
  }
  // don't less than use's min target
  if (target < minTarget_) {
    target = minTarget_;
  }

  // set to zero
  sharesNum_.mapMultiply(0);
  curTarget_ = target;
}

void DiffController::addAcceptedShare() {
  const int64 idx = time(nullptr) / kRecordSeconds_;  // index
  sharesNum_.insert(idx, 1.0);
}

// call this function everytime before send 'notify.mininig'
uint32_t DiffController::calcCurBits() {
  arith_uint256 target = _calcCurTarget();
  if (target < minTarget_) {
    target = minTarget_;
  }
  return TargetToBits(ArithToUint256(target));
}

arith_uint256 DiffController::_calcCurTarget() {
  const time_t now = time(nullptr);
  const int64 k = now / kRecordSeconds_;
  const double sharesCount = (double)sharesNum_.sum(k);
  if (startTime_ == 0) {  // first time, we set now as the start time
    startTime_ = time(nullptr);
  }

  const double kRateHigh = 1.40;
  const double kRateLow  = 0.40;
  double expectedCount = round(kDiffWindow_ / (double)shareAvgSeconds_);

  // this is for very low hashrate miner, should received at least one share every 60 seconds
  if (!isFullWindow(now) && now >= startTime_ + 60 &&
      sharesCount <= (int32_t)((now - startTime_)/60.0) &&
      curTarget_ >= minTarget_*2) {
    curTarget_ /= 2;
    sharesNum_.mapMultiply(2.0);
    return curTarget_;
  }

  // too fast
  if (sharesCount > expectedCount * kRateHigh) {
    while (sharesNum_.sum(k) > expectedCount) {
      curTarget_ *= 2;
      sharesNum_.mapDivide(2.0);
    }
    return curTarget_;
  }

  // too slow
  if (isFullWindow(now) && curTarget_ >= minTarget_*2) {
    while (sharesNum_.sum(k) < expectedCount * kRateLow &&
           curTarget_ >= minTarget_*2) {
      curTarget_ /= 2;
      sharesNum_.mapMultiply(2.0);
    }
    assert(curTarget_ >= minTarget_);
    return curTarget_;
  }

  return curTarget_;
}



//////////////////////////////// StratumSession ////////////////////////////////
StratumSession::StratumSession(evutil_socket_t fd, struct bufferevent *bev,
                               Server *server, struct sockaddr *saddr,
                               const int32_t shareAvgSeconds,
                               const uint32_t extraNonce1) :
shareAvgSeconds_(shareAvgSeconds), diffController_(shareAvgSeconds_),
shortJobIdIdx_(0u), agentSessions_(nullptr), isDead_(false),
invalidSharesCounter_(INVALID_SHARE_SLIDING_WINDOWS_SIZE),
bev_(bev), fd_(fd), server_(server)
{
  state_ = CONNECTED;
  currTarget_  = arith_uint256();
  extraNonce1_ = extraNonce1;

  // usually stratum job interval is 30~60 seconds, 10 is enough for miners
  // should <= 10, we use short_job_id,  range: [0 ~ 9]. do NOT change it.
  kMaxNumLocalJobs_ = 10;
  assert(kMaxNumLocalJobs_ <= 10);

  inBuf_  = evbuffer_new();
  isLongTimeout_    = false;
  isNiceHashClient_ = false;

  clientAgent_ = "unknown";
  // ipv4
  clientIp_.resize(INET_ADDRSTRLEN);
  struct sockaddr_in *saddrin = (struct sockaddr_in *)saddr;
  clientIp_ = inet_ntop(AF_INET, &saddrin->sin_addr,
                        (char *)clientIp_.data(), (socklen_t)clientIp_.size());
  clientIpInt_ = saddrin->sin_addr.s_addr;

  setup();

  LOG(INFO) << "client connect, ip: " << clientIp_;
}

StratumSession::~StratumSession() {
  LOG(INFO) << "close stratum session, ip: " << clientIp_
  << ", name: \"" << worker_.fullName_ << "\""
  << ", agent: \"" << clientAgent_ << "\"";

//  close(fd_);  // we don't need to close because we set 'BEV_OPT_CLOSE_ON_FREE'
  evbuffer_free(inBuf_);
  bufferevent_free(bev_);
}

void StratumSession::markAsDead() {
  // mark as dead
  isDead_ = true;

  // sent event to kafka: miner_dead
  if (worker_.userId_ > 0) {
    string eventJson;
    eventJson = Strings::Format("{\"created_at\":\"%s\","
                                "\"type\":\"miner_dead\","
                                "\"content\":{"
                                "\"user_id\":%d,\"user_name\":\"%s\","
                                "\"worker_name\":\"%s\","
                                "\"client_agent\":\"%s\",\"ip\":\"%s\""
                                "}}",
                                date("%F %T").c_str(),
                                worker_.userId_, worker_.userName_.c_str(),
                                worker_.workerName_.c_str(),
                                clientAgent_.c_str(), clientIp_.c_str());
    server_->sendCommonEvents2Kafka(eventJson);
  }
}

bool StratumSession::isDead() {
  return (isDead_ == true) ? true : false;
}

void StratumSession::setup() {
  // we set 15 seconds, will increase the timeout after sub & auth
  setReadTimeout(15);
}

void StratumSession::setReadTimeout(const int32_t timeout) {
  // clear it
  bufferevent_set_timeouts(bev_, NULL, NULL);

  // set a new one
  struct timeval rtv = {timeout, 0};
  struct timeval wtv = {120, 0};
  bufferevent_set_timeouts(bev_, &rtv, &wtv);
}

// if read success, will remove data from eventbuf
bool StratumSession::tryReadLine(string &line) {
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

void StratumSession::handleLine(const string &line) {
  DLOG(INFO) << "recv(" << line.size() << "): " << line;

  JsonNode jnode;
  if (!JsonNode::parse(line.data(), line.data() + line.size(), jnode)) {
    LOG(ERROR) << "decode line fail, not a json string";
    return;
  }
  JsonNode jid = jnode["id"];
  JsonNode jmethod = jnode["method"];
  JsonNode jparams = jnode["params"];

  string idStr = "null";
  if (jid.type() == Utilities::JS::type::Int) {
    idStr = jid.str();
  } else if (jid.type() == Utilities::JS::type::Str) {
    idStr = "\"" + jnode["id"].str() + "\"";
  }

  if (jmethod.type() == Utilities::JS::type::Str &&
      jmethod.size() != 0 &&
      jparams.type() == Utilities::JS::type::Array) {
    handleRequest(idStr, jmethod.str(), jparams);
    return;
  }

  // invalid params
  responseError(idStr, StratumError::ILLEGAL_PARARMS);
}

void StratumSession::responseError(const string &idStr, int errCode) {
  //
  // {"id": 10, "result": null, "error":[21, "Job not found", null]}
  //
  char buf[1024];
  int len = snprintf(buf, sizeof(buf),
                     "{\"id\":%s,\"result\":null,\"error\":[%d,\"%s\",null]}\n",
                     idStr.empty() ? "null" : idStr.c_str(),
                     errCode, StratumError::toString(errCode));
  sendData(buf, len);
}

void StratumSession::responseTrue(const string &idStr) {
  const string s = "{\"id\":" + idStr + ",\"result\":true,\"error\":null}\n";
  sendData(s);
}

void StratumSession::handleRequest(const string &idStr, const string &method,
                                   const JsonNode &jparams) {
  if (method == "mining.submit") {  // most of requests are 'mining.submit'
    handleRequest_Submit(idStr, jparams);
  }
  else if (method == "mining.subscribe") {
    handleRequest_Subscribe(idStr, jparams);
  }
  else if (method == "mining.authorize") {
    handleRequest_Authorize(idStr, jparams);
  }
  else if (method == "mining.suggest_target") {
    handleRequest_SuggestTarget(idStr, jparams);
  } else {
    // unrecognised method, just ignore it
    LOG(WARNING) << "unrecognised method: \"" << method << "\""
    << ", client: " << clientIp_ << "/" << clientAgent_;
  }
}

void StratumSession::handleRequest_Subscribe(const string &idStr,
                                             const JsonNode &jparams) {
  if (state_ != CONNECTED) {
    responseError(idStr, StratumError::UNKNOWN);
    return;
  }
  state_ = SUBSCRIBED;

  //
  //  params[0] = client version     [optional]
  //  params[1] = session id of pool [optional]
  //
  // client request eg.:
  //  {"id": 1, "method": "mining.subscribe", "params": ["MINER_USER_AGENT", "SESSION_ID", "CONNECT_HOST", CONNECT_PORT]}
  //
  if (jparams.children()->size() >= 1) {
    clientAgent_ = jparams.children()->at(0).str().substr(0, 30);  // 30 is max len
    clientAgent_ = filterWorkerName(clientAgent_);
  }

  //
  // Response:
  // {"id": 1, "result": ["SESSION_ID", "NONCE_1"], "error": null}
  // SESSION_ID:
  //   Servers MAY set SESSION_ID to null to indicate that they do not support session resuming.
  //
  const string s = Strings::Format("{\"id\":%s,\"result\":[null,\"%08x%08x\"],\"error\":null}\n",
                                   idStr.c_str(), 0u, extraNonce1_);
  sendData(s);

  if (clientAgent_ == "__PoolWatcher__") {
    isLongTimeout_ = true;
  }
}

void StratumSession::_handleRequest_AuthorizePassword(const string &password) {
  // testcase: TEST(StratumSession, SetDiff)
  using namespace boost::algorithm;

  arith_uint256 t, mt;  // target, min_target
  vector<string> arr;   // key=value,key=value
  split(arr, password, is_any_of(","));
  if (arr.size() == 0)
    return;

  for (auto it = arr.begin(); it != arr.end(); it++) {
    vector<string> arr2;  // key,value
    split(arr2, *it, is_any_of("="));
    if (arr2.size() != 2 || arr2[1].empty()) {
      continue;
    }

    if (arr2[0] == "t") {
      // 't' : start target
      t = uint256(arr2[1]);
    }
    else if (arr2[0] == "mt") {
      // 'mt' : minimum target
      mt = uint256(arr2[1]);
    }
  }

  // set min diff first
  if (mt >= DiffController::KMinTarget_) {
    diffController_.setMinTarget(mt);
  }

  // than set current diff
  if (t >= DiffController::resetCurTarget) {
    diffController_.resetCurDiff(d);
  }
}

void StratumSession::handleRequest_Authorize(const string &idStr,
                                             const JsonNode &jparams) {
  if (state_ != SUBSCRIBED) {
    responseError(idStr, StratumError::NOT_SUBSCRIBED);
    return;
  }

  //
  //  params[0] = user[.worker]
  //  params[1] = password
  //  eg. {"id": 2, "method": "mining.authorize", "params": ["WORKER_NAME", "WORKER_PASSWORD"]}
  //
  if (jparams.children()->size() < 1) {
    responseError(idStr, StratumError::INVALID_USERNAME);
    return;
  }

  const string password = jparams.children()->at(1).str();
  if (!password.empty()) {
    _handleRequest_AuthorizePassword(password);
  }

  const string fullName = jparams.children()->at(0).str();
  const string userName = worker_.getUserName(fullName);

  const int32_t userId = server_->userInfo_->getUserId(userName);
  if (userId <= 0) {
    responseError(idStr, StratumError::INVALID_USERNAME);
    return;
  }

  // auth success
  responseTrue(idStr);
  state_ = AUTHENTICATED;

  // set id & names, will filter workername in this func
  worker_.setUserIDAndNames(userId, fullName);
  server_->userInfo_->addWorker(worker_.userId_, worker_.workerHashId_,
                                worker_.workerName_, clientAgent_);
  DLOG(INFO) << "userId: " << worker_.userId_
  << ", wokerHashId: " << worker_.workerHashId_ << ", workerName:" << worker_.workerName_;

  // set read timeout to 10 mins, it's enought for most miners even usb miner.
  // if it's a pool watcher, set timeout to a week
  setReadTimeout(isLongTimeout_ ? 86400*7 : 60*10);

  // send latest stratum job
  sendMiningNotify(server_->jobRepository_->getLatestStratumJobEx(), true/* is first job */);

  // sent events to kafka: miner_connect
  {
    string eventJson;
    eventJson = Strings::Format("{\"created_at\":\"%s\","
                                "\"type\":\"miner_connect\","
                                "\"content\":{"
                                "\"user_id\":%d,\"user_name\":\"%s\","
                                "\"worker_name\":\"%s\","
                                "\"client_agent\":\"%s\",\"ip\":\"%s\""
                                "}}",
                                date("%F %T").c_str(),
                                worker_.userId_, worker_.userName_.c_str(),
                                worker_.workerName_.c_str(),
                                clientAgent_.c_str(), clientIp_.c_str());
    server_->sendCommonEvents2Kafka(eventJson);
  }
}

void StratumSession::handleRequest_SuggestTarget(const string &idStr,
                                                 const JsonNode &jparams) {
  if (state_ != CONNECTED) {
    return;  // suggest should be call before subscribe
  }
  if (jparams.children()->size() == 0) {
    responseError(idStr, StratumError::ILLEGAL_PARARMS);
    return;
  }
  diffController_.resetCurTarget(uint256(jparams.children()->at(0).str()));
}

void StratumSession::handleRequest_Submit(const string &idStr,
                                          const JsonNode &jparams) {
  if (state_ != AUTHENTICATED) {
    responseError(idStr, StratumError::UNAUTHORIZED);

    // there must be something wrong, send reconnect command
    const string s = "{\"id\":null,\"method\":\"client.reconnect\",\"params\":[]}\n";
    sendData(s);

    return;
  }
  // Request:
  // {"id": 4, "method": "mining.submit", "params": ["WORKER_NAME", "JOB_ID", "TIME", "NONCE_2", "EQUIHASH_SOLUTION"]}
  //
  //  params[0] = WORKER_NAME
  //  params[1] = JOB_ID
  //  params[2] = TIME
  //  params[3] = NONCE_2
  //  params[4] = EQUIHASH_SOLUTION
  if (jparams.children()->size() < 5) {
    responseError(idStr, StratumError::ILLEGAL_PARARMS);
    return;
  }

  const uint16_t shortJobId = (uint16_t)jparams.children()->at(1).uint32_hex();
  const uint32_t nTime      = jparams.children()->at(2).uint32_hex();
  const string nonce2hex    = jparams.children()->at(3).str();
  const string solution     = jparams.children()->at(4).str();

  if (nonce2hex.length()  != 32) {
    responseError(idStr, StratumError::ILLEGAL_PARARMS);
    return;
  }

  handleRequest_Submit(idStr, shortJobId, nonce2hex, solution, nTime);
}

void StratumSession::handleRequest_Submit(const string &idStr,
                                          const uint16_t shortJobId,
                                          const string &nonce2hex,
                                          const string &solution,
                                          const uint32_t nTime) {
  LocalJob *localJob = findLocalJob(shortJobId);
  if (localJob == nullptr) {
    // if can't find localJob, could do nothing
    responseError(idStr, StratumError::JOB_NOT_FOUND);
    return;
  }

  shared_ptr<StratumJobEx> exjob = server_->jobRepository_->getStratumJobEx(localJob->jobId_);
  if (exjob.get() == nullptr) {
    // if can't find StratumJobEx, could do nothing
    responseError(idStr, StratumError::JOB_NOT_FOUND);
    return;
  }

  Share share;
  share.jobId_        = localJob->jobId_;
  share.workerHashId_ = worker_.workerHashId_;
  share.ip_           = clientIpInt_;
  share.userId_       = worker_.userId_;
  share.jobBits_      = localJob->jobBits_;
  share.blkBits_      = localJob->blkBits_;
  share.blkHeight_    = exjob->sjob_->height_;
  share.shareTime_    = (uint32_t)time(nullptr);
  share.result_       = Share::Result::REJECT;

  // we send share to kafka by default, but if there are lots of invalid
  // shares in a short time, we just drop them.
  bool isSendShareToKafka = true;

  int submitResult;
  LocalShare localShare(strtoll(nonce2hex.substr(0 , 16).c_str(), nullptr, 16),
                        strtoll(nonce2hex.substr(16, 16).c_str(), nullptr, 16),
                        nTime);

  // can't find local share
  if (!localJob->addLocalShare(localShare)) {
    // duplicate
    responseError(idStr, StratumError::DUPLICATE_SHARE);

    // add invalid share to counter
    invalidSharesCounter_.insert((int64_t)time(nullptr), 1);

    goto finish;
  }

  // build block header and check share
  submitResult = server_->checkShare(share, extraNonce1_, nonce2hex,
                                     solution, nTime,
                                     worker_.fullName_);

  if (submitResult == StratumError::NO_ERROR) {
    // accepted share
    share.result_ = Share::Result::ACCEPT;

    diffController_.addAcceptedShare();
    responseTrue(idStr);
  } else {
    // reject share
    responseError(idStr, submitResult);

    // add invalid share to counter
    invalidSharesCounter_.insert((int64_t)time(nullptr), 1);
  }


finish:
  DLOG(INFO) << share.toString();

  // check if thers is invalid share spamming
  if (share.result_ != Share::Result::ACCEPT) {
    int64_t invalidSharesNum = invalidSharesCounter_.sum(time(nullptr),
                                                         INVALID_SHARE_SLIDING_WINDOWS_SIZE);
    // too much invalid shares, don't send them to kafka
    if (invalidSharesNum >= INVALID_SHARE_SLIDING_WINDOWS_MAX_LIMIT) {
      isSendShareToKafka = false;

      LOG(WARNING) << "invalid share spamming, diff: "
      << share.share_ << ", uid: " << worker_.userId_
      << ", uname: \""  << worker_.userName_ << "\", agent: \""
      << clientAgent_ << "\", ip: " << clientIp_;
    }
  }

  if (isSendShareToKafka) {
  	server_->sendShare2Kafka((const uint8_t *)&share, sizeof(Share));
  }
  return;
}

StratumSession::LocalJob *StratumSession::findLocalJob(uint16_t shortJobId) {
  for (auto rit = localJobs_.rbegin(); rit != localJobs_.rend(); ++rit) {
    if (rit->shortJobId_ == shortJobId) {
      return &(*rit);
    }
  }
  return nullptr;
}

void StratumSession::sendSetTarget(const uint32_t bits) {
  // {"id": null, "method": "mining.set_target", "params": ["TARGET"]}
  string s = Strings::Format("{\"id\":null,\"method\":\"mining.set_target\""
                             ",\"params\":[\"%s\"]}\n",
                             BitsToTarget(bits).ToString().c_str());
  sendData(s);
}

uint16_t StratumSession::allocShortJobId() {
  // return range: [0, UINT16_MAX]
  if (shortJobIdIdx_ == UINT16_MAX) {
    shortJobIdIdx_ = 0;
  }
  return shortJobIdIdx_++;
}

void StratumSession::sendMiningNotify(shared_ptr<StratumJobEx> exJobPtr,
                                      bool isFirstJob) {
  if (state_ < AUTHENTICATED || exJobPtr == nullptr) {
    return;
  }
  StratumJob *sjob = exJobPtr->sjob_;  // alias

  localJobs_.push_back(LocalJob());
  LocalJob &ljob = *(localJobs_.rbegin());
  ljob.jobId_         = sjob->jobId_;
  ljob.jobBits_       = sjob->diffController_.calcCurBits();
  ljob.blkBits_       = sjob->header_.nBits;
  ljob.shortJobId_    = allocShortJobId();

  // check if we need to 'mining.set_target'
  if (currJobBits_ != ljob.jobBits_) {
    sendSetTarget(ljob.jobBits_);
    currJobBits_ = ljob.jobBits_;
  }

  string notifyStr;
  notifyStr.reserve(1024);

  // notify1
  notifyStr.append(exJobPtr->miningNotify1_);

  // jobId
  notifyStr.append(Strings::Format("%x", ljob.shortJobId_));  // short jobId

  // notify2
  if (isFirstJob)
  	notifyStr.append(exJobPtr->miningNotify2True_);
  else
    notifyStr.append(exJobPtr->miningNotify2_);

  sendData(notifyStr);  // send notify string

  // clear localJobs_
  while (localJobs_.size() >= kMaxNumLocalJobs_) {
    localJobs_.pop_front();
  }
}

void StratumSession::sendData(const char *data, size_t len) {
  // add data to a bufferevent’s output buffer
  // it is automatically locked so we don't need to lock
  bufferevent_write(bev_, data, len);
//  DLOG(INFO) << "send(" << len << "): " << data;
}

// if read a message (ex-message or stratum) success should return true,
// otherwise return false.
bool StratumSession::handleMessage() {
  //
  // handle ex-message
  //
  const size_t evBufLen = evbuffer_get_length(inBuf_);

  // no matter what kind of messages, length should at least 4 bytes
  if (evBufLen < 4)
    return false;

  uint8_t buf[4];
  evbuffer_copyout(inBuf_, buf, 4);

  //
  // handle stratum message
  //
  string line;
  if (tryReadLine(line)) {
    handleLine(line);
    return true;
  }

  return false;  // read mesasge failure
}

void StratumSession::readBuf(struct evbuffer *buf) {
  // moves all data from src to the end of dst
  evbuffer_add_buffer(inBuf_, buf);

  while (handleMessage()) {
  }
}

uint32_t StratumSession::getSessionId() const {
  return extraNonce1_;
}


///////////////////////////////// AgentSessions ////////////////////////////////
AgentSessions::AgentSessions(const int32_t shareAvgSeconds,
                             StratumSession *stratumSession)
:shareAvgSeconds_(shareAvgSeconds), stratumSession_(stratumSession)
{
  kDefaultDiff2Exp_ = (uint8_t)log2(DiffController::kDefaultDiff_);

  // we just pre-alloc all
  workerIds_.resize(UINT16_MAX, 0);
  diffControllers_.resize(UINT16_MAX, nullptr);
  curDiff2ExpVec_.resize(UINT16_MAX, kDefaultDiff2Exp_);
}

AgentSessions::~AgentSessions() {
  for (auto ptr : diffControllers_) {
    if (ptr != nullptr) {
      delete ptr;
    }
  }
}

int64_t AgentSessions::getWorkerId(const uint16_t sessionId) {
  return workerIds_[sessionId];
}

void AgentSessions::handleExMessage_RegisterWorker(const string *exMessage) {
  //
  // CMD_REGISTER_WORKER:
  // | magic_number(1) | cmd(1) | len (2) | session_id(2) | clientAgent | worker_name |
  //
  if (exMessage->size() < 8 || exMessage->size() > 100 /* 100 bytes is big enough */)
    return;

  const uint8_t *p = (uint8_t *)exMessage->data();
  const uint16_t sessionId = *(uint16_t *)(p + 4);
  if (sessionId > AGENT_MAX_SESSION_ID)
    return;

  // copy out string and make sure end with zero
  string clientStr;
  clientStr.append(exMessage->begin() + 6, exMessage->end());
  clientStr[clientStr.size() - 1] = '\0';

  // client agent
  const char *clientAgentPtr = clientStr.c_str();
  const string clientAgent = filterWorkerName(clientAgentPtr);

  // worker name
  string workerName;
  if (strlen(clientAgentPtr) < clientStr.size() - 2) {
    workerName = filterWorkerName(clientAgentPtr + strlen(clientAgentPtr) + 1);
  }
  if (workerName.empty())
    workerName = DEFAULT_WORKER_NAME;

  // worker Id
  const int64_t workerId = StratumWorker::calcWorkerId(workerName);

  DLOG(INFO) << "[agent] clientAgent: " << clientAgent
  << ", workerName: " << workerName << ", workerId: "
  << workerId << ", session id:" << sessionId;

  // set sessionId -> workerId
  workerIds_[sessionId] = workerId;

  // deletes managed object
  if (diffControllers_[sessionId] != nullptr) {
    delete diffControllers_[sessionId];
    diffControllers_[sessionId] = nullptr;
  }

  // acquires new pointer
  assert(diffControllers_[sessionId] == nullptr);
  diffControllers_[sessionId] = new DiffController(shareAvgSeconds_);

  // set curr diff to default Diff
  curDiff2ExpVec_[sessionId] = kDefaultDiff2Exp_;
}

void AgentSessions::handleExMessage_SubmitShare(const string *exMessage,
                                                const bool isWithTime) {
  //
  // CMD_SUBMIT_SHARE / CMD_SUBMIT_SHARE_WITH_TIME:
  // | magic_number(1) | cmd(1) | len (2) | jobId (uint8_t) | session_id (uint16_t) |
  // | extra_nonce2 (uint32_t) | nNonce (uint32_t) | [nTime (uint32_t) |]
  //
  if (exMessage->size() != (isWithTime ? 19 : 15))
    return;

  const uint8_t *p = (uint8_t *)exMessage->data();
  const uint8_t shortJobId = *(uint8_t  *)(p +  4);
  const uint16_t sessionId = *(uint16_t *)(p +  5);
  if (sessionId > AGENT_MAX_SESSION_ID)
    return;

  const uint32_t  exNonce2 = *(uint32_t *)(p +  7);
  const uint32_t     nonce = *(uint32_t *)(p + 11);
  const uint32_t time = (isWithTime == false ? 0 : *(uint32_t *)(p + 15));

  const uint64_t fullExtraNonce2 = ((uint64_t)sessionId << 32) | (uint64_t)exNonce2;

  // debug
  string logLine = Strings::Format("[agent] shortJobId: %02x, sessionId: %08x"
                                   ", exNonce2: %016llx, nonce: %08x, time: %08x",
                                   shortJobId, (uint32_t)sessionId,
                                   fullExtraNonce2, nonce, time);
  DLOG(INFO) << logLine;

  if (stratumSession_ != nullptr)
    stratumSession_->handleRequest_Submit("null", shortJobId,
                                          fullExtraNonce2, nonce, time,
                                          true /* submit by agent's miner */,
                                          diffControllers_[sessionId]);
}

void AgentSessions::handleExMessage_UnRegisterWorker(const string *exMessage) {
  //
  // CMD_UNREGISTER_WORKER:
  // | magic_number(1) | cmd(1) | len (2) | session_id(2) |
  //
  if (exMessage->size() != 6)
    return;

  const uint8_t *p = (uint8_t *)exMessage->data();
  const uint16_t sessionId = *(uint16_t *)(p +  4);
  if (sessionId > AGENT_MAX_SESSION_ID)
    return;

  DLOG(INFO) << "[agent] sessionId: " << sessionId;

  // un-register worker
  workerIds_[sessionId] = 0;

  // set curr diff to default Diff
  curDiff2ExpVec_[sessionId] = kDefaultDiff2Exp_;

  // release diff controller
  if (diffControllers_[sessionId] != nullptr) {
    delete diffControllers_[sessionId];
    diffControllers_[sessionId] = nullptr;
  }
}

void AgentSessions::calcSessionsJobDiff(vector<uint8_t> &sessionsDiff2Exp) {
  sessionsDiff2Exp.clear();
  sessionsDiff2Exp.resize(UINT16_MAX, kDefaultDiff2Exp_);

  for (size_t i = 0; i < diffControllers_.size(); i++) {
    if (diffControllers_[i] == nullptr) {
      continue;
    }
    const uint64_t diff = diffControllers_[i]->calcCurDiff();
    sessionsDiff2Exp[i] = (uint8_t)log2(diff);
  }
}

void AgentSessions::getSessionsChangedDiff(const vector<uint8_t> &sessionsDiff2Exp,
                                           string &data) {
  vector<uint32_t> changedDiff2Exp;
  changedDiff2Exp.resize(UINT16_MAX, 0u);
  assert(curDiff2ExpVec_.size() == sessionsDiff2Exp.size());

  // get changed diff and set to new diff
  for (size_t i = 0; i < curDiff2ExpVec_.size(); i++) {
    if (curDiff2ExpVec_[i] == sessionsDiff2Exp[i]) {
      continue;
    }
    changedDiff2Exp[i] = sessionsDiff2Exp[i];
    curDiff2ExpVec_[i] = sessionsDiff2Exp[i];  // set new diff
  }

  // diff_2exp -> session_id | session_id | ... | session_id
  map<uint8_t, vector<uint16_t> > diffSessionIds;
  for (uint32_t i = 0; i < changedDiff2Exp.size(); i++) {
    if (changedDiff2Exp[i] == 0u) {
      continue;
    }
    diffSessionIds[changedDiff2Exp[i]].push_back((uint16_t)i);
  }

  getSetDiffCommand(diffSessionIds, data);
}

void AgentSessions::getSetDiffCommand(map<uint8_t, vector<uint16_t> > &diffSessionIds,
                                      string &data) {
  //
  // CMD_MINING_SET_DIFF:
  // | magic_number(1) | cmd(1) | len (2) | diff_2_exp(1) | count(2) | session_id (2) ... |
  //
  //
  // max session id count is 32,764, each message's max length is UINT16_MAX.
  //     65535 -1-1-2-1-2 = 65,528
  //     65,528 / 2 = 32,764
  //
  data.clear();
  const size_t kMaxCount = 32764;

  for (auto it = diffSessionIds.begin(); it != diffSessionIds.end(); it++) {

    while (it->second.size() > 0) {
      size_t count = std::min(kMaxCount, it->second.size());

      string buf;
      const uint16_t len = 1+1+2+1+2+ count * 2;
      buf.resize(len);
      uint8_t *p = (uint8_t *)buf.data();

      // cmd
      *p++ = CMD_MAGIC_NUMBER;
      *p++ = CMD_MINING_SET_DIFF;

      // len
      *(uint16_t *)p = len;
      p += 2;

      // diff, 2 exp
      *p++ = it->first;

      // count
      *(uint16_t *)p = (uint16_t)count;
      p += 2;

      // session ids
      for (size_t j = 0; j < count; j++) {
        *(uint16_t *)p = it->second[j];
        p += 2;
      }

      // remove the first `count` elements from vector
      it->second.erase(it->second.begin(), it->second.begin() + count);

      data.append(buf);
      
    } /* /while */
  } /* /for */
}
