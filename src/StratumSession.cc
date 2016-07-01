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

//////////////////////////////// StratumSession ////////////////////////////////
StratumSession::StratumSession(evutil_socket_t fd, struct bufferevent *bev,
                               Server *server, struct sockaddr *saddr)
: bev_(bev), fd_(fd), server_(server)
{
  state_ = CONNECTED;
  extraNonce1_ = 0U;

  // usually stratum job interval is 30~60 seconds, 10 is enough for miners
  kMaxNumLocalJobs_ = 10;

  inBuf_ = evbuffer_new();
  lastNoEOLPos_ = 0;

  clientAgent_ = "unknown";
  clientIp_.resize(INET_ADDRSTRLEN);
  struct sockaddr_in *saddrin = (struct sockaddr_in *)saddr;
  clientIp_ = inet_ntop(AF_INET, &saddrin->sin_addr,
                        (char *)clientIp_.data(), (socklen_t)clientIp_.size());
  setup();

  LOG(INFO) << "client connect, ip: " << clientIp_;
}

StratumSession::~StratumSession() {
  LOG(INFO) << "close stratum session, ip: " << clientIp_
  << ", name: \"" << worker_.fullName_ << "\"";

  if (inBuf_)
    evbuffer_free(inBuf_);

  if (bev_) {
    bufferevent_free(bev_);
    bev_ = NULL;
  }
}

void StratumSession::setup() {
  // TODO:
  // set extraNonce1_

  // fisrt we set 60 seconds, increase the timeout after auth & sub
  setReadTimeout(60);
}

//void StratumSession::close() {
//  if (bev_) {
//    bufferevent_free(bev_);
//    bev_ = NULL;
//  }
//}

void StratumSession::setReadTimeout(const int32_t timeout) {
  struct timeval tv = {timeout, 0};
  bufferevent_set_timeouts(bev_, &tv, NULL);
}

bool StratumSession::tryReadLine(string &line) {
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

void StratumSession::handleLine(const string &line) {
  string hex;
  Bin2Hex((uint8_t *)line.data(), line.size(), hex);
  LOG(INFO) << "dump line, hex: " << hex << ", line: " << line << ", size: " << line.size();

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
  // {"id": 10, "result": null, "error": (21, "Job not found", null)}
  //
  char buf[1024];
  int len = snprintf(buf, sizeof(buf),
                     "{\"id\":%s,\"result\":null,\"error\":(%d,\"%s\",null)}\n",
                     idStr.empty() ? "null" : idStr.c_str(),
                     errCode, StratumError::toString(errCode));
  send(buf, len);
}

void StratumSession::responseTrue(const string &idStr) {
  const string s = "{\"id\":" + idStr + ",\"result\": true,\"error\":null}\n";
  send(s);
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
  }
  else if (method == "mining.suggest_difficulty") {
    handleRequest_SuggestDifficulty(idStr, jparams);
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
  //  {"id": 1, "method": "mining.subscribe", "params": ["bfgminer/4.4.0-32-gac4e9b3", "01ad557d"]}
  //
  if (jparams.children()->size() == 1) {
    clientAgent_ = jparams.children()->at(0).str().substr(0, 30);  // 30 is max len
  }

  //  result[0] = 2-tuple with name of subscribed notification and subscription ID.
  //              Theoretically it may be used for unsubscribing, but obviously miners won't use it.
  //  result[1] = ExtraNonce1, used for building the coinbase.
  //  result[2] = Extranonce2_size, the number of bytes that the miner users for its ExtraNonce2 counter
  assert(kExtraNonce2Size_ == 8);
  const string s = Strings::Format("{\"id\":%s,\"result\":[[[\"mining.set_difficulty\",\"%08x\"]"
                                   ",[\"mining.notify\",\"%08x\"]],\"%08x\",%d],\"error\":null}\n",
                                   idStr.c_str(), extraNonce1_, extraNonce1_, extraNonce1_, kExtraNonce2Size_);
  send(s);
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

  // TODO: get user ID from user name
  const int32_t userId = 1;
  if (userId == 0) {
    responseError(idStr, StratumError::INVALID_USERNAME);
    return;
  }

  // auth success
  responseTrue(idStr);
  worker_.setUserIDAndNames(userId, fullName);
  state_ = AUTHENTICATED;

  // set read timeout to 15 mins, it's enought for most miners even usb miner
  setReadTimeout(60*15);

  // send latest stratum job
  sendMiningNotify(server_->jobRepository_->getLatestStratumJobEx());
}

void StratumSession::_handleRequest_SetDifficulty(uint64_t suggestDiff) {
  double i = 1;  // 2^10 -> 1024
  while ((uint64_t)exp2(i) < suggestDiff) {
    i++;
  }
  suggestDiff = (uint64_t)exp2(i);

  // TODO:
  //  diffController_.resetCurDiff(suggestDiff);
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
    send(s);

    return;
  }

  //  params[0] = Worker Name
  //  params[1] = Job ID
  //  params[2] = ExtraNonce 2
  //  params[3] = nTime
  //  params[4] = nonce
  if (jparams.children()->size() != 5) {
    responseError(idStr, StratumError::ILLEGAL_PARARMS);
    return;
  }
  const uint64 jobId       = jparams.children()->at(1).uint64();
  const uint64 extraNonce2 = jparams.children()->at(2).uint64_hex();
  string extraNonce2Hex    = jparams.children()->at(2).str();
  const uint32 nTime       = jparams.children()->at(3).uint32_hex();
  const uint32 nonce       = jparams.children()->at(4).uint32_hex();

  if (extraNonce2Hex.length()/2 > kExtraNonce2Size_) {
    extraNonce2Hex.resize(kExtraNonce2Size_ * 2);
  }
  LocalJob *localJob = findLocalJob(jobId);
  if (localJob == nullptr) {
    responseError(idStr, StratumError::JOB_NOT_FOUND);
    return;
  }

  LocalShare localShare(extraNonce2, nonce, nTime);
  if (!localJob->addLocalShare(localShare)) {
    responseError(idStr, StratumError::DUPLICATE_SHARE);
    return;
  }

  int result = server_->submitShare(jobId, extraNonce1_, extraNonce2Hex, nTime,
                                    nonce, localJob->jobTarget_, worker_.fullName_);
  if (result != StratumError::NO_ERROR) {
    responseError(idStr, result);
    return;
  }

  // accept share
  responseTrue(idStr);
}

StratumSession::LocalJob *StratumSession::findLocalJob(uint64_t jobId) {
  for (auto rit = localJobs_.rbegin(); rit != localJobs_.rend(); ++rit) {
    if (rit->jobId_ == jobId) {
      return &(*rit);
    }
  }
  return nullptr;
}

void StratumSession::sendMiningNotify(shared_ptr<StratumJobEx> exJobPtr) {
  if (state_ < SUBSCRIBED || exJobPtr == nullptr) {
    return;
  }
  StratumJob *sjob = exJobPtr->sjob_;

  localJobs_.push_back(LocalJob());
  LocalJob &ljob = *(localJobs_.rbegin());
  ljob.jobId_ = sjob->jobId_;
// TODO:
//  ljob.jobDifficulty_ =
  DiffToTarget(ljob.jobDifficulty_, ljob.jobTarget_);

  send(exJobPtr->miningNotify_);
}

void StratumSession::send(const char *data, size_t len) {
  ScopeLock sl(writeLock_);

  // add data to a bufferevent’s output buffer
  bufferevent_write(bev_, data, len);

  LOG(INFO) << "send: " << data;
}

void StratumSession::readBuf(struct evbuffer *buf) {
  // moves all data from src to the end of dst
  evbuffer_add_buffer(inBuf_, buf);

  string line;
  while (tryReadLine(line)) {
    handleLine(line);
  }
}
