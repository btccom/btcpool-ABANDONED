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

#include "StratumServer.h"


//////////////////////////////// DiffController ////////////////////////////////
void DiffController::setMinDiff(uint64 minDiff) {
  if (minDiff < kMinDiff_) {
    minDiff = kMinDiff_;
  }
  minDiff_ = minDiff;
}

void DiffController::resetCurDiff(uint64 curDiff) {
  if (curDiff < kMinDiff_) {
    curDiff = kMinDiff_;
  }
  if (curDiff < minDiff_) {
    curDiff = minDiff_;
  }

  // set to zero
  sharesNum_.mapMultiply(0);
  shares_.mapMultiply(0);

  curDiff_ = curDiff;
}

void DiffController::addAcceptedShare(const uint64 share) {
  const int64 k = time(nullptr) / kRecordSeconds_;
  sharesNum_.insert(k, 1.0);
  shares_.insert(k, share);
}


//
// level:  min ~ max, coefficient
//
// 0 :    0 ~    4 T,  1.0
// 1 :    4 ~    8 T,  1.0
// 2 :    8 ~   16 T,  1.0
// 3 :   16 ~   32 T,  1.2
// 4 :   32 ~   64 T,  1.5
// 5 :   64 ~  128 T,  2.0
// 6 :  128 ~  256 T,  3.0
// 7 :  256 ~  512 T,  4.0
// 8 :  512 ~  ... T,  6.0
//

static int __hashRateDown(int level) {
  const int levels[] = {0, 4, 8, 16,   32, 64, 128, 256};
  if (level >= 8) {
    return 512;
  }
  assert(level >= 0 && level <= 7);
  return levels[level];
}

static int __hashRateUp(int level) {
  const int levels[] = {4, 8, 16, 32,   64, 128, 256, 512};
  assert(level >= 0 && level <= 7);
  if (level >= 8) {
    return 0x7fffffffL;  // INT32_MAX
  }
  return levels[level];
}

// TODO: test case
int DiffController::adjustHashRateLevel(const double hashRateT) {
  // hashrate is always danceing,
  // so need to use rate high and low to check it's level
  const double rateHigh = 1.50;
  const double rateLow  = 0.75;

  // reduce level
  if (curHashRateLevel_ > 0 && hashRateT < __hashRateDown(curHashRateLevel_)) {
    while (curHashRateLevel_ > 0 &&
           hashRateT <= __hashRateDown(curHashRateLevel_) * rateLow) {
      curHashRateLevel_--;
    }
    return curHashRateLevel_;
  }

  // increase level
  if (curHashRateLevel_ <= 7 && hashRateT > __hashRateUp(curHashRateLevel_)) {
    while (curHashRateLevel_ <= 7 &&
           hashRateT >= __hashRateUp(curHashRateLevel_) * rateHigh) {
      curHashRateLevel_++;
    }
    return curHashRateLevel_;
  }

  return curHashRateLevel_;
}

double DiffController::minerCoefficient(const time_t now, const int64_t idx) {
  if (now <= startTime_) {
    return 1.0;
  }
  uint64_t shares    = shares_.sum(idx);
  time_t shareWindow = isFullWindow(now) ? kDiffWindow_ : (now - startTime_);
  double hashRateT   = (double)shares * pow(2, 32) / shareWindow / pow(10, 12);
  adjustHashRateLevel(hashRateT);
  assert(curHashRateLevel_ >= 0 && curHashRateLevel_ <= 8);

  const double c[] = {1.0, 1.0, 1.0, 1.2, 1.5, 2.0, 3.0, 4.0, 6.0};
  assert(sizeof(c)/sizeof(c[0]) == 9);
  return c[curHashRateLevel_];
}

uint64 DiffController::calcCurDiff() {
  uint64 diff = _calcCurDiff();
  if (diff < minDiff_) {
    diff = minDiff_;
  }
  return diff;
}

uint64 DiffController::_calcCurDiff() {
  const time_t now = time(nullptr);
  const int64 k = now / kRecordSeconds_;
  const double sharesCount = (double)sharesNum_.sum(k);
  if (startTime_ == 0) {  // first time, we set the start time
    startTime_ = time(nullptr);
  }

  const double kRateHigh = 1.40;
  const double kRateLow  = 0.40;
  double expectedCount = round(kDiffWindow_ / (double)shareAvgSeconds_);

  if (isFullWindow(now)) { /* have a full window now */
    // big miner have big expected share count to make it looks more smooth.
    expectedCount *= minerCoefficient(now, k);
  }
  if (expectedCount > kDiffWindow_) {
    expectedCount = kDiffWindow_;  // one second per share is enough
  }

  // this is for very low hashrate miner, eg. USB miners
  // should received at least one share every 60 seconds
  if (!isFullWindow(now) && now >= startTime_ + 60 &&
      sharesCount <= (int32_t)((now - startTime_)/60.0) &&
      curDiff_ >= minDiff_*2) {
    curDiff_ /= 2;
    sharesNum_.mapMultiply(2.0);
    return curDiff_;
  }

  // too fast
  if (sharesCount > expectedCount * kRateHigh) {
    while (sharesNum_.sum(k) > expectedCount) {
      curDiff_ *= 2;
      sharesNum_.mapDivide(2.0);
    }
    return curDiff_;
  }

  // too slow
  if (isFullWindow(now) && curDiff_ >= minDiff_*2) {
    while (sharesNum_.sum(k) < expectedCount * kRateLow &&
           curDiff_ >= minDiff_*2) {
      curDiff_ /= 2;
      sharesNum_.mapMultiply(2.0);
    }
    assert(curDiff_ >= minDiff_);
    return curDiff_;
  }
  
  return curDiff_;
}



//////////////////////////////// StratumSession ////////////////////////////////
StratumSession::StratumSession(evutil_socket_t fd, struct bufferevent *bev,
                               Server *server, struct sockaddr *saddr,
                               const int32_t shareAvgSeconds) :
diffController_(shareAvgSeconds),
shortJobIdIdx_(0), bev_(bev), fd_(fd), server_(server)
{
  state_ = CONNECTED;
  currDiff_    = 0U;
  extraNonce1_ = 0u;

  // usually stratum job interval is 30~60 seconds, 10 is enough for miners
  // should <= 10, we use short_job_id,  range: [0 ~ 9]. do NOT change it.
  kMaxNumLocalJobs_ = 10;
  assert(kMaxNumLocalJobs_ <= 10);

  inBuf_  = evbuffer_new();
  isPoolWatcher_ = false;

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
  // free session id
  server_->sessionIDManager_->freeSessionId(extraNonce1_);

  LOG(INFO) << "close stratum session, ip: " << clientIp_
  << ", name: \"" << worker_.fullName_ << "\"";

//  close(fd_);  // we don't need to close because we set 'BEV_OPT_CLOSE_ON_FREE'
  evbuffer_free(inBuf_);
  bufferevent_free(bev_);
}

void StratumSession::setup() {
  // alloc session id
  extraNonce1_ = server_->sessionIDManager_->allocSessionId();
  assert(extraNonce1_ != 0u);

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
  else if (method == "mining.multi_version") {
    handleRequest_MultiVersion(idStr, jparams);
  }
  else if (method == "mining.suggest_target") {
    handleRequest_SuggestTarget(idStr, jparams);
  }
  else if (method == "mining.suggest_difficulty") {
    handleRequest_SuggestDifficulty(idStr, jparams);
  } else {
    // unrecognised method, just ignore it
    LOG(WARNING) << "unrecognised method: \"" << method << "\""
    << ", client: " << clientIp_ << "/" << clientAgent_;
  }
}

void StratumSession::handleRequest_MultiVersion(const string &idStr,
                                                const JsonNode &jparams) {
//  // we ignore right now, 2016-07-04
//  const string s = Strings::Format("{\"id\":%s,\"method\":\"mining.midstate_change\",\"params\":[4]}",
//                                   idStr.c_str());
//  sendData(s);
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
  //  {"id": 1, "method": "mining.subscribe", "params": ["bfgminer/4.4.0-32-gac4e9b3", "01ad557d"]}
  //
  if (jparams.children()->size() >= 1) {
    clientAgent_ = jparams.children()->at(0).str().substr(0, 30);  // 30 is max len
    clientAgent_ = filterWorkerName(clientAgent_);
  }

  //  result[0] = 2-tuple with name of subscribed notification and subscription ID.
  //              Theoretically it may be used for unsubscribing, but obviously miners won't use it.
  //  result[1] = ExtraNonce1, used for building the coinbase.
  //  result[2] = Extranonce2_size, the number of bytes that the miner users for its ExtraNonce2 counter
  assert(kExtraNonce2Size_ == 8);
  const string s = Strings::Format("{\"id\":%s,\"result\":[[[\"mining.set_difficulty\",\"%08x\"]"
                                   ",[\"mining.notify\",\"%08x\"]],\"%08x\",%d],\"error\":null}\n",
                                   idStr.c_str(), extraNonce1_, extraNonce1_, extraNonce1_, kExtraNonce2Size_);
  sendData(s);

  if (clientAgent_ == "__PoolWatcher__") {
    isPoolWatcher_ = true;
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
  //  eg. {"params": ["slush.miner1", "password"], "id": 2, "method": "mining.authorize"}
  //
  if (jparams.children()->size() < 1) {
    responseError(idStr, StratumError::INVALID_USERNAME);
    return;
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
  setReadTimeout(isPoolWatcher_ ? 86400*7 : 60*10);

  // send latest stratum job
  sendMiningNotify(server_->jobRepository_->getLatestStratumJobEx());
}

void StratumSession::_handleRequest_SetDifficulty(uint64_t suggestDiff) {
  // suggestDiff must be 2^N
  double i = 1;
  while ((uint64_t)exp2(i) < suggestDiff) {
    i++;
  }
  suggestDiff = (uint64_t)exp2(i);

  diffController_.resetCurDiff(suggestDiff);
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
  _handleRequest_SetDifficulty(TargetToPdiff(jparams.children()->at(0).str()));
}

void StratumSession::handleRequest_SuggestDifficulty(const string &idStr,
                                                     const JsonNode &jparams) {
  if (state_ != CONNECTED) {
    return;  // suggest should be call before subscribe
  }
  if (jparams.children()->size() == 0) {
    responseError(idStr, StratumError::ILLEGAL_PARARMS);
    return;
  }
  _handleRequest_SetDifficulty(jparams.children()->at(0).uint64());
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

  //  params[0] = Worker Name
  //  params[1] = Job ID
  //  params[2] = ExtraNonce 2
  //  params[3] = nTime
  //  params[4] = nonce
  if (jparams.children()->size() < 5) {
    responseError(idStr, StratumError::ILLEGAL_PARARMS);
    return;
  }
  const uint8_t shortJobId   = (uint8_t)jparams.children()->at(1).uint32();
  const uint64_t extraNonce2 = jparams.children()->at(2).uint64_hex();
  string extraNonce2Hex      = jparams.children()->at(2).str();
  const uint32_t nTime       = jparams.children()->at(3).uint32_hex();
  const uint32_t nonce       = jparams.children()->at(4).uint32_hex();

  if (extraNonce2Hex.length()/2 > kExtraNonce2Size_) {
    extraNonce2Hex.resize(kExtraNonce2Size_ * 2);
  }

  LocalJob *localJob = findLocalJob(shortJobId);
  if (localJob == nullptr) {
    // if can't find localJob, could do nothing
    responseError(idStr, StratumError::JOB_NOT_FOUND);
    return;
  }

  Share share;
  share.jobId_        = localJob->jobId_;
  share.workerHashId_ = worker_.workerHashId_;
  share.ip_           = clientIpInt_;
  share.userId_       = worker_.userId_;
  share.share_        = localJob->jobDifficulty_;
  share.blkBits_      = localJob->blkBits_;
  share.timestamp_    = (uint32_t)time(nullptr);
  share.result_       = Share::Result::REJECT;

  int submitResult;

  LocalShare localShare(extraNonce2, nonce, nTime);
  if (!localJob->addLocalShare(localShare)) {
    responseError(idStr, StratumError::DUPLICATE_SHARE);
    goto finish;
  }

  submitResult = server_->checkShare(share, extraNonce1_, extraNonce2Hex,
                                     nTime, nonce, localJob->jobTarget_,
                                     worker_.fullName_);
  if (submitResult == StratumError::NO_ERROR) {
    // accepted share
    share.result_ = Share::Result::ACCEPT;
    diffController_.addAcceptedShare(localJob->jobDifficulty_);
    responseTrue(idStr);
  } else {
    responseError(idStr, submitResult);
  }

finish:
  DLOG(INFO) << share.toString();
  server_->sendShare2Kafka((const uint8_t *)&share, sizeof(Share));
  return;
}

StratumSession::LocalJob *StratumSession::findLocalJob(uint8_t shortJobId) {
  for (auto rit = localJobs_.rbegin(); rit != localJobs_.rend(); ++rit) {
    if (rit->shortJobId_ == shortJobId) {
      return &(*rit);
    }
  }
  return nullptr;
}

void StratumSession::sendSetDifficulty(const uint64_t difficulty) {
  string s = Strings::Format("{\"id\":null,\"method\":\"mining.set_difficulty\""
                             ",\"params\":[%" PRIu64"]}\n",
                             difficulty);
  sendData(s);
}

uint8_t StratumSession::allocShortJobId() {
  // return range: [0, 9]
  if (shortJobIdIdx_ == 10) {
    shortJobIdIdx_ = 0;
  }
  return shortJobIdIdx_++;
}

void StratumSession::sendMiningNotify(shared_ptr<StratumJobEx> exJobPtr) {
  if (state_ < SUBSCRIBED || exJobPtr == nullptr) {
    return;
  }
  StratumJob *sjob = exJobPtr->sjob_;

  localJobs_.push_back(LocalJob());
  LocalJob &ljob = *(localJobs_.rbegin());
  ljob.blkBits_       = sjob->nBits_;
  ljob.jobId_         = sjob->jobId_;
  ljob.shortJobId_    = allocShortJobId();
  ljob.jobDifficulty_ = diffController_.calcCurDiff();
  DiffToTarget(ljob.jobDifficulty_, ljob.jobTarget_);

  if (currDiff_ != ljob.jobDifficulty_) {
    sendSetDifficulty(ljob.jobDifficulty_);
    currDiff_ = ljob.jobDifficulty_;
  }

  sendData(exJobPtr->miningNotify1_);
  sendData(Strings::Format("%u", ljob.shortJobId_));  // short jobId
  sendData(exJobPtr->miningNotify2_);

  // clear localJobs_
  while (localJobs_.size() > kMaxNumLocalJobs_) {
    localJobs_.pop_front();
  }
}

void StratumSession::sendData(const char *data, size_t len) {
  // add data to a bufferevent’s output buffer
  bufferevent_lock(bev_);
  bufferevent_write(bev_, data, len);
  bufferevent_unlock(bev_);
//  DLOG(INFO) << "send(" << len << "): " << data;
}

void StratumSession::readBuf(struct evbuffer *buf) {
  // moves all data from src to the end of dst
  evbuffer_add_buffer(inBuf_, buf);

  string line;
  while (tryReadLine(line)) {
    handleLine(line);
  }
}
