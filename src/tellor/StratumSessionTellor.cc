#include "StratumSessionTellor.h"

#include "StratumMinerTellor.h"
#include "StratumServerTellor.h"
#include "DiffController.h"

StratumSessionTellor::StratumSessionTellor(
    StratumServerTellor &server,
    struct bufferevent *bev,
    struct sockaddr *saddr,
    uint32_t sessionId)
  : StratumSessionBase{server, bev, saddr, sessionId}
  , currentDifficulty_{0} {
}

void StratumSessionTellor::sendSetDifficulty(
    LocalJob &localJob, uint64_t difficulty) {
  currentDifficulty_ = difficulty;
  DLOG(INFO) << "current difficulty : " << currentDifficulty_;
}

void StratumSessionTellor::sendMiningNotify(
    shared_ptr<StratumJobEx> exJobPtr, bool isFirstJob) {
  if (state_ < AUTHENTICATED || exJobPtr == nullptr) {
    LOG(ERROR) << "sendMiningNotify failed, state = " << state_;
    return;
  }

  auto jobTellor = std::static_pointer_cast<StratumJobTellor>(exJobPtr->sjob_);
  if (nullptr == jobTellor) {
    LOG(ERROR) << "Invalid job type, jobId = " << exJobPtr->sjob_->jobId_;
    return;
  }
  // set session
  curNetWorkDiff_ = jobTellor->difficulty_;

  auto ljob = findLocalJob(jobTellor->jobId_);
  // create a new LocalJobBeam if not exists

  if (ljob == nullptr) {
    ljob = &addLocalJob(exJobPtr->chainId_, jobTellor->jobId_);
  } else {
    dispatcher_->addLocalJob(*ljob);
    // update the job id to the latest one
    ljob->jobId_ = jobTellor->jobId_;
  }

  auto notifyStr = Strings::Format(
      "{\"id\":null,\"method\":\"mining.notify\","
      "\"params\":[\"%" PRIx64
      "\",\"%s\",\"%s\","
      "%" PRIu64
      ""
      ",%s]}\n",
      jobTellor->jobId_,
      jobTellor->challenge_.c_str(),
      jobTellor->publicAddress_.c_str(),
      currentDifficulty_,
      exJobPtr->isClean_ ? "true" : "false");
  // if (exJobPtr->isClean_)
  //   exJobPtr->isClean_ = false;
  // trb producce one task at each height, after we have sent task to miner ,
  // don't clean job again

  DLOG(INFO) << "send mining notify" << notifyStr;

  sendData(notifyStr);
  // clear localJobs_
  clearLocalJobs(exJobPtr->isClean_);
}

std::unique_ptr<StratumMiner> StratumSessionTellor::createMiner(
    const std::string &clientAgent,
    const std::string &workerName,
    int64_t workerId) {
  return std::make_unique<StratumMinerTellor>(
      *this,
      *getServer().defaultDifficultyController_,
      clientAgent,
      workerName,
      workerId);
}

bool StratumSessionTellor::validate(
    const JsonNode &jmethod, const JsonNode &jparams, const JsonNode &jroot) {
  if (jmethod.type() == Utilities::JS::type::Str && jmethod.size() != 0) {
    return true;
  }
  return false;
}

void StratumSessionTellor::handleRequest(
    const std::string &idStr,
    const std::string &method,
    const JsonNode &jparams,
    const JsonNode &jroot) {
  if (method == "mining.subscribe") {
    handleRequest_Subscribe(idStr, jparams);
  } else if (method == "mining.authorize") {
    handleRequest_Authorize(idStr, jparams);
  } else {
    dispatcher_->handleRequest(idStr, method, jparams, jroot);
  }
}

void StratumSessionTellor::handleRequest_Subscribe(
    const string &idStr, const JsonNode &jparams) {
  if (state_ != CONNECTED) {
    responseError(idStr, StratumStatus::UNKNOWN);
    return;
  }
  DLOG(INFO) << "receive handleRequest_Subscribe jparams : " << jparams;

  state_ = SUBSCRIBED;
  //{"id":1,"method":"mining.subscribe","params":["Tellorminer-v1.0.0",null,"Tellor.uupool.cn","10861"]}
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

void StratumSessionTellor::handleRequest_Authorize(
    const string &idStr, const JsonNode &jparams) {
  DLOG(INFO) << "receive handleRequest_Authorize jparams : " << jparams;
  if (state_ != SUBSCRIBED) {
    responseError(idStr, StratumStatus::NOT_SUBSCRIBED);
    return;
  }

  if (jparams.children()->size() < 1) {
    responseError(idStr, StratumStatus::INVALID_USERNAME);
    return;
  }

  string fullName, password;

  fullName = jparams.children()->at(0).str();
  if (jparams.children()->size() > 1) {
    password = jparams.children()->at(1).str();
  }

  checkUserAndPwd(idStr, fullName, password);
  return;
}
