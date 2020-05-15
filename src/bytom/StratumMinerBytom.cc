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
#include "StratumMinerBytom.h"

#include "StratumServerBytom.h"
#include "StratumSessionBytom.h"
#include "StratumMessageDispatcher.h"
#include "DiffController.h"

#include "bytom/bh_shared.h"

#ifndef NO_CUDA
#include "bytom/cutil/src/GpuTs.h"
#endif // NO_CUDA

/////////////////////////////StratumMinerBytom////////////////////////////
StratumMinerBytom::StratumMinerBytom(
    StratumSessionBytom &session,
    const DiffController &diffController,
    const std::string &clientAgent,
    const std::string &workerName,
    int64_t workerId)
  : StratumMinerBase(
        session, diffController, clientAgent, workerName, workerId) {
}

void StratumMinerBytom::handleRequest(
    const std::string &idStr,
    const std::string &method,
    const JsonNode &jparams,
    const JsonNode &jroot) {
  if (method == "getwork") {
    handleRequest_GetWork(idStr, jparams);
  } else if (method == "submit") {
    handleRequest_Submit(idStr, jparams);
  }
}

void StratumMinerBytom::handleRequest_GetWork(
    const string &idStr, const JsonNode &jparams) {
  getSession().sendMiningNotify(
      getSession()
          .getServer()
          .GetJobRepository(getSession().getChainId())
          ->getLatestStratumJobEx(),
      false);
}

namespace BytomUtils {

int checkProofOfWork(
    EncodeBlockHeader_return encoded,
    shared_ptr<StratumJobBytom> sJob,
    uint64_t difficulty) {
  DLOG(INFO) << "verify blockheader hash=" << encoded.r1
             << ", seed=" << sJob->seed_;
  vector<char> vHeader, vSeed;
  Hex2Bin(encoded.r1, vHeader);
  Hex2Bin(sJob->seed_.c_str(), sJob->seed_.length(), vSeed);

#ifndef NO_CUDA
  uint8_t *pTarget = GpuTs((uint8_t *)vHeader.data(), (uint8_t *)vSeed.data());
#else
  GoSlice hSlice = {
      (void *)vHeader.data(), (int)vHeader.size(), (int)vHeader.size()};
  GoSlice sSlice = {(void *)vSeed.data(), (int)vSeed.size(), (int)vSeed.size()};
  uint8_t pTarget[32];
  GoSlice hOut = {(void *)pTarget, 32, 32};
  ProofOfWorkHashCPU(hSlice, sSlice, hOut);
#endif

  //  first job target first before checking solved share
  string targetStr;
  Bin2Hex(pTarget, 32, targetStr);
  GoSlice text = {(void *)pTarget, 32, 32};
  uint64_t localJobBits = Bytom_JobDifficultyToTargetCompact(difficulty);

  bool powResultLocalJob = CheckProofOfWork(text, localJobBits);
  if (powResultLocalJob) {
    //  passed job target, now check the blockheader target
    bool powResultBlock = CheckProofOfWork(text, sJob->blockHeader_.bits);
    if (powResultBlock) {
      return StratumStatus::SOLVED;
    }
    return StratumStatus::ACCEPT;
  } else {
    return StratumStatus::LOW_DIFFICULTY;
  }

  return StratumStatus::REJECT_NO_REASON;
}

} // namespace BytomUtils

void StratumMinerBytom::handleRequest_Submit(
    const string &idStr, const JsonNode &jparams) {
  auto &session = getSession();
  auto &server = session.getServer();
  /*
    Calculating difficulty
    - Nonce. B3-Mimic send hex value.

    - Job PoW bits. Bits to check proof of work of job (not block)
      see CalculateTargetCompactByDifficulty (bh_shared.go).

    - Block PoW bits. Use BlockHeader.Bits
  */
  LOG(INFO) << idStr.c_str() << ": bytom handle request submit";
  JsonNode &params = const_cast<JsonNode &>(jparams);

  uint8_t shortJobId = (uint8_t)params["job_id"].uint32();

  auto *localJob = session.findLocalJob(shortJobId);
  if (nullptr == localJob) {
    session.rpc2ResponseBoolean(idStr, false, "Block expired");
    LOG(ERROR) << "can not find local bytom job id=" << (int)shortJobId;
    return;
  }

  shared_ptr<StratumJobEx> exjob;
  exjob = server.GetJobRepository(localJob->chainId_)
              ->getStratumJobEx(localJob->jobId_);
  if (nullptr == exjob || nullptr == exjob->sjob_) {
    session.rpc2ResponseBoolean(idStr, false, "Block expired");
    LOG(ERROR) << "bytom local job not found " << std::hex << localJob->jobId_;
    return;
  }

  auto sJob = std::static_pointer_cast<StratumJobBytom>(exjob->sjob_);
  if (nullptr == sJob) {
    session.rpc2ResponseBoolean(idStr, false, "Unknown reason");
    LOG(FATAL) << "Code error, casting stratum job bytom failed for job id="
               << std::hex << localJob->jobId_;
    return;
  }

  // get header submission string and header hash string
  //  nonce in bytom B3Poisoned is using hex not decimal
  uint64_t nonce = 0;
  {
    string nonceHex = params["nonce"].str();
    vector<char> nonceBinBuf;
    Hex2BinReverse(nonceHex.c_str(), nonceHex.length(), nonceBinBuf);
    nonce = *(uint64_t *)nonceBinBuf.data();
    LOG(INFO) << idStr.c_str() << ": bytom handle request submit jobId "
              << (int)shortJobId << " with nonce: " << nonce
              << " - noncehex: " << nonceHex.c_str();
  }

  //  Check share duplication
  // LocalShare localShare(nonce, 0, 0);
  LocalShareType localShare(nonce);
  if (!server.isEnableSimulator_ && !localJob->addLocalShare(localShare)) {
    session.responseError(idStr, StratumStatus::DUPLICATE_SHARE);
    // add invalid share to counter
    invalidSharesCounter_.insert((int64_t)time(nullptr), 1);
    return;
  }

  auto &worker = session.getWorker();
  auto iter = jobDiffs_.find(localJob);
  if (iter == jobDiffs_.end()) {
    LOG(ERROR) << "can't find session's diff, worker: " << worker.fullName_;
    return;
  }
  auto difficulty = iter->second;
  auto clientIp = session.getClientIp();

  // Check share
  ShareBytom share;
  //  ShareBase portion
  share.set_version(ShareBytom::CURRENT_VERSION);
  share.set_workerhashid(workerId_);
  share.set_userid(worker.userId(localJob->chainId_));
  share.set_status(StratumStatus::REJECT_NO_REASON);
  share.set_timestamp((uint32_t)time(nullptr));
  IpAddress ip;
  ip.fromIpv4Int(clientIp);
  share.set_ip(ip.toString());

  //  ShareBytom portion
  share.set_jobid(localJob->jobId_);
  share.set_sharediff(difficulty);
  share.set_blkbits(sJob->blockHeader_.bits);
  share.set_height(sJob->blockHeader_.height);

  auto StringToCheapHash = [](const std::string &str) -> uint64_t {
    // int merkleRootLen = std::min(32, (int)str.length());
    auto merkleRootBegin = (uint8_t *)&str[0];
    // auto merkleRootEnd = merkleRootBegin + merkleRootLen;

    uint64_t res;
    memcpy(&res, merkleRootBegin, std::min(8, (int)str.length()));
    return res;

    // vector<uint8_t> merkleRootBin(merkleRootBegin, merkleRootEnd);
    // return uint256(merkleRootBin).GetCheapHash();
  };
  BytomCombinedHeader combinedHeader;

  combinedHeader.blockCommitmentMerkleRootCheapHash_ =
      StringToCheapHash(sJob->blockHeader_.transactionsMerkleRoot);
  combinedHeader.blockCommitmentStatusHashCheapHash_ =
      StringToCheapHash(sJob->blockHeader_.transactionStatusHash);
  combinedHeader.timestamp_ = sJob->blockHeader_.timestamp;
  combinedHeader.nonce_ = nonce;

  share.set_combinedheader(&combinedHeader, sizeof(combinedHeader));

  if (exjob->isStale()) {
    share.set_status(StratumStatus::STALE_SHARE);
    session.rpc2ResponseBoolean(idStr, false, "Block expired");
  } else {
    EncodeBlockHeader_return encoded = EncodeBlockHeader(
        sJob->blockHeader_.version,
        sJob->blockHeader_.height,
        (char *)sJob->blockHeader_.previousBlockHash.c_str(),
        sJob->blockHeader_.timestamp,
        nonce,
        sJob->blockHeader_.bits,
        (char *)sJob->blockHeader_.transactionStatusHash.c_str(),
        (char *)sJob->blockHeader_.transactionsMerkleRoot.c_str());
    int powResult = BytomUtils::checkProofOfWork(encoded, sJob, difficulty);
    share.set_status(powResult);
    if (powResult == StratumStatus::SOLVED) {
      std::cout << "share solved\n";
      LOG(INFO) << "share solved";
      server.sendSolvedShare2Kafka(
          localJob->chainId_,
          nonce,
          encoded.r0,
          share.height(),
          Bytom_TargetCompactToDifficulty(sJob->blockHeader_.bits),
          worker);
      server.GetJobRepository(localJob->chainId_)
          ->markAllJobsAsStale(sJob->height());
      handleShare(idStr, share.status(), share.sharediff(), localJob->chainId_);
    } else if (powResult == StratumStatus::ACCEPT) {
      handleShare(idStr, share.status(), share.sharediff(), localJob->chainId_);
    } else {
      std::string failMessage = "Unknown reason";
      switch (share.status()) {
      case StratumStatus::LOW_DIFFICULTY:
        failMessage = "Low difficulty share";
        break;
      }
      session.rpc2ResponseBoolean(idStr, false, failMessage);
    }
    free(encoded.r0);
    free(encoded.r1);
  }

  bool isSendShareToKafka = true;
  DLOG(INFO) << share.toString();
  // check if thers is invalid share spamming
  if (!StratumStatus::isAccepted(share.status())) {
    int64_t invalidSharesNum = invalidSharesCounter_.sum(
        time(nullptr), INVALID_SHARE_SLIDING_WINDOWS_SIZE);
    // too much invalid shares, don't send them to kafka
    if (invalidSharesNum >= INVALID_SHARE_SLIDING_WINDOWS_MAX_LIMIT) {
      isSendShareToKafka = false;
      LOG(WARNING) << "invalid share spamming, worker: " << worker.fullName_
                   << ", " << share.toString();
    }
  }

  if (isSendShareToKafka) {

    std::string message;
    if (!share.SerializeToStringWithVersion(message)) {
      LOG(ERROR) << "share SerializeToStringWithVersion failed!"
                 << share.toString();
      return;
    }
    server.sendShare2Kafka(localJob->chainId_, message.data(), message.size());

    // string shareInHex;
    // Bin2Hex((uint8_t *) &share, sizeof(ShareBytom), shareInHex);
    // LOG(INFO) << "\nsendShare2Kafka ShareBytom:\n"
    //           << "- size: " << sizeof(ShareBytom) << " bytes\n"
    //           << "- hexvalue: " << shareInHex.c_str() << "\n";
  }
}
