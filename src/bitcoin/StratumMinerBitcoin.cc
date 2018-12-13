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
  // SUBMIT_SHARE | SUBMIT_SHARE_WITH_TIME | SUBMIT_SHARE_WITH_VER | SUBMIT_SHARE_WITH_TIME_VER:
  // | magic_number(1) | cmd(1) | len (2) | jobId (uint8_t) | session_id (uint16_t) |
  // | extra_nonce2 (uint32_t) | nNonce (uint32_t) | [nTime (uint32_t) ] | [nVersionMask (uint32_t)] |
  //
  auto command = static_cast<StratumCommandEx>(exMessage[1]);
  if (command == StratumCommandEx::SUBMIT_SHARE) {
    handleExMessage_SubmitShare(exMessage, false, false);
  } else if (command == StratumCommandEx::SUBMIT_SHARE_WITH_TIME) {
    handleExMessage_SubmitShare(exMessage, true, false);
  } else if (command == StratumCommandEx::SUBMIT_SHARE_WITH_VER) {
    handleExMessage_SubmitShare(exMessage, false, true);
  } else if (command == StratumCommandEx::SUBMIT_SHARE_WITH_TIME_VER) {
    handleExMessage_SubmitShare(exMessage, true, true);
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
  //  params[5] = version mask (optional)
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

  uint32_t versionMask = 0u;
  if (jparams.children()->size() >= 6) {
    versionMask = jparams.children()->at(5).uint32_hex();
  }

  handleRequest_Submit(idStr, shortJobId, extraNonce2, nonce, nTime, versionMask);
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
  resetCurDiff(formatDifficulty(TargetToDiff(jparams.children()->at(0).str())));
}

void StratumMinerBitcoin::handleExMessage_SubmitShare(const std::string &exMessage,
                                                      const bool isWithTime,
                                                      const bool isWithVersion) {
  //
  // SUBMIT_SHARE | SUBMIT_SHARE_WITH_TIME | SUBMIT_SHARE_WITH_VER | SUBMIT_SHARE_WITH_TIME_VER:
  // | magic_number(1) | cmd(1) | len (2) | jobId (uint8_t) | session_id (uint16_t) |
  // | extra_nonce2 (uint32_t) | nNonce (uint32_t) | [nTime (uint32_t) ] | [nVersionMask (uint32_t)] |
  //
  size_t msgSize = 15;
  if (isWithTime) {
    msgSize += 4;
  }
  if (isWithVersion) {
    msgSize += 4;
  }
  if (exMessage.size() != msgSize) {
    return;
  }

  const uint8_t *p = (uint8_t *)exMessage.data();
  const uint8_t shortJobId = *(uint8_t  *)(p +  4);
  const uint16_t sessionId = *(uint16_t *)(p +  5);
  if (sessionId > StratumMessageEx::AGENT_MAX_SESSION_ID) {
    return;
  }
  const uint32_t exNonce2    = *(uint32_t *)(p +  7);
  const uint32_t nonce       = *(uint32_t *)(p + 11);
  const uint32_t timestamp   = (isWithTime    == false ? 0 : *(uint32_t *)(p + 15));
  const uint32_t versionMask = (isWithVersion == false ? 0 : *(uint32_t *)(p + msgSize - 4));

  const uint64_t fullExtraNonce2 = ((uint64_t)sessionId << 32) | (uint64_t)exNonce2;

  // debug
  DLOG(INFO) << Strings::Format("[agent] shortJobId: %02x, sessionId: %08x, "
                                "exNonce2: %016llx, nonce: %08x, time: %08x, versionMask: %08x",
                                shortJobId, (uint32_t)sessionId,
                                fullExtraNonce2, nonce, timestamp, versionMask);

  handleRequest_Submit("null", shortJobId, fullExtraNonce2, nonce, timestamp, versionMask);
}

void StratumMinerBitcoin::handleRequest_Submit(const string &idStr,
                                               uint8_t shortJobId,
                                               uint64_t extraNonce2,
                                               uint32_t nonce,
                                               uint32_t nTime,
                                               uint32_t versionMask) {
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
              << ", worker: " << worker.fullName_
              << ", versionMask: " << Strings::Format("%08x", versionMask)
              << ", Share(id: " << idStr << ", shortJobId: "
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
  share.set_version(ShareBitcoin::CURRENT_VERSION);
  share.set_jobid(localJob->jobId_);
  share.set_workerhashid(workerId_);
  share.set_userid(worker.userId_);
  share.set_sharediff(iter->second);
  share.set_blkbits(localJob->blkBits_);
  share.set_timestamp((uint64_t) time(nullptr));
  share.set_height(height);
  share.set_nonce(nonce);
  share.set_sessionid(session.getSessionId());
  share.set_status(StratumStatus::REJECT_NO_REASON);
  IpAddress ip;
  ip.fromIpv4Int(session.getClientIp());
  share.set_ip(ip.toString());

  // calc jobTarget
  uint256 jobTarget;
  DiffToTarget(share.sharediff(), jobTarget);

  // we send share to kafka by default, but if there are lots of invalid
  // shares in a short time, we just drop them.
  bool isSendShareToKafka = true;

  LocalShare localShare(extraNonce2, nonce, nTime, versionMask);

  // can't find local share
  if (!localJob->addLocalShare(localShare)) {
    share.set_status(StratumStatus::DUPLICATE_SHARE);
  } else {
#ifdef  USER_DEFINED_COINBASE
    // check block header
    share.set_status(server->checkShare(share, extraNonce1_, extraNonce2Hex,
                                       nTime, nonce, versionMask, jobTarget,
                                       worker_.fullName_,
                                       &localJob->userCoinbaseInfo_));
#else
    // check block header
    share.set_status(server.checkShare(share, share.sessionid(), extraNonce2Hex,
                                      nTime, nonce, versionMask, jobTarget,
                                      worker.fullName_));
#endif
  }

  DLOG(INFO) << share.toString();

  if (!handleShare(idStr, share.status(), share.sharediff())) {
    // add invalid share to counter
    invalidSharesCounter_.insert((int64_t) time(nullptr), 1);

    // log all rejected share to answer "Why the rejection rate of my miner increased?"
    LOG(INFO) << "rejected share: " << StratumStatus::toString(share.status())
              << ", worker: " << worker.fullName_
              << ", versionMask: " << Strings::Format("%08x", versionMask)
              << ", " << share.toString();

    // check if thers is invalid share spamming
    int64_t invalidSharesNum = invalidSharesCounter_.sum(time(nullptr),
                                                         INVALID_SHARE_SLIDING_WINDOWS_SIZE);
    // too much invalid shares, don't send them to kafka
    if (invalidSharesNum >= INVALID_SHARE_SLIDING_WINDOWS_MAX_LIMIT) {
      isSendShareToKafka = false;

      LOG(INFO) << "invalid share spamming, diff: "
                << share.sharediff() << ", worker: " << worker.fullName_ << ", agent: "
                << clientAgent_ << ", ip: " << session.getClientIp();
    }
  }

  if (isSendShareToKafka) {

    std::string message;
    uint32_t size = 0;
    if (!share.SerializeToArrayWithVersion(message, size)) {
      LOG(ERROR) << "share SerializeToBuffer failed!"<< share.toString();
      return;
    }

    server.sendShare2Kafka((const uint8_t *) message.data(), size);
  }
}
