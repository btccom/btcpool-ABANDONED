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
#include "StratumSessionBytom.h"
#include "Utils.h"
#include "utilities_js.hpp"
#include <arith_uint256.h>
#include <arpa/inet.h>
#include <boost/algorithm/string.hpp>
#include "bytom/bh_shared.h"
#include "StratumServer.h"

#ifndef NO_CUDA
#include "bytom/cutil/src/GpuTs.h"
#endif  //NO_CUDA

/////////////////////////////StratumSessionBytom////////////////////////////
StratumSessionBytom::StratumSessionBytom(evutil_socket_t fd, struct bufferevent *bev,
                                         Server *server, struct sockaddr *saddr,
                                         const int32_t shareAvgSeconds, const uint32_t extraNonce1) : StratumSession(fd,
                                                                                                                     bev,
                                                                                                                     server,
                                                                                                                     saddr,
                                                                                                                     shareAvgSeconds,
                                                                                                                     extraNonce1),
                                                                                                                     shortJobId_(1)
{
}

void StratumSessionBytom::handleRequest_Authorize(const string &idStr, const JsonNode &jparams, const JsonNode &/*jroot*/)
{
  state_ = SUBSCRIBED;
  auto params = const_cast<JsonNode&> (jparams);
  string fullName = params["login"].str();
  string pwd = params["pass"].str();
  checkUserAndPwd(idStr, fullName, pwd);
}

void StratumSessionBytom::sendMiningNotify(shared_ptr<StratumJobEx> exJobPtr, bool isFirstJob)
{
  /*
    Bytom difficulty logic (based on B3-Mimic repo)
    - constants
      * Diff1: 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF
    Sending miningNotify
    - target
      Pool target is based from Diff1 and difficulty. target = Diff1 / difficulty
    Miner difficulty logic:
    - use target
    Pool check submit (see StratumSessionBytom::handleRequest_Submit)
  */
  if (state_ < AUTHENTICATED || nullptr == exJobPtr)
  {
    LOG(ERROR) << "bytom sendMiningNotify failed, state: " << state_;
    return;
  }

  StratumJobBytom *sJob = dynamic_cast<StratumJobBytom *>(exJobPtr->sjob_);
  if (nullptr == sJob)
    return;

  localJobs_.push_back(LocalJob());
  LocalJob &ljob = *(localJobs_.rbegin());
  ljob.jobId_ = sJob->jobId_;
  ljob.shortJobId_ = shortJobId_++;

  if (server_->isDevModeEnable_)
  {
    ljob.jobDifficulty_ = server_->minerDifficulty_;
  }
  else
  {
    ljob.jobDifficulty_ = diffController_->calcCurDiff();
  }


  uint64 nonce = (((uint64)extraNonce1_) << 32);
  string notifyStr, nonceStr, versionStr, heightStr, timestampStr, bitsStr;
  Bin2HexR((uint8 *)&nonce, 8, nonceStr);
  Bin2Hex((uint8 *)&sJob->blockHeader_.version, 8, versionStr);
  Bin2Hex((uint8 *)&sJob->blockHeader_.height, 8, heightStr);
  Bin2Hex((uint8 *)&sJob->blockHeader_.timestamp, 8, timestampStr);
  Bin2Hex((uint8 *)&sJob->blockHeader_.bits, 8, bitsStr);

  string targetStr;
  {
    vector<uint8_t> targetBin;
    Bytom_DifficultyToTargetBinary(ljob.jobDifficulty_, targetBin);
    //  trim the zeroes to reduce bandwidth
    unsigned int endIdx = targetBin.size() - 1;
    for(; endIdx > 0; --endIdx)  //  > 0 (not >=0) because need to print at least 1 byte
    {
      if(targetBin[endIdx] != 0)
        break;
    }
    //  reversed based on logic seen in B3-Mimic. Miner expect reversed hex
    Bin2HexR(targetBin.data(), endIdx + 1, targetStr);  
  }

  string jobString = Strings::Format(
    "{\"version\": \"%s\","
        "\"height\": \"%s\","
        "\"previous_block_hash\": \"%s\","
        "\"timestamp\": \"%s\","
        "\"transactions_merkle_root\": \"%s\","
        "\"transaction_status_hash\": \"%s\","
        "\"nonce\": \"%s\","
        "\"bits\": \"%s\","
        "\"job_id\": \"%d\","
        "\"seed\": \"%s\","
        "\"target\": \"%s\"}",
        versionStr.c_str(),
        heightStr.c_str(),
        sJob->blockHeader_.previousBlockHash.c_str(),
        timestampStr.c_str(),
        sJob->blockHeader_.transactionsMerkleRoot.c_str(),
        sJob->blockHeader_.transactionStatusHash.c_str(),
        nonceStr.c_str(),
        bitsStr.c_str(),
        ljob.shortJobId_,
        sJob->seed_.c_str(),
        targetStr.c_str());   
  
  if (isFirstJob)
  {
    notifyStr = Strings::Format(
        "{\"id\": 1, \"jsonrpc\": \"2.0\", \"result\": {\"id\": \"%s\", \"job\": %s, \"status\": \"OK\"}, \"error\": null}",
        server_->isDevModeEnable_ ? "antminer_1" : worker_.fullName_.c_str(),
        jobString.c_str());
  }
  else
  {
    notifyStr = Strings::Format(
        "{\"jsonrpc\": \"2.0\", \"method\":\"job\", \"params\": %s}",
        jobString.c_str());
  }
  // LOG(INFO) << "Difficulty: " << ljob.jobDifficulty_ << "\nsendMiningNotify " << notifyStr.c_str();
  sendData(notifyStr);
}

void StratumSessionBytom::handleRequest_GetWork(const string &idStr, const JsonNode &jparams) {
    sendMiningNotify(server_->jobRepository_->getLatestStratumJobEx(), false);
}

namespace BytomUtils
{

int checkProofOfWork(EncodeBlockHeader_return encoded, StratumJobBytom *sJob, StratumSession::LocalJob *localJob)
{
  DLOG(INFO) << "verify blockheader hash=" << encoded.r1 << ", seed=" << sJob->seed_;
  vector<char> vHeader, vSeed;
  Hex2Bin(encoded.r1, vHeader);
  Hex2Bin(sJob->seed_.c_str(), sJob->seed_.length(), vSeed);

#ifndef NO_CUDA
  uint8_t *pTarget = GpuTs((uint8_t*)vHeader.data(), (uint8_t*)vSeed.data());
#else    
  GoSlice hSlice = {(void *)vHeader.data(), (int)vHeader.size(), (int)vHeader.size()};
  GoSlice sSlice = {(void *)vSeed.data(), (int)vSeed.size(), (int)vSeed.size()};
  uint8_t pTarget[32];
  GoSlice hOut = {(void *)pTarget, 32, 32};
  ProofOfWorkHashCPU(hSlice, sSlice, hOut);
#endif

  //  first job target first before checking solved share
  string targetStr;
  Bin2Hex(pTarget, 32, targetStr);
  GoSlice text = {(void *)pTarget, 32, 32};
  uint64 localJobBits = Bytom_JobDifficultyToTargetCompact(localJob->jobDifficulty_);  

  bool powResultLocalJob = CheckProofOfWork(text, localJobBits);
  if(powResultLocalJob)
  {
    //  passed job target, now check the blockheader target
    bool powResultBlock = CheckProofOfWork(text, sJob->blockHeader_.bits);
    if(powResultBlock)
    {
      return StratumStatus::SOLVED;
    }
    return StratumStatus::ACCEPT;
  }
  else
  {
    return StratumStatus::LOW_DIFFICULTY;
  }

  return StratumStatus::REJECT_NO_REASON;
}

}


void StratumSessionBytom::handleRequest_Submit(const string &idStr, const JsonNode &jparams)
{
  /*
    Calculating difficulty
    - Nonce. B3-Mimic send hex value.

    - Job PoW bits. Bits to check proof of work of job (not block)
      see CalculateTargetCompactByDifficulty (bh_shared.go). 

    - Block PoW bits. Use BlockHeader.Bits
  */
  LOG(INFO) << idStr.c_str() << ": bytom handle request submit";
  JsonNode &params = const_cast<JsonNode &>(jparams);

  ServerBytom *s = dynamic_cast<ServerBytom*> (server_);
  if (s == nullptr) {
    responseError(idStr, StratumStatus::REJECT_NO_REASON);
    LOG(FATAL) << "Code error, casting Server to ServerBytom failed";
    //  should we assert here?
    return;
  }

  uint8 shortJobId = (uint8)params["job_id"].uint32();

  LocalJob *localJob= findLocalJob(shortJobId);
  if (nullptr == localJob)
  {
    responseError(idStr, StratumStatus::JOB_NOT_FOUND);
    LOG(ERROR) << "can not find local bytom job id=" << (int)shortJobId;
    return;
  }

  shared_ptr<StratumJobEx> exjob;
  exjob = server_->jobRepository_->getStratumJobEx(localJob->jobId_);
  if (nullptr == exjob || nullptr == exjob->sjob_)
  {
    responseError(idStr, StratumStatus::JOB_NOT_FOUND);
    LOG(ERROR) << "bytom local job not found " << std::hex << localJob->jobId_;
    return;
  }

  StratumJobBytom *sJob = dynamic_cast<StratumJobBytom *>(exjob->sjob_);
  if (nullptr == sJob)
  {
    responseError(idStr, StratumStatus::REJECT_NO_REASON);
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
    nonce = *(uint64*)nonceBinBuf.data();
    LOG(INFO) << idStr.c_str() << ": bytom handle request submit jobId " << (int)shortJobId
              << " with nonce: " << nonce << " - noncehex: " << nonceHex.c_str();
  }

  //  Check share duplication
  LocalShare localShare(nonce, 0, 0);
  if (!server_->isEnableSimulator_ && !localJob->addLocalShare(localShare))
  {
    responseError(idStr, StratumStatus::DUPLICATE_SHARE);
    // add invalid share to counter
    invalidSharesCounter_.insert((int64_t)time(nullptr), 1);
    return;
  }

  //Check share
  ShareBytom share;
  //  ShareBase portion
  share.version_ = ShareBytom::CURRENT_VERSION;
  //  TODO: not set: share.checkSum_
  share.workerHashId_ = worker_.workerHashId_;
  share.userId_ = worker_.userId_;
  share.status_ = StratumStatus::REJECT_NO_REASON;
  share.timestamp_ = (uint32_t)time(nullptr);
  share.ip_ = clientIpInt_;

  //  ShareBytom portion
  share.jobId_ = localJob->jobId_;
  share.shareDiff_ = localJob->jobDifficulty_;
  share.blkBits_ = sJob->blockHeader_.bits;
  share.height_ = sJob->blockHeader_.height;
  
  auto StringToCheapHash = [](const std::string& str) -> uint64
  {
    int merkleRootLen = std::min(32, (int)str.length());
    auto merkleRootBegin = (uint8_t*)&str[0];
    auto merkleRootEnd = merkleRootBegin + merkleRootLen;

    vector<uint8_t> merkleRootBin(merkleRootBegin, merkleRootEnd);
    return uint256(merkleRootBin).GetCheapHash();
  };

  share.combinedHeader_.blockCommitmentMerkleRootCheapHash_ = StringToCheapHash(sJob->blockHeader_.transactionsMerkleRoot);
  share.combinedHeader_.blockCommitmentStatusHashCheapHash_ = StringToCheapHash(sJob->blockHeader_.transactionStatusHash);
  share.combinedHeader_.timestamp_ = sJob->blockHeader_.timestamp;
  share.combinedHeader_.nonce_ = nonce;
  if(exjob->isStale())
  {
    share.status_ = StratumStatus::JOB_NOT_FOUND;
    responseError(idStr, StratumStatus::JOB_NOT_FOUND);
  }
  else
  {
    EncodeBlockHeader_return encoded = EncodeBlockHeader(sJob->blockHeader_.version, sJob->blockHeader_.height, (char *)sJob->blockHeader_.previousBlockHash.c_str(), sJob->blockHeader_.timestamp,
                                    nonce, sJob->blockHeader_.bits, (char *)sJob->blockHeader_.transactionStatusHash.c_str(), (char *)sJob->blockHeader_.transactionsMerkleRoot.c_str());
    int powResult = BytomUtils::checkProofOfWork(encoded, sJob, localJob);
    switch(powResult)
    {
      case StratumStatus::SOLVED:
        LOG(INFO) << "share solved";
        s->sendSolvedShare2Kafka(nonce, encoded.r0, share.height_, Bytom_TargetCompactToDifficulty(sJob->blockHeader_.bits), worker_);
        server_->jobRepository_->markAllJobsAsStale();      
        //  do not put break here! It needs to run "ACCEPT" code.
      case StratumStatus::ACCEPT:
        diffController_->addAcceptedShare(share.shareDiff_);
        rpc2ResponseBoolean(idStr, true);
        break;
    }
    free(encoded.r0);
    free(encoded.r1);

    share.status_ = powResult;
  }


  bool isSendShareToKafka = true;
  DLOG(INFO) << share.toString();
  // check if thers is invalid share spamming
  if (!StratumStatus::isAccepted(share.status_))
  {
    int64_t invalidSharesNum = invalidSharesCounter_.sum(time(nullptr), INVALID_SHARE_SLIDING_WINDOWS_SIZE);
    // too much invalid shares, don't send them to kafka
    if (invalidSharesNum >= INVALID_SHARE_SLIDING_WINDOWS_MAX_LIMIT)
    {
      isSendShareToKafka = false;
      LOG(WARNING) << "invalid share spamming, diff: "
                   << share.shareDiff_ << ", uid: " << worker_.userId_
                   << ", uname: \"" << worker_.userName_ << "\", ip: " << clientIp_
                   << "checkshare result: " << share.status_;
    }
  }

  if (isSendShareToKafka)
  {
    share.checkSum_ = share.checkSum();
    server_->sendShare2Kafka((const uint8_t *)&share, sizeof(ShareBytom));

    string shareInHex;
    Bin2Hex((uint8_t*)&share, sizeof(ShareBytom), shareInHex);
    LOG(INFO) << "\nsendShare2Kafka ShareBytom:\n" 
              << "- size: " << sizeof(ShareBytom) << " bytes\n"
              << "- hexvalue: " << shareInHex.c_str() << "\n";

  }
  
}

bool StratumSessionBytom::validate(const JsonNode &jmethod, const JsonNode &jparams)
{
  if (jmethod.type() == Utilities::JS::type::Str &&
      jmethod.size() != 0 &&
      jparams.type() == Utilities::JS::type::Obj)
  {
    return true;
  }

  return false;
}
