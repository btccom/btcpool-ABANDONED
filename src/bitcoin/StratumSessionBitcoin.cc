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
#include "StratumSessionBitcoin.h"
#include "DiffController.h"

#include <event2/buffer.h>


///////////////////////////////// StratumSessionBitcoin ////////////////////////////////
StratumSessionBitcoin::StratumSessionBitcoin(evutil_socket_t fd, struct bufferevent *bev,
                ServerBitcoin *server, struct sockaddr *saddr,
                const int32_t shareAvgSeconds, const uint32_t extraNonce1)
                : StratumSessionBase(fd, bev, server, saddr, shareAvgSeconds, extraNonce1)
                , agentSessions_(nullptr)
{
  
}

StratumSessionBitcoin::~StratumSessionBitcoin()
{
   if (agentSessions_ != nullptr) {
    delete agentSessions_;
    agentSessions_ = nullptr;
  }
 
}

void StratumSessionBitcoin::handleRequest_Authorize(const string &idStr,
                                             const JsonNode &jparams, const JsonNode &/*jroot*/)
{
  if (state_ != SUBSCRIBED)
  {
    responseError(idStr, StratumStatus::NOT_SUBSCRIBED);
    return;
  }

  //
  //  params[0] = user[.worker]
  //  params[1] = password
  //  eg. {"params": ["slush.miner1", "password"], "id": 2, "method": "mining.authorize"}
  //  the password may be omitted.
  //  eg. {"params": ["slush.miner1"], "id": 2, "method": "mining.authorize"}
  //
  if (jparams.children()->size() < 1)
  {
    responseError(idStr, StratumStatus::INVALID_USERNAME);
    return;
  }

  string fullName = jparams.children()->at(0).str();
  string password;
  if (jparams.children()->size() > 1)
  {
    password = jparams.children()->at(1).str();
  }

  checkUserAndPwd(idStr, fullName, password);
}

void StratumSessionBitcoin::handleRequest_Subscribe(const string &idStr,
                                             const JsonNode &jparams) {
  if (state_ != CONNECTED) {
    responseError(idStr, StratumStatus::UNKNOWN);
    return;
  }


#ifdef WORK_WITH_STRATUM_SWITCHER

  //
  // For working with StratumSwitcher, the ExtraNonce1 must be provided as param 2.
  // 
  //  params[0] = client version           [require]
  //  params[1] = session id / ExtraNonce1 [require]
  //  params[2] = miner's real IP (unit32) [optional]
  //
  //  StratumSwitcher request eg.:
  //  {"id": 1, "method": "mining.subscribe", "params": ["StratumSwitcher/0.1", "01ad557d", 203569230]}
  //  203569230 -> 12.34.56.78
  //

  if (jparams.children()->size() < 2) {
    responseError(idStr, StratumStatus::ILLEGAL_PARARMS);
    return;
  }

  state_ = SUBSCRIBED;

  clientAgent_ = jparams.children()->at(0).str().substr(0, 30);  // 30 is max len
  clientAgent_ = filterWorkerName(clientAgent_);

  string extraNonce1Str = jparams.children()->at(1).str().substr(0, 8);  // 8 is max len
  sscanf(extraNonce1Str.c_str(), "%x", &extraNonce1_); // convert hex to int

  // receive miner's IP from stratumSwitcher
  if (jparams.children()->size() >= 3) {
    clientIpInt_ = htonl(jparams.children()->at(2).uint32());

    // ipv4
    clientIp_.resize(INET_ADDRSTRLEN);
    struct in_addr addr;
    addr.s_addr = clientIpInt_;
    clientIp_ = inet_ntop(AF_INET, &addr, (char *)clientIp_.data(), (socklen_t)clientIp_.size());
    LOG(INFO) << "client real IP: " << clientIp_;
  }

#else

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

#endif // WORK_WITH_STRATUM_SWITCHER


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
    isLongTimeout_ = true;
  }

  // check if it's NinceHash/x.x.x
  if (isNiceHashAgent(clientAgent_))
    isNiceHashClient_ = true;

  //
  // check if it's BTCAgent
  //
  if (strncmp(clientAgent_.c_str(), BTCCOM_MINER_AGENT_PREFIX,
              std::min(clientAgent_.length(), strlen(BTCCOM_MINER_AGENT_PREFIX))) == 0) {
    LOG(INFO) << "agent model, client: " << clientAgent_;
    agentSessions_ = new AgentSessions(shareAvgSeconds_, this);

    isLongTimeout_ = true;  // will set long timeout
  }
}

void StratumSessionBitcoin::handleRequest_Submit(const string &idStr,
                                          const JsonNode &jparams) {
  if (state_ != AUTHENTICATED) {
    responseError(idStr, StratumStatus::UNAUTHORIZED);

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
    responseError(idStr, StratumStatus::ILLEGAL_PARARMS);
    return;
  }

  uint8_t shortJobId;
  if (isNiceHashClient_) {
    shortJobId = (uint8_t)(jparams.children()->at(1).uint64() % 10);
  } else {
    shortJobId = (uint8_t)jparams.children()->at(1).uint32();
  }
  const uint64_t extraNonce2 = jparams.children()->at(2).uint64_hex();
  uint32_t nTime             = jparams.children()->at(3).uint32_hex();
  const uint32_t nonce       = jparams.children()->at(4).uint32_hex();

  handleRequest_Submit(idStr, shortJobId, extraNonce2, nonce, nTime,
                       false /* not agent session */, nullptr);
}

// TODO: remove goto.
void StratumSessionBitcoin::handleRequest_Submit(const string &idStr,
                                          const uint8_t shortJobId,
                                          const uint64_t extraNonce2,
                                          const uint32_t nonce,
                                          uint32_t nTime,
                                          bool isAgentSession,
                                          DiffController *sessionDiffController) {
  ServerBitcoin* serverBitcoin = GetServer();
  JobRepositoryBitcoin* jobRepoBitcoin = serverBitcoin->GetJobRepository();
  if(!serverBitcoin)
  {
    LOG(FATAL) << "StratumSession::handleRequest_Submit. cast ServerBitcoin failed";
  }

  //
  // if share is from agent session, we don't need to send reply json
  //
  if (isAgentSession == true && agentSessions_ == nullptr) {
    LOG(ERROR) << "can't find agentSession, worker: " << worker_.fullName_;
    return;
  }

  const string extraNonce2Hex = Strings::Format("%016llx", extraNonce2);
  assert(extraNonce2Hex.length()/2 == kExtraNonce2Size_);

  LocalJob *localJob = findLocalJob(shortJobId);
  if (localJob == nullptr) {
    // if can't find localJob, could do nothing
    if (isAgentSession == false) {
    	responseError(idStr, StratumStatus::JOB_NOT_FOUND);
    }
    
    LOG(INFO) << "rejected share: " << StratumStatus::toString(StratumStatus::JOB_NOT_FOUND)
    << ", worker: " << worker_.fullName_ << ", Share(id: " << idStr << ", shortJobId: "
    << (int)shortJobId << ", nTime: " << nTime << "/" << date("%F %T", nTime) << ")";
    return;
  }

  uint32_t height = 0;

  shared_ptr<StratumJobEx> exjob;
  exjob = jobRepoBitcoin->getStratumJobEx(localJob->jobId_);

  if (exjob.get() != NULL) {
    // 0 means miner use stratum job's default block time
    StratumJobBitcoin* sjobBitcoin = static_cast<StratumJobBitcoin*>(exjob->sjob_);
    if (nTime == 0) {
        nTime = sjobBitcoin->nTime_;
    }

    height = sjobBitcoin->height_;
  }

  ShareBitcoin share;
  share.version_      = ShareBitcoin::CURRENT_VERSION;
  share.jobId_        = localJob->jobId_;
  share.workerHashId_ = worker_.workerHashId_;
  share.userId_       = worker_.userId_;
  share.shareDiff_    = localJob->jobDifficulty_;
  share.blkBits_      = localJob->blkBits_;
  share.timestamp_    = (uint64_t)time(nullptr);
  share.height_       = height;
  share.nonce_        = nonce;
  share.sessionId_    = extraNonce1_;
  share.status_       = StratumStatus::REJECT_NO_REASON;
  share.ip_.fromIpv4Int(clientIpInt_);

  if (isAgentSession == true) {
    const uint16_t sessionId = (uint16_t)(extraNonce2 >> 32);

    // reset to agent session's workerId
    share.workerHashId_ = agentSessions_->getWorkerId(sessionId);
    if (share.workerHashId_ == 0) {
      LOG(ERROR) << "invalid workerId 0, sessionId: " << sessionId << ", worker: " << worker_.fullName_;
      return;
    }

    // reset to agent session's diff
    if (localJob->agentSessionsDiff2Exp_.size() < (size_t)sessionId + 1) {
      LOG(ERROR) << "can't find agent session's diff, sessionId: " << sessionId << ", worker: " << worker_.fullName_;
      return;
    }
    share.shareDiff_ = (uint64_t)exp2(localJob->agentSessionsDiff2Exp_[sessionId]);
  }

  // calc jobTarget
  uint256 jobTarget;
  DiffToTarget(share.shareDiff_, jobTarget);

  // we send share to kafka by default, but if there are lots of invalid
  // shares in a short time, we just drop them.
  bool isSendShareToKafka = true;

  LocalShare localShare(extraNonce2, nonce, nTime);

  // can't find local share
  if (!localJob->addLocalShare(localShare)) {
    share.status_ = StratumStatus::DUPLICATE_SHARE;

    if (isAgentSession == false) {
      responseError(idStr, share.status_);
    }

    // add invalid share to counter
    invalidSharesCounter_.insert((int64_t)time(nullptr), 1);

    goto finish;
  }

#ifdef  USER_DEFINED_COINBASE
  // check block header
  share.status_ = serverBitcoin->checkShare(share, extraNonce1_, extraNonce2Hex,
                                     nTime, nonce, jobTarget,
                                     worker_.fullName_,
                                     &localJob->userCoinbaseInfo_);
#else
  // check block header
  share.status_ = serverBitcoin->checkShare(share, extraNonce1_, extraNonce2Hex,
                                     nTime, nonce, jobTarget,
                                     worker_.fullName_);
#endif

  // accepted share
  if (StratumStatus::isAccepted(share.status_)) {

    // agent miner's diff controller
    if (isAgentSession && sessionDiffController != nullptr) {
      sessionDiffController->addAcceptedShare(share.shareDiff_);
    }

    if (isAgentSession == false) {
    	diffController_->addAcceptedShare(share.shareDiff_);
      responseTrue(idStr);
    }
  } else {
    // reject share
    if (isAgentSession == false) {
    	responseError(idStr, share.status_);
    }

    // add invalid share to counter
    invalidSharesCounter_.insert((int64_t)time(nullptr), 1);
  }


finish:
  DLOG(INFO) << share.toString();

  if (!StratumStatus::isAccepted(share.status_)) {
    
    // log all rejected share to answer "Why the rejection rate of my miner increased?"
    LOG(INFO) << "rejected share: " << StratumStatus::toString(share.status_)
    << ", worker: " << worker_.fullName_ << ", " << share.toString();

    // check if thers is invalid share spamming
    int64_t invalidSharesNum = invalidSharesCounter_.sum(time(nullptr),
                                                         INVALID_SHARE_SLIDING_WINDOWS_SIZE);
    // too much invalid shares, don't send them to kafka
    if (invalidSharesNum >= INVALID_SHARE_SLIDING_WINDOWS_MAX_LIMIT) {
      isSendShareToKafka = false;

      LOG(INFO) << "invalid share spamming, diff: "
      << share.shareDiff_ << ", worker: " << worker_.fullName_ << ", agent: "
      << clientAgent_ << ", ip: " << clientIp_;
    }
  }

  if (isSendShareToKafka) {
    share.checkSum_ = share.checkSum();
  	GetServer()->sendShare2Kafka((const uint8_t *)&share, sizeof(ShareBitcoin));
  }
  return;
}

void StratumSessionBitcoin::sendMiningNotify(shared_ptr<StratumJobEx> exJobPtrShared, bool isFirstJob) {
  StratumJobExBitcoin* exJobPtr = static_cast<StratumJobExBitcoin*>(exJobPtrShared.get());
  if (state_ < AUTHENTICATED || exJobPtr == nullptr) {
    return;
  }
  StratumJobBitcoin *sjob = dynamic_cast<StratumJobBitcoin*>(exJobPtr->sjob_);

  localJobs_.push_back(LocalJob());
  LocalJob &ljob = *(localJobs_.rbegin());
  ljob.blkBits_       = sjob->nBits_;
  ljob.jobId_         = sjob->jobId_;
  ljob.shortJobId_    = allocShortJobId();
  ljob.jobDifficulty_ = diffController_->calcCurDiff();

#ifdef USER_DEFINED_COINBASE
  // add the User's coinbaseInfo to the coinbase1's tail
  string userCoinbaseInfo = GetServer()->userInfo_->getCoinbaseInfo(worker_.userId_);
  ljob.userCoinbaseInfo_ = userCoinbaseInfo;
#endif

  if (agentSessions_ != nullptr)
  {
    // calc diff and save to ljob
    agentSessions_->calcSessionsJobDiff(ljob.agentSessionsDiff2Exp_);

    // get ex-message
    string exMessage;
    agentSessions_->getSessionsChangedDiff(ljob.agentSessionsDiff2Exp_, exMessage);
    if (exMessage.size())
    	sendData(exMessage);
  }

  // set difficulty
  if (currDiff_ != ljob.jobDifficulty_) {
    sendSetDifficulty(ljob.jobDifficulty_);
    currDiff_ = ljob.jobDifficulty_;
  }

  string notifyStr;
  notifyStr.reserve(2048);

  // notify1
  notifyStr.append(exJobPtr->miningNotify1_);

  // jobId
  if (isNiceHashClient_) {
    //
    // we need to send unique JobID to NiceHash Client, they have problems with
    // short Job ID
    //
    const uint64_t niceHashJobId = (uint64_t)time(nullptr) * 10 + ljob.shortJobId_;
    notifyStr.append(Strings::Format("% " PRIu64"", niceHashJobId));
  } else {
    notifyStr.append(Strings::Format("%u", ljob.shortJobId_));  // short jobId
  }

  // notify2
  notifyStr.append(exJobPtr->miningNotify2_);

  string coinbase1 = exJobPtr->coinbase1_;

#ifdef USER_DEFINED_COINBASE
  string userCoinbaseHex;
  Bin2Hex((const uint8_t *)ljob.userCoinbaseInfo_.c_str(), ljob.userCoinbaseInfo_.size(), userCoinbaseHex);
  // replace the last `userCoinbaseHex.size()` bytes to `userCoinbaseHex`
  coinbase1.replace(coinbase1.size()-userCoinbaseHex.size(), userCoinbaseHex.size(), userCoinbaseHex);
#endif

  // coinbase1
  notifyStr.append(coinbase1);

  // notify3
  if (isFirstJob)
  	notifyStr.append(exJobPtr->miningNotify3Clean_);
  else
    notifyStr.append(exJobPtr->miningNotify3_);

  sendData(notifyStr);  // send notify string

  // clear localJobs_
  clearLocalJobs();
}

bool StratumSessionBitcoin::handleRequest_Specific(const string &idStr, const string &method
                            , const JsonNode &jparams, const JsonNode &jroot)
{
  if (method == "mining.suggest_target")
  {
    handleRequest_SuggestTarget(idStr, jparams);
    return true;
  }
  return false;
}

void StratumSessionBitcoin::handleRequest_SuggestTarget(const string &idStr,
                                                 const JsonNode &jparams) {
  if (state_ != CONNECTED) {
    return;  // suggest should be call before subscribe
  }
  if (jparams.children()->size() == 0) {
    responseError(idStr, StratumStatus::ILLEGAL_PARARMS);
    return;
  }
  _handleRequest_SetDifficulty(TargetToDiff(jparams.children()->at(0).str()));
}

void StratumSessionBitcoin::handleExMessage_RegisterWorker(const string *exMessage) {
  if (agentSessions_ == nullptr) {
    return;
  }
  agentSessions_->handleExMessage_RegisterWorker(exMessage);
}

void StratumSessionBitcoin::handleExMessage_SubmitShare(const string *exMessage) {
  if (agentSessions_ == nullptr) {
    return;
  }
  // without timestamp
  agentSessions_->handleExMessage_SubmitShare(exMessage, false);
}

void StratumSessionBitcoin::handleExMessage_SubmitShareWithTime(const string *exMessage) {
  if (agentSessions_ == nullptr) {
    return;
  }
  // with timestamp
  agentSessions_->handleExMessage_SubmitShare(exMessage, true);
}

void StratumSessionBitcoin::handleExMessage_UnRegisterWorker(const string *exMessage) {
  if (agentSessions_ == nullptr) {
    return;
  }
  agentSessions_->handleExMessage_UnRegisterWorker(exMessage);
}


void StratumSessionBitcoin::handleExMessage_AuthorizeAgentWorker(const int64_t workerId,
                                                          const string &clientAgent,
                                                          const string &workerName) {
  if (state_ != AUTHENTICATED) {
    LOG(ERROR) << "curr stratum session has NOT auth yet";
    return;
  }
  GetServer()->userInfo_->addWorker(worker_.userId_, workerId,
                                workerName, clientAgent);
}
///////////////////////////////// AgentSessions ////////////////////////////////
AgentSessions::AgentSessions(const int32_t shareAvgSeconds,
                             StratumSessionBitcoin *stratumSession)
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

  // ptr can't be nullptr, just make it easy for test
  if (stratumSession_ == nullptr)
    return;

  // set sessionId -> workerId
  workerIds_[sessionId] = workerId;

  // deletes managed object
  if (diffControllers_[sessionId] != nullptr) {
    delete diffControllers_[sessionId];
    diffControllers_[sessionId] = nullptr;
  }

  // acquires new pointer
  assert(diffControllers_[sessionId] == nullptr);
  diffControllers_[sessionId] = new DiffController(stratumSession_->GetServer()->defaultDifficultyController_.get());

  // set curr diff to default Diff
  curDiff2ExpVec_[sessionId] = kDefaultDiff2Exp_;

  // submit worker info to stratum session
  stratumSession_->handleExMessage_AuthorizeAgentWorker(workerId, clientAgent,
                                                        workerName);
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
