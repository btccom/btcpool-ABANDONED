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
#include "StratumMinerBitcoin.h"

#include "StratumSessionBitcoin.h"
#include "StratumMessageDispatcher.h"
#include "DiffController.h"

#include <boost/make_unique.hpp>
#include <event2/buffer.h>

///////////////////////////////// StratumMinerBitcoin ////////////////////////////////
StratumMinerBitcoin::StratumMinerBitcoin(StratumSessionBitcoin &session,
                                         const DiffController &diffController,
                                         const string &clientAgent,
                                         const string &workerName,
                                         int64_t workerId)
    : StratumMinerBase(session, diffController, clientAgent, workerName, workerId) {
}

void StratumMinerBitcoin::handleRequest(const string &idStr,
                                        const string &method,
                                        const JsonNode &jparams,
                                        const JsonNode &jroot) {
  if (method == "mining.submit") {
    handleRequest_Submit(idStr, jparams);
  } else if (method == "mining.suggest_target") {
    handleRequest_SuggestTarget(idStr, jparams);
  }
}

void StratumMinerBitcoin::handleExMessage(const std::string &exMessage) {
  //
  // CMD_SUBMIT_SHARE / CMD_SUBMIT_SHARE_WITH_TIME:
  // | magic_number(1) | cmd(1) | len (2) | jobId (uint8_t) | session_id (uint16_t) |
  // | extra_nonce2 (uint32_t) | nNonce (uint32_t) | [nTime (uint32_t) |]
  //
  auto command = static_cast<StratumCommandEx>(exMessage[1]);
  if (command == StratumCommandEx::CMD_SUBMIT_SHARE) {
    handleExMessage_SubmitShare(exMessage, false);
  } else if (command == StratumCommandEx::CMD_SUBMIT_SHARE_WITH_TIME) {
    handleExMessage_SubmitShare(exMessage, true);
  }
}

void StratumMinerBitcoin::handleRequest_Submit(const string &idStr, const JsonNode &jparams) {
  auto &session = getSession();
  if (session.getState() != StratumSession::AUTHENTICATED) {
    session.responseError(idStr, StratumStatus::UNAUTHORIZED);

    // there must be something wrong, send reconnect command
    const string s = "{\"id\":null,\"method\":\"client.reconnect\",\"params\":[]}\n";
    session.sendData(s);
    return;
  }

  //  params[0] = Worker Name
  //  params[1] = Job ID
  //  params[2] = ExtraNonce 2
  //  params[3] = nTime
  //  params[4] = nonce
  if (jparams.children()->size() < 5) {
    session.responseError(idStr, StratumStatus::ILLEGAL_PARARMS);
    return;
  }

  uint8_t shortJobId;
  if (isNiceHashClient_) {
    shortJobId = (uint8_t) (jparams.children()->at(1).uint64() % 10);
  } else {
    shortJobId = (uint8_t) jparams.children()->at(1).uint32();
  }
  const uint64_t extraNonce2 = jparams.children()->at(2).uint64_hex();
  uint32_t nTime = jparams.children()->at(3).uint32_hex();
  const uint32_t nonce = jparams.children()->at(4).uint32_hex();

  handleRequest_Submit(idStr, shortJobId, extraNonce2, nonce, nTime);
}

void StratumMinerBitcoin::handleRequest_SuggestTarget(const string &idStr,
                                                      const JsonNode &jparams) {
  auto &session = getSession();
  if (session.getState() != StratumSession::CONNECTED) {
    return;  // suggest should be call before subscribe
  }

  if (jparams.children()->size() == 0) {
    session.responseError(idStr, StratumStatus::ILLEGAL_PARARMS);
    return;
  }
  resetCurDiff(TargetToDiff(jparams.children()->at(0).str()));
}

void StratumMinerBitcoin::handleExMessage_SubmitShare(const std::string &exMessage, bool isWithTime) {
  //
  // CMD_SUBMIT_SHARE / CMD_SUBMIT_SHARE_WITH_TIME:
  // | magic_number(1) | cmd(1) | len (2) | jobId (uint8_t) | session_id (uint16_t) |
  // | extra_nonce2 (uint32_t) | nNonce (uint32_t) | [nTime (uint32_t) |]
  //
  if (exMessage.size() != (isWithTime ? 19 : 15))
    return;

  const uint8_t *p = (uint8_t *) exMessage.data();
  const uint8_t shortJobId = *(uint8_t *) (p + 4);
  const uint16_t sessionId = *(uint16_t *) (p + 5);
  if (sessionId > StratumMessageEx::AGENT_MAX_SESSION_ID)
    return;

  const uint32_t exNonce2 = *(uint32_t *) (p + 7);
  const uint32_t nonce = *(uint32_t *) (p + 11);
  const uint32_t timestamp = (isWithTime == false ? 0 : *(uint32_t *) (p + 15));

  const uint64_t fullExtraNonce2 = ((uint64_t) sessionId << 32) | (uint64_t) exNonce2;

  // debug
  string logLine = Strings::Format("[agent] shortJobId: %02x, sessionId: %08x"
                                   ", exNonce2: %016llx, nonce: %08x, time: %08x",
                                   shortJobId, (uint32_t) sessionId,
                                   fullExtraNonce2, nonce, time);
  DLOG(INFO) << logLine;

  handleRequest_Submit("null", shortJobId, fullExtraNonce2, nonce, timestamp);
}

void StratumMinerBitcoin::handleRequest_Submit(const string &idStr,
                                               uint8_t shortJobId,
                                               uint64_t extraNonce2,
                                               uint32_t nonce,
                                               uint32_t nTime) {
  auto &session = getSession();
  auto &server = session.getServer();
  auto &worker = session.getWorker();
  auto jobRepo = server.GetJobRepository();

  const string extraNonce2Hex = Strings::Format("%016llx", extraNonce2);
  assert(extraNonce2Hex.length() / 2 == kExtraNonce2Size_);

  auto localJob = session.findLocalJob(shortJobId);
  if (localJob == nullptr) {
    // if can't find localJob, could do nothing
    handleShare(idStr, StratumStatus::JOB_NOT_FOUND, 0);

    LOG(INFO) << "rejected share: " << StratumStatus::toString(StratumStatus::JOB_NOT_FOUND)
              << ", worker: " << worker.fullName_ << ", Share(id: " << idStr << ", shortJobId: "
              << (int) shortJobId << ", nTime: " << nTime << "/" << date("%F %T", nTime) << ")";
    return;
  }

  uint32_t height = 0;

  shared_ptr<StratumJobEx> exjob;
  exjob = jobRepo->getStratumJobEx(localJob->jobId_);

  if (exjob.get() != NULL) {
    // 0 means miner use stratum job's default block time
    StratumJobBitcoin *sjobBitcoin = static_cast<StratumJobBitcoin *>(exjob->sjob_);
    if (nTime == 0) {
      nTime = sjobBitcoin->nTime_;
    }

    height = sjobBitcoin->height_;
  }

  auto iter = jobDiffs_.find(localJob);
  if (iter == jobDiffs_.end()) {
    LOG(ERROR) << "can't find session's diff, worker: " << worker.fullName_;
    return;
  }

  ShareBitcoin share;
  share.version_ = ShareBitcoin::CURRENT_VERSION;
  share.jobId_ = localJob->jobId_;
  share.workerHashId_ = workerId_;
  share.userId_ = worker.userId_;
  share.shareDiff_ = iter->second;
  share.blkBits_ = localJob->blkBits_;
  share.timestamp_ = (uint64_t) time(nullptr);
  share.height_ = height;
  share.nonce_ = nonce;
  share.sessionId_ = session.getSessionId();
  share.status_ = StratumStatus::REJECT_NO_REASON;
  share.ip_.fromIpv4Int(session.getClientIp());

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
  } else {
#ifdef  USER_DEFINED_COINBASE
    // check block header
    share.status_ = server->checkShare(share, extraNonce1_, extraNonce2Hex,
                                       nTime, nonce, jobTarget,
                                       worker_.fullName_,
                                       &localJob->userCoinbaseInfo_);
#else
    // check block header
    share.status_ = server.checkShare(share, share.sessionId_, extraNonce2Hex,
                                      nTime, nonce, jobTarget,
                                      worker.fullName_);
#endif
  }

  DLOG(INFO) << share.toString();

  if (!handleShare(idStr, share.status_, share.shareDiff_)) {
    // add invalid share to counter
    invalidSharesCounter_.insert((int64_t) time(nullptr), 1);

    // log all rejected share to answer "Why the rejection rate of my miner increased?"
    LOG(INFO) << "rejected share: " << StratumStatus::toString(share.status_)
              << ", worker: " << worker.fullName_ << ", " << share.toString();

    // check if thers is invalid share spamming
    int64_t invalidSharesNum = invalidSharesCounter_.sum(time(nullptr),
                                                         INVALID_SHARE_SLIDING_WINDOWS_SIZE);
    // too much invalid shares, don't send them to kafka
    if (invalidSharesNum >= INVALID_SHARE_SLIDING_WINDOWS_MAX_LIMIT) {
      isSendShareToKafka = false;

      LOG(INFO) << "invalid share spamming, diff: "
                << share.shareDiff_ << ", worker: " << worker.fullName_ << ", agent: "
                << clientAgent_ << ", ip: " << session.getClientIp();
    }
  }

  if (isSendShareToKafka) {
    share.checkSum_ = share.checkSum();
    server.sendShare2Kafka((const uint8_t *) &share, sizeof(ShareBitcoin));
  }
}
