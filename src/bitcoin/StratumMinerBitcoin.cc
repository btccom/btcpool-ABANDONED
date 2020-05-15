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
#include "BitcoinUtils.h"

#include <event2/buffer.h>

///////////////////////////////// StratumMinerBitcoin
///////////////////////////////////
StratumMinerBitcoin::StratumMinerBitcoin(
    StratumSessionBitcoin &session,
    const DiffController &diffController,
    const string &clientAgent,
    const string &workerName,
    int64_t workerId)
  : StratumMinerBase(
        session, diffController, clientAgent, workerName, workerId) {
}

void StratumMinerBitcoin::handleRequest(
    const string &idStr,
    const string &method,
    const JsonNode &jparams,
    const JsonNode &jroot) {
  if (method == "mining.submit") {
    handleRequest_Submit(idStr, jparams);
  }
}

void StratumMinerBitcoin::handleExMessage(const std::string &exMessage) {
  //
  // SUBMIT_SHARE | SUBMIT_SHARE_WITH_TIME | SUBMIT_SHARE_WITH_VER |
  // SUBMIT_SHARE_WITH_TIME_VER: | magic_number(1) | cmd(1) | len (2) | jobId
  // (uint8_t) | session_id (uint16_t) | | extra_nonce2 (uint32_t) | nNonce
  // (uint32_t) | [nTime (uint32_t) ] | [nVersionMask (uint32_t)] |
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

void StratumMinerBitcoin::handleRequest_Submit(
    const string &idStr, const JsonNode &jparams) {
  auto &session = getSession();
  if (session.getState() != StratumSession::AUTHENTICATED) {
    session.responseError(idStr, StratumStatus::UNAUTHORIZED);

    // there must be something wrong, send reconnect command
    const string s =
        "{\"id\":null,\"method\":\"client.reconnect\",\"params\":[]}\n";
    session.sendData(s);
    return;
  }

  if (jparams.children()->size() < 5) {
    session.responseError(idStr, StratumStatus::ILLEGAL_PARARMS);
    return;
  }

  uint8_t shortJobId;
  if (isNiceHashClient_) {
    shortJobId = (uint8_t)(
        jparams.children()->at(1).uint64() % session.maxNumLocalJobs());
  } else {
    shortJobId = (uint8_t)jparams.children()->at(1).uint32();
  }

#ifdef CHAIN_TYPE_ZEC
  BitcoinNonceType nonce;

  // For ZCash:
  //  params[0] = WORKER_NAME
  //  params[1] = JOB_ID
  //  params[2] = TIME
  //  params[3] = NONCE_2
  //  params[4] = EQUIHASH_SOLUTION
  uint32_t nTime = SwapUint(jparams.children()->at(2).uint32_hex());
  string nonce2Str = jparams.children()->at(3).str();
  if (nonce2Str.size() != 56) {
    session.responseError(idStr, StratumStatus::ILLEGAL_PARARMS);
  }

  nonce.nonce = SwapUint(uint256S(Strings::Format(
      "%08x%s",
      session.getSessionId(),
      jparams.children()->at(3).str().c_str())));
  nonce.solution = jparams.children()->at(4).str();

  // ZCash's share doesn't have them
  const uint64_t extraNonce2 = 0;
  uint32_t versionMask = 0u;

#else

  // For Bitcoin:
  //  params[0] = Worker Name
  //  params[1] = Job ID
  //  params[2] = ExtraNonce 2
  //  params[3] = nTime
  //  params[4] = nonce
  //  params[5] = version mask (optional)
  const uint64_t extraNonce2 = jparams.children()->at(2).uint64_hex();
  uint32_t nTime = jparams.children()->at(3).uint32_hex();
  BitcoinNonceType nonce = jparams.children()->at(4).uint32_hex();
  uint32_t versionMask = 0u;
  if (jparams.children()->size() >= 6) {
    versionMask = jparams.children()->at(5).uint32_hex();
  }
#endif

  uint32_t extraGrandNonce1 = 0;
  if (getSession().isGrandPoolClient()) {
    extraGrandNonce1 = jparams.children()->at(0).uint32_hex();
  }

  handleRequest_Submit(
      idStr,
      shortJobId,
      extraNonce2,
      nonce,
      nTime,
      versionMask,
      extraGrandNonce1);
}

void StratumMinerBitcoin::handleExMessage_SubmitShare(
    const std::string &exMessage,
    const bool isWithTime,
    const bool isWithVersion) {
#ifdef CHAIN_TYPE_ZEC
  LOG(INFO) << "sserver ZCash does not support BTCAgent protocol at current.";
#else
  //
  // SUBMIT_SHARE | SUBMIT_SHARE_WITH_TIME | SUBMIT_SHARE_WITH_VER |
  // SUBMIT_SHARE_WITH_TIME_VER: | magic_number(1) | cmd(1) | len (2) | jobId
  // (uint8_t) | session_id (uint16_t) | | extra_nonce2 (uint32_t) | nNonce
  // (uint32_t) | [nTime (uint32_t) ] | [nVersionMask (uint32_t)] |
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
  const uint8_t shortJobId = *(uint8_t *)(p + 4);
  const uint16_t sessionId = *(uint16_t *)(p + 5);
  if (sessionId > StratumMessageEx::AGENT_MAX_SESSION_ID) {
    return;
  }
  const uint32_t exNonce2 = *(uint32_t *)(p + 7);
  const uint32_t nonce = *(uint32_t *)(p + 11);
  const uint32_t timestamp = (isWithTime == false ? 0 : *(uint32_t *)(p + 15));
  const uint32_t versionMask =
      (isWithVersion == false ? 0 : *(uint32_t *)(p + msgSize - 4));

  const uint64_t fullExtraNonce2 =
      ((uint64_t)sessionId << 32) | (uint64_t)exNonce2;

  // debug
  DLOG(INFO) << Strings::Format(
      "[agent] shortJobId: %02x, sessionId: %08x, "
      "exNonce2: %016x, nonce: %08x, time: %08x, versionMask: %08x",
      shortJobId,
      (uint32_t)sessionId,
      fullExtraNonce2,
      nonce,
      timestamp,
      versionMask);

  try {
    auto dispatcher = dynamic_cast<StratumMessageAgentDispatcher *>(
        &(session_.getDispatcher()));

    handleRequest_Submit(
        std::to_string(dispatcher->nextSubmitIndex()),
        shortJobId,
        fullExtraNonce2,
        nonce,
        timestamp,
        versionMask,
        0);
  } catch (const std::bad_cast &ex) {
    LOG(WARNING) << Strings::Format(
        "[agent] submit before authorized, ignore! shortJobId: %02x, "
        "sessionId: %08x, "
        "exNonce2: %016x, nonce: %08x, time: %08x, versionMask: %08x",
        shortJobId,
        (uint32_t)sessionId,
        fullExtraNonce2,
        nonce,
        timestamp,
        versionMask);
  }
#endif
}

void StratumMinerBitcoin::handleRequest_Submit(
    const string &idStr,
    uint8_t shortJobId,
    uint64_t extraNonce2,
    BitcoinNonceType nonce,
    uint32_t nTime,
    uint32_t versionMask,
    uint32_t extraGrandNonce1) {
  auto &session = getSession();
  auto &server = session.getServer();
  auto &worker = session.getWorker();

  // Prevent changing unused bits to bypass the duplicate share checking
  if (server.extraNonce2Size() < sizeof(extraNonce2)) {
    extraNonce2 &= (1ull << server.extraNonce2Size() * 8) - 1;
  }

  const string extraNonce2Hex = Strings::Format(
      "%0" + std::to_string(server.extraNonce2Size() * 2) + "x", extraNonce2);
  assert(extraNonce2Hex.size() == server.extraNonce2Size() * 2);

  // a function to log rejected stale share
  auto rejectJobNotFound = [&](size_t chainId) {
    handleShare(idStr, StratumStatus::JOB_NOT_FOUND, 0, chainId);

    LOG(INFO) << "rejected share: "
              << StratumStatus::toString(StratumStatus::JOB_NOT_FOUND)
              << ", worker: " << worker.fullName_
              << ", versionMask: " << Strings::Format("%08x", versionMask)
              << ", Share(id: " << idStr << ", shortJobId: " << (int)shortJobId
              << ", nTime: " << nTime << "/" << date("%F %T", nTime) << ")";
  };

  auto localJob = session.findLocalJob(shortJobId);
  if (localJob == nullptr) {
    // If can't find localJob, could do nothing.
    rejectJobNotFound(session.getChainId());
    return;
  }

  auto jobRepo = server.GetJobRepository(localJob->chainId_);
  auto exjob = jobRepo->getStratumJobEx(localJob->jobId_);
  if (exjob == nullptr) {
    // If can't find exjob, could do nothing too.
    //
    // This situation can occur after LocalJobs has been expanded to 256.
    // But in this case, the miner apparently submitted a very outdated job.
    // So we don't have to risk that the code might crash (subsequent code
    // forgets to check for null pointers) and send it to kafka.
    //
    // Tips: an exjob is only removed after max_job_lifetime seconds past.
    //
    rejectJobNotFound(localJob->chainId_);
    return;
  }

  auto sjobBitcoin = std::static_pointer_cast<StratumJobBitcoin>(exjob->sjob_);

  // 0 means miner use stratum job's default block time
  if (nTime == 0) {
    nTime = sjobBitcoin->nTime_;
  }

  auto iter = jobDiffs_.find(localJob);
  if (iter == jobDiffs_.end()) {
    handleShare(idStr, StratumStatus::JOB_NOT_FOUND, 0, localJob->chainId_);
    LOG(ERROR) << "can't find session's diff, worker: " << worker.fullName_;
    return;
  }

  ShareBitcoin share;
  share.set_version(ShareBitcoin::CURRENT_VERSION);
  share.set_jobid(localJob->jobId_);
  share.set_workerhashid(workerId_);
  share.set_userid(worker.userId(localJob->chainId_));
  share.set_sharediff(iter->second);
  share.set_blkbits(localJob->blkBits_);
  share.set_timestamp((uint64_t)time(nullptr));
  share.set_height(sjobBitcoin->height_);
  share.set_versionmask(versionMask);
  share.set_sessionid(session.getSessionId());
  share.set_status(StratumStatus::REJECT_NO_REASON);
  // set IP
  IpAddress ip;
  ip.fromIpv4Int(session.getClientIp());
  share.set_ip(ip.toString());
// set nonce
#ifdef CHAIN_TYPE_ZEC
  uint32_t nonceHash = djb2(nonce.nonce.ToString().c_str());
  share.set_nonce(nonceHash);
#else
  share.set_nonce(nonce);
#endif

  if (server.singleUserMode()) {
    share.set_extuserid(share.userid());
    share.set_userid(server.singleUserId(localJob->chainId_));
  } else if (server.subPoolEnabled()) {
    share.set_extuserid(server.subPoolExtUserId());
  }

  // calc jobTarget
  uint256 jobTarget;
  BitcoinDifficulty::DiffToTarget(share.sharediff(), jobTarget);

#ifdef CHAIN_TYPE_ZEC
  LocalShareType localShare(
      nonce.nonce.GetCheapHash(),
      nonceHash,
      nTime,
      djb2(nonce.solution.c_str()));
#else
  LocalShareType localShare(
      extraNonce2, nonce, nTime, versionMask, extraGrandNonce1);
#endif

  // can't find local share
  if (!localJob->addLocalShare(localShare)) {
    share.set_status(StratumStatus::DUPLICATE_SHARE);
    handleCheckedShare(idStr, localJob->chainId_, share);
  } else {
    // check block header
    server.checkShare(
        localJob->chainId_,
        share,
        session.getSessionId(),
        extraNonce2Hex,
        nTime,
        nonce,
        versionMask,
        jobTarget,
        worker.fullName_,
        getSession().isGrandPoolClient(),
        extraGrandNonce1,
        [this,
         alive = std::weak_ptr<bool>{alive_},
         idStr,
         chainId = localJob->chainId_,
         ip = session.getClientIp(),
         share,
         &server](int32_t status, uint32_t bitsReached) mutable {
          share.set_status(status);
          if (bitsReached > 0) {
            share.set_bitsreached(bitsReached);
          }

          if (alive.expired() || handleCheckedShare(idStr, chainId, share)) {
            if (server.useShareV1()) {
              ShareBitcoinBytesV1 sharev1;
              sharev1.jobId_ = share.jobid();
              sharev1.workerHashId_ = share.workerhashid();
              sharev1.ip_ = ip;
              sharev1.userId_ = share.userid();
              sharev1.shareDiff_ = share.sharediff();
              sharev1.timestamp_ = share.timestamp();
              sharev1.blkBits_ = share.blkbits();
              sharev1.result_ = StratumStatus::isAccepted(share.status())
                  ? ShareBitcoinBytesV1::ACCEPT
                  : ShareBitcoinBytesV1::REJECT;

              server.sendShare2Kafka(
                  chainId, (char *)&sharev1, sizeof(sharev1));
            } else {
              std::string message;
              if (!share.SerializeToStringWithVersion(message)) {
                LOG(ERROR) << "share SerializeToStringWithVersion failed!"
                           << share.toString();
                return;
              }
              server.sendShare2Kafka(chainId, message.data(), message.size());
            }
          }
        });
  }
}

bool StratumMinerBitcoin::handleCheckedShare(
    const std::string &idStr, size_t chainId, const ShareBitcoin &share) {
  DLOG(INFO) << share.toString();

  // we send share to kafka by default, but if there are lots of invalid
  // shares in a short time, we just drop them.
  bool isSendShareToKafka = true;
  auto &session = getSession();
  auto &worker = session.getWorker();

  if (!handleShare(idStr, share.status(), share.sharediff(), chainId)) {
    // add invalid share to counter
    invalidSharesCounter_.insert((int64_t)time(nullptr), 1);

    // log all rejected share to answer "Why the rejection rate of my miner
    // increased?"
    LOG(INFO) << "rejected share: " << StratumStatus::toString(share.status())
              << ", worker: " << worker.fullName_ << ", versionMask: "
              << Strings::Format("%08x", share.versionmask()) << ", "
              << share.toString();

    // check if thers is invalid share spamming
    int64_t invalidSharesNum = invalidSharesCounter_.sum(
        time(nullptr), INVALID_SHARE_SLIDING_WINDOWS_SIZE);
    // too much invalid shares, don't send them to kafka
    if (invalidSharesNum >= INVALID_SHARE_SLIDING_WINDOWS_MAX_LIMIT) {
      isSendShareToKafka = false;
      LOG(WARNING) << "invalid share spamming, worker: " << worker.fullName_
                   << ", " << share.toString();
    }
  }

  return isSendShareToKafka;
}
