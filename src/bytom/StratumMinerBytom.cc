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
#include "cutil/src/GpuTs.h"
#endif  //NO_CUDA

/////////////////////////////StratumMinerBytom////////////////////////////
StratumMinerBytom::StratumMinerBytom(StratumSessionBytom &session,
                                     const DiffController &diffController,
                                     const std::string &clientAgent,
                                     const std::string &workerName,
                                     int64_t workerId)
    : StratumMinerBase(session, diffController, clientAgent, workerName, workerId) {
}

void StratumMinerBytom::handleRequest(const std::string &idStr,
                                      const std::string &method,
                                      const JsonNode &jparams,
                                      const JsonNode &jroot) {
  if (method == "getwork") {
    handleRequest_GetWork(idStr, jparams);
  } else if (method == "submit") {
    handleRequest_Submit(idStr, jparams);
  }
}

void StratumMinerBytom::handleRequest_GetWork(const string &idStr, const JsonNode &jparams) {
  getSession().sendMiningNotify(getSession().getServer().GetJobRepository()->getLatestStratumJobEx(), false);
}

namespace BytomUtils {

int checkProofOfWork(EncodeBlockHeader_return encoded, StratumJobBytom *sJob, uint64_t difficulty) {
  DLOG(INFO) << "verify blockheader hash=" << encoded.r1 << ", seed=" << sJob->seed_;
  vector<char> vHeader, vSeed;
  Hex2Bin(encoded.r1, vHeader);
  Hex2Bin(sJob->seed_.c_str(), sJob->seed_.length(), vSeed);

#ifndef NO_CUDA
  uint8_t *pTarget = GpuTs((uint8_t*)vHeader.data(), (uint8_t*)vSeed.data());
#else
  GoSlice hSlice = {(void *) vHeader.data(), (int) vHeader.size(), (int) vHeader.size()};
  GoSlice sSlice = {(void *) vSeed.data(), (int) vSeed.size(), (int) vSeed.size()};
  uint8_t pTarget[32];
  GoSlice hOut = {(void *) pTarget, 32, 32};
  ProofOfWorkHashCPU(hSlice, sSlice, hOut);
#endif

  //  first job target first before checking solved share
  string targetStr;
  Bin2Hex(pTarget, 32, targetStr);
  GoSlice text = {(void *) pTarget, 32, 32};
  uint64 localJobBits = Bytom_JobDifficultyToTargetCompact(difficulty);

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

}

void StratumMinerBytom::handleRequest_Submit(const string &idStr, const JsonNode &jparams) {
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

  uint8 shortJobId = (uint8) params["job_id"].uint32();

  LocalJob *localJob = session.findLocalJob(shortJobId);
  if (nullptr == localJob) {
    session.rpc2ResponseBoolean(idStr, false, "Block expired");
    LOG(ERROR) << "can not find local bytom job id=" << (int) shortJobId;
    return;
  }

  shared_ptr<StratumJobEx> exjob;
  exjob = server.GetJobRepository()->getStratumJobEx(localJob->jobId_);
  if (nullptr == exjob || nullptr == exjob->sjob_) {
    session.rpc2ResponseBoolean(idStr, false, "Block expired");
    LOG(ERROR) << "bytom local job not found " << std::hex << localJob->jobId_;
    return;
  }

  StratumJobBytom *sJob = dynamic_cast<StratumJobBytom *>(exjob->sjob_);
  if (nullptr == sJob) {
    session.rpc2ResponseBoolean(idStr, false, "Unknown reason");
    LOG(FATAL) << "Code error, casting stratum job bytom failed for job id=" << std::hex << localJob->jobId_;
    return;
  }

  //get header submission string and header hash string
  //  nonce in bytom B3Poisoned is using hex not decimal
  uint64 nonce = 0;
  {
    string nonceHex = params["nonce"].str();
    vector<char> nonceBinBuf;
    Hex2BinReverse(nonceHex.c_str(), nonceHex.length(), nonceBinBuf);
    nonce = *(uint64 *) nonceBinBuf.data();
    LOG(INFO) << idStr.c_str() << ": bytom handle request submit jobId " << (int) shortJobId
              << " with nonce: " << nonce << " - noncehex: " << nonceHex.c_str();
  }

  //  Check share duplication
  LocalShare localShare(nonce, 0, 0);
  if (!server.isEnableSimulator_ && !localJob->addLocalShare(localShare)) {
    session.responseError(idStr, StratumStatus::DUPLICATE_SHARE);
    // add invalid share to counter
    invalidSharesCounter_.insert((int64_t) time(nullptr), 1);
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

  //Check share
  ShareBytom share;
  //  ShareBase portion
  share.version_ = ShareBytom::CURRENT_VERSION;
  //  TODO: not set: share.checkSum_
  share.workerHashId_ = workerId_;
  share.userId_ = worker.userId_;
  share.status_ = StratumStatus::REJECT_NO_REASON;
  share.timestamp_ = (uint32_t) time(nullptr);
  share.ip_.fromIpv4Int(clientIp);

  //  ShareBytom portion
  share.jobId_ = localJob->jobId_;
  share.shareDiff_ = difficulty;
  share.blkBits_ = sJob->blockHeader_.bits;
  share.height_ = sJob->blockHeader_.height;

  auto StringToCheapHash = [](const std::string &str) -> uint64 {
    // int merkleRootLen = std::min(32, (int)str.length());
    auto merkleRootBegin = (uint8_t *) &str[0];
    // auto merkleRootEnd = merkleRootBegin + merkleRootLen;

    uint64 res;
    memcpy(&res, merkleRootBegin, std::min(8, (int) str.length()));
    return res;

    // vector<uint8_t> merkleRootBin(merkleRootBegin, merkleRootEnd);
    // return uint256(merkleRootBin).GetCheapHash();
  };

  share.combinedHeader_.blockCommitmentMerkleRootCheapHash_ =
      StringToCheapHash(sJob->blockHeader_.transactionsMerkleRoot);
  share.combinedHeader_.blockCommitmentStatusHashCheapHash_ =
      StringToCheapHash(sJob->blockHeader_.transactionStatusHash);
  share.combinedHeader_.timestamp_ = sJob->blockHeader_.timestamp;
  share.combinedHeader_.nonce_ = nonce;
  if (exjob->isStale()) {
    share.status_ = StratumStatus::JOB_NOT_FOUND;
    session.rpc2ResponseBoolean(idStr, false, "Block expired");
  } else {
    EncodeBlockHeader_return encoded = EncodeBlockHeader(sJob->blockHeader_.version,
                                                         sJob->blockHeader_.height,
                                                         (char *) sJob->blockHeader_.previousBlockHash.c_str(),
                                                         sJob->blockHeader_.timestamp,
                                                         nonce,
                                                         sJob->blockHeader_.bits,
                                                         (char *) sJob->blockHeader_.transactionStatusHash.c_str(),
                                                         (char *) sJob->blockHeader_.transactionsMerkleRoot.c_str());
    int powResult = BytomUtils::checkProofOfWork(encoded, sJob, difficulty);
    share.status_ = powResult;
    if (powResult == StratumStatus::SOLVED) {
      std::cout << "share solved\n";
      LOG(INFO) << "share solved";
      server.sendSolvedShare2Kafka(nonce,
                                   encoded.r0,
                                   share.height_,
                                   Bytom_TargetCompactToDifficulty(sJob->blockHeader_.bits),
                                   worker);
      server.GetJobRepository()->markAllJobsAsStale();
      handleShare(idStr, share.status_, share.shareDiff_);
    } else if (powResult == StratumStatus::ACCEPT) {
      handleShare(idStr, share.status_, share.shareDiff_);
    } else {
      std::string failMessage = "Unknown reason";
      switch (share.status_) {
      case StratumStatus::LOW_DIFFICULTY:failMessage = "Low difficulty share";
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
  if (!StratumStatus::isAccepted(share.status_)) {
    int64_t invalidSharesNum = invalidSharesCounter_.sum(time(nullptr), INVALID_SHARE_SLIDING_WINDOWS_SIZE);
    // too much invalid shares, don't send them to kafka
    if (invalidSharesNum >= INVALID_SHARE_SLIDING_WINDOWS_MAX_LIMIT) {
      isSendShareToKafka = false;
      LOG(WARNING) << "invalid share spamming, diff: "
                   << share.shareDiff_ << ", uid: " << worker.userId_
                   << ", uname: \"" << worker.userName_ << "\", ip: " << clientIp
                   << "checkshare result: " << share.status_;
    }
  }

  if (isSendShareToKafka) {
    share.checkSum_ = share.checkSum();
    server.sendShare2Kafka((const uint8_t *) &share, sizeof(ShareBytom));

    string shareInHex;
    Bin2Hex((uint8_t *) &share, sizeof(ShareBytom), shareInHex);
    LOG(INFO) << "\nsendShare2Kafka ShareBytom:\n"
              << "- size: " << sizeof(ShareBytom) << " bytes\n"
              << "- hexvalue: " << shareInHex.c_str() << "\n";

  }
}
