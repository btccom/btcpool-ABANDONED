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
#include "StratumSessionCkb.h"

#include "StratumMinerCkb.h"
#include "StratumServerCkb.h"
#include "DiffController.h"

StratumSessionCkb::StratumSessionCkb(
    StratumServerCkb &server,
    struct bufferevent *bev,
    struct sockaddr *saddr,
    uint32_t sessionId)
  : StratumSessionBase{server, bev, saddr, sessionId}
  , currentDifficulty_{0} {
}

void StratumSessionCkb::sendSetDifficulty(
    LocalJob &localJob, uint64_t difficulty) {
  //{"id":null,"method":"mining.set_difficulty","params":[1.999969]}
  currentDifficulty_ = difficulty;
  DLOG(INFO) << "difficulty : " << difficulty;
  uint256 jobtarget;
  CkbDifficulty::DiffToTarget(currentDifficulty_, jobtarget);

  string strNotify = Strings::Format(
      "{\"id\":null,\"method\":\"mining.set_target\","
      "\"params\":[\"%s\"]"
      "}\n",
      UintToArith256(jobtarget).GetHex().c_str());

  DLOG(INFO) << "current difficulty : " << currentDifficulty_
             << " target : " << UintToArith256(jobtarget).GetHex().c_str();

  sendData(strNotify);
}

void StratumSessionCkb::sendMiningNotify(
    shared_ptr<StratumJobEx> exJobPtr, bool isFirstJob) {
  if (state_ < AUTHENTICATED || exJobPtr == nullptr) {
    LOG(ERROR) << "sendMiningNotify failed, state = " << state_;
    return;
  }

  auto jobckb = std::static_pointer_cast<StratumJobCkb>(exJobPtr->sjob_);
  if (nullptr == jobckb) {
    LOG(ERROR) << "Invalid job type, jobId = " << exJobPtr->sjob_->jobId_;
    return;
  }

  auto ljob = findLocalJob(jobckb->jobId_);
  // create a new LocalJobBeam if not exists

  if (ljob == nullptr) {
    ljob = &addLocalJob(exJobPtr->chainId_, jobckb->jobId_);
  } else {
    dispatcher_->addLocalJob(*ljob);
    // update the job id to the latest one
    ljob->jobId_ = jobckb->jobId_;
  }

  auto notifyStr = Strings::Format(
      "{\"id\":null,\"method\":\"mining.notify\","
      "\"params\":[\"%" PRIx64 "\",\"%s\",%d,\"%s\",%s]}\n",
      jobckb->jobId_,
      HexStripPrefix(jobckb->pow_hash_),
      jobckb->height_,
      HexStripPrefix(jobckb->parent_hash_),
      exJobPtr->isClean_ ? "true" : "false");

  DLOG(INFO) << "send mining notify" << notifyStr;

  sendData(notifyStr);
  // clear localJobs_
  clearLocalJobs(exJobPtr->isClean_);
}

std::unique_ptr<StratumMiner> StratumSessionCkb::createMiner(
    const std::string &clientAgent,
    const std::string &workerName,
    int64_t workerId) {
  return std::make_unique<StratumMinerCkb>(
      *this,
      *getServer().defaultDifficultyController_,
      clientAgent,
      workerName,
      workerId);
}

bool StratumSessionCkb::validate(
    const JsonNode &jmethod, const JsonNode &jparams, const JsonNode &jroot) {
  if (jmethod.type() == Utilities::JS::type::Str && jmethod.size() != 0) {
    return true;
  }
  return false;
}

void StratumSessionCkb::handleRequest(
    const std::string &idStr,
    const std::string &method,
    const JsonNode &jparams,
    const JsonNode &jroot) {
  if (method == "mining.subscribe") {
    handleRequest_Subscribe(idStr, jparams);
  } else if (method == "mining.extranonce.subscribe") {
    handleRequest_Extranonce_Subscribe(idStr, jparams);
  } else if (method == "mining.authorize") {
    handleRequest_Authorize(idStr, jparams);
  } else {
    dispatcher_->handleRequest(idStr, method, jparams, jroot);
  }
}

void StratumSessionCkb::handleRequest_Extranonce_Subscribe(
    const string &idStr, const JsonNode &jparams) {
  // const string s = Strings::Format (
  //     "{\"id\": %s, \"result\": false, \"error\": [20, \"Not supported.\",
  //     null]}\n", idStr.c_str());
  // LOG(INFO) << "sendmining  :" << s;

  // uint256 jobtarget;
  // CkbDifficulty::DiffToTarget(currentDifficulty_, jobtarget);

  // string s = Strings::Format(
  //     "{\"id\":null,\"method\":\"mining.set_target\","
  //     "\"params\":[\"%s\"]"
  //     "}\n",
  //     UintToArith256(jobtarget).GetHex().c_str());

  // sendData(s);
}

void StratumSessionCkb::handleRequest_Subscribe(
    const string &idStr, const JsonNode &jparams) {
  if (state_ != CONNECTED) {
    responseError(idStr, StratumStatus::UNKNOWN);
    return;
  }
  DLOG(INFO) << "receive handleRequest_Subscribe jparams : " << jparams;

  state_ = SUBSCRIBED;
  //{"id":1,"method":"mining.subscribe","params":["ckbminer-v1.0.0",null,"ckb.uupool.cn","10861"]}
  if (jparams.children()->size() >= 1) {
    setClientAgent(
        jparams.children()->at(0).str().substr(0, 30)); // 30 is max len
  }

  //{"id":1,"result":[null,"3e29d5", 5],"error":null}// nonce1, size of nonce2
  const string s = Strings::Format(
      "{\"id\":%s,\"result\":[\"null\",\"%08x\",%d],\"error\":null}\n",
      idStr.c_str(),
      sessionId_,
      12);
  DLOG(INFO) << "send mining  :" << s;
  sendData(s);
}

void StratumSessionCkb::handleRequest_Authorize(
    const string &idStr, const JsonNode &jparams) {
  DLOG(INFO) << "receive handleRequest_Authorize jparams : " << jparams;
  if (state_ != SUBSCRIBED) {
    responseError(idStr, StratumStatus::NOT_SUBSCRIBED);
    return;
  }

  //{"id":2,"method":"mining.authorize","params":["ckb1qyq2znu0gempdahctxsm49sa9jdzq9vnka7qt9ntff.worker1","x"]}//
  // params: [username, password]
  //{"id":2,"result":true,"error":null}
  if (jparams.children()->size() < 1) {
    responseError(idStr, StratumStatus::INVALID_USERNAME);
    return;
  }

  string fullName, password;

  fullName = jparams.children()->at(0).str();
  if (jparams.children()->size() > 1) {
    password = jparams.children()->at(1).str();
  }

  if (StratumServerMiningModel::ANONYMOUS == server_.miningModel_ &&
      fullName.length() >= 32 &&
      (fullName.substr(0, 3) == "ckt" || fullName.substr(0, 3) == "ckb")) {
    AnonymousAuthorize(idStr, fullName, password);
  } else {
    checkUserAndPwd(idStr, fullName, password);
  }
  return;
}
