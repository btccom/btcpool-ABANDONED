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
#include "StratumServerBitcoin.h"
#include "StratumSessionBitcoin.h"
#include "StratumBitcoin.h"
#include "StratumMiner.h"
#include "StratumMinerBitcoin.h"
#include "BitcoinUtils.h"

#include "rsk/RskSolvedShareData.h"

#include "arith_uint256.h"
#include "hash.h"
#include "primitives/block.h"

using namespace std;

//////////////////////////////////// JobRepositoryBitcoin
////////////////////////////////////

shared_ptr<StratumJob> JobRepositoryBitcoin::createStratumJob() {
  return std::make_shared<StratumJobBitcoin>();
}

shared_ptr<StratumJobEx> JobRepositoryBitcoin::createStratumJobEx(
    shared_ptr<StratumJob> sjob, bool isClean) {
  return std::make_shared<StratumJobExBitcoin>(
      chainId_, sjob, isClean, GetServer()->extraNonce2Size());
}

void JobRepositoryBitcoin::broadcastStratumJob(
    shared_ptr<StratumJob> sjobBase) {
  auto sjob = std::static_pointer_cast<StratumJobBitcoin>(sjobBase);

  if (sjob->proxyExtraNonce2Size_ > 0 &&
      sjob->proxyExtraNonce2Size_ <
          StratumMiner::kExtraNonce1Size_ + GetServer()->extraNonce2Size()) {
    LOG(ERROR) << "CANNOT MINING, job discarded: JobExtraNonce2Size("
               << sjob->proxyExtraNonce2Size_ << ") < StratumExtraNonce1Size("
               << StratumMiner::kExtraNonce1Size_
               << ") + StratumExtraNonce2Size("
               << GetServer()->extraNonce2Size() << ")";
    return;
  }

  if (GetServer()->subPoolEnabled()) {
    auto itr = sjob->subPool_.find(GetServer()->subPoolName());
    if (itr != sjob->subPool_.end()) {
      sjob->coinbase1_ = itr->second.coinbase1_;
      sjob->coinbase2_ = itr->second.coinbase2_;
      sjob->grandCoinbase1_ = itr->second.grandCoinbase1_;
    } else {
      LOG(ERROR) << "CANNOT FIND COINBASE TX OF SUBPOOL "
                 << GetServer()->subPoolName()
                 << "! The main pool coinbase tx is used.";
    }
  }

  bool isClean = false;
  uint32_t height = sjob->height_;
  if (height > lastHeight_) {
    isClean = true;
    lastHeight_ = height;
    LOG(INFO) << "received new height " << GetServer()->chainName(chainId_)
              << " job, height: " << sjob->height_
              << ", prevhash: " << sjob->prevHash_.ToString();
  }

  bool isMergedMiningClean =
      sjob->isMergedMiningCleanJob_ && height >= lastHeight_;

  // In the job proxy mode, the upstream pool may switch the chain
  // (such as from BCH to BTC), resulting in a height drop.
  if (sjob->proxyJobDifficulty_ > 0 && height < lastHeight_) {
    LOG(INFO) << "upstream " << GetServer()->chainName(chainId_)
              << " switched chain, new height: " << sjob->height_
              << ", prevhash: " << sjob->prevHash_.ToString()
              << ", old height: " << lastHeight_;
    isMergedMiningClean = true;
    lastHeight_ = height;
  }

  //
  // The `clean_jobs` field should be `true` ONLY IF a new block found in
  // Bitcoin blockchains. Most miner implements will never submit their previous
  // shares if the field is `true`. There will be a huge loss of hashrates and
  // earnings if the field is often `true`.
  //
  // There is the definition from
  // <https://slushpool.com/help/manual/stratum-protocol>:
  //
  // clean_jobs - When true, server indicates that submitting shares from
  // previous jobs don't have a sense and such shares will be rejected. When
  // this flag is set, miner should also drop all previous jobs.
  //
  shared_ptr<StratumJobEx> exJob(createStratumJobEx(sjob, isClean));

  if (isClean) {
    // mark all jobs as stale, should do this before insert new job
    for (auto it : exJobs_) {
      it.second->markStale();
    }
  }

  // get the last job
  shared_ptr<StratumJobEx> lastExJob = nullptr;
  if (!exJobs_.empty()) {
    lastExJob = exJobs_.rbegin()->second;
  }

  // insert new job
  exJobs_[sjob->jobId_] = exJob;

  // if job has clean flag, call server to send job
  if (isClean || isMergedMiningClean) {
    sendMiningNotify(exJob);
    return;
  }

  // if last job is an empty block job, we need to send a new non-empty job
  // as quick as possible.
  if (lastExJob != nullptr) {
    auto lastSjob =
        std::static_pointer_cast<StratumJobBitcoin>(lastExJob->sjob_);

    if (lastSjob->merkleBranch_.size() == 0 &&
        sjob->merkleBranch_.size() != 0 && height >= lastHeight_) {
      sendMiningNotify(exJob);
    }
  }
}

StratumJobExBitcoin::StratumJobExBitcoin(
    size_t chainId,
    shared_ptr<StratumJob> sjob,
    bool isClean,
    uint32_t extraNonce2Size)
  : StratumJobEx(chainId, sjob, isClean) {
  init(extraNonce2Size);
}

void StratumJobExBitcoin::init(uint32_t extraNonce2Size) {
  auto sjob = std::static_pointer_cast<StratumJobBitcoin>(sjob_);

#ifdef CHAIN_TYPE_ZEC
  //
  // mining.notify()
  //   {"id": null, "method": "mining.notify",
  //    "params": ["JobID", "Version", "PrevHash", "MerkleRoot",
  //               "FinalSaplingRootHash", "Time", "Bits", CleanJobs]}
  //
  // we don't put jobId here, session will fill with the shortJobId
  miningNotify1_ = "{\"id\":null,\"method\":\"mining.notify\",\"params\":[\"";

  miningNotify2_ = "";
  coinbase1_ = "";

  const string prevHash = getNotifyHashStr(sjob->prevHash_);
  const string merkleRoot = getNotifyHashStr(sjob->merkleRoot_);
  const string finalSaplingRoot = getNotifyHashStr(sjob->finalSaplingRoot_);

  miningNotify3_ = Strings::Format(
      "\",\"%s\",\"%s\",\"%s\","
      "\"%s\",\"%s\",\"%s\",%s]}\n",
      getNotifyUint32Str(sjob->nVersion_).c_str(),
      prevHash.c_str(),
      merkleRoot.c_str(),
      finalSaplingRoot.c_str(),
      getNotifyUint32Str(sjob->nTime_).c_str(),
      getNotifyUint32Str(sjob->nBits_).c_str(),
      isClean_ ? "true" : "false");

  // always set clean to true, reset of them is the same with miningNotify2_
  miningNotify3Clean_ = Strings::Format(
      "\",\"%s\",\"%s\",\"%s\","
      "\"%s\",\"%s\",\"%s\",true]}\n",
      getNotifyUint32Str(sjob->nVersion_).c_str(),
      prevHash.c_str(),
      merkleRoot.c_str(),
      finalSaplingRoot.c_str(),
      getNotifyUint32Str(sjob->nTime_).c_str(),
      getNotifyUint32Str(sjob->nBits_).c_str());

#else
  string merkleBranchStr;
  {
    // '"'+ 64 + '"' + ',' = 67 bytes
    merkleBranchStr.reserve(sjob->merkleBranch_.size() * 67);
    for (size_t i = 0; i < sjob->merkleBranch_.size(); i++) {
      //
      // do NOT use GetHex() or uint256.ToString(), need to dump the memory
      //
      string merklStr;
      Bin2Hex(sjob->merkleBranch_[i].begin(), 32, merklStr);
      merkleBranchStr.append("\"" + merklStr + "\",");
    }
    if (merkleBranchStr.length()) {
      merkleBranchStr.resize(merkleBranchStr.length() - 1); // remove last ','
    }
  }

  // we don't put jobId here, session will fill with the shortJobId
  miningNotify1_ = "{\"id\":null,\"method\":\"mining.notify\",\"params\":[\"";

  miningNotify2_ = Strings::Format("\",\"%s\",\"", sjob->prevHashBeStr_);

  coinbase1_ = sjob->coinbase1_.c_str();
  grandCoinbase1_ = sjob->grandCoinbase1_.c_str();

  ssize_t jobExtraNonce2Size = StratumMiner::kExtraNonce2Size_;
  if (sjob->proxyExtraNonce2Size_ > 0) {
    // we use 4 bytes as extraNonce1
    jobExtraNonce2Size =
        sjob->proxyExtraNonce2Size_ - StratumMiner::kExtraNonce1Size_;
  }

  if (jobExtraNonce2Size > extraNonce2Size) {
    coinbase1_ += string((jobExtraNonce2Size - extraNonce2Size) * 2, '0');
  } else if (jobExtraNonce2Size < extraNonce2Size) {
    // This should not happen. Job should be discarded before this.
    LOG(ERROR) << "CANNOT MINING, code need a fix: JobExtraNonce2Size("
               << (sjob->proxyExtraNonce2Size_ > 0
                       ? sjob->proxyExtraNonce2Size_
                       : StratumMiner::kExtraNonce2Size_)
               << ") < StratumExtraNonce1Size("
               << StratumMiner::kExtraNonce1Size_
               << ") + StratumExtraNonce2Size(" << extraNonce2Size << ")";
  }

  miningNotify3_ = Strings::Format(
      "\",\"%s\""
      ",[%s]"
      ",\"%08x\",\"%08x\",\"%08x\",%s"
      "]}\n",
      sjob->coinbase2_,
      merkleBranchStr,
      sjob->nVersion_,
      sjob->nBits_,
      sjob->nTime_,
      isClean_ ? "true" : "false");
  // always set clean to true, reset of them is the same with miningNotify2_
  miningNotify3Clean_ = Strings::Format(
      "\",\"%s\""
      ",[%s]"
      ",\"%08x\",\"%08x\",\"%08x\",true"
      "]}\n",
      sjob->coinbase2_,
      merkleBranchStr,
      sjob->nVersion_,
      sjob->nBits_,
      sjob->nTime_);
#endif
}

void StratumJobExBitcoin::generateCoinbaseTx(
    std::vector<char> *coinbaseBin,
    const uint32_t extraNonce1,
    const string &extraNonce2Hex,
    const bool isGrandPoolClient,
    const uint32_t extraGrandNonce1) {
  string coinbaseHex;
  string extraNonceStr = Strings::Format("%08x%s", extraNonce1, extraNonce2Hex);
  auto sjob = std::static_pointer_cast<StratumJobBitcoin>(sjob_);
  string coinbase1 = sjob->coinbase1_;

  if (isGrandPoolClient) {
    coinbase1 = sjob->grandCoinbase1_;
    extraNonceStr = Strings::Format(
        "%08x%08x%s", extraNonce1, extraGrandNonce1, extraNonce2Hex);
  }

  coinbaseHex.append(coinbase1);
  coinbaseHex.append(extraNonceStr);
  coinbaseHex.append(sjob->coinbase2_);

  // DLOG(INFO) << "coinbase tx: " << coinbaseHex;
  Hex2Bin((const char *)coinbaseHex.c_str(), *coinbaseBin);
}

void StratumJobExBitcoin::generateBlockHeader(
    CBlockHeader *header,
    std::vector<char> *coinbaseBin,
    const uint32_t extraNonce1,
    const string &extraNonce2Hex,
    const vector<uint256> &merkleBranch,
    const uint256 &hashPrevBlock,
    const uint32_t nBits,
    const int32_t nVersion,
    const uint32_t nTime,
    const BitcoinNonceType nonce,
    const uint32_t versionMask,
    const bool isGrandPoolClient,
    const uint32_t extraGrandNonce1) {

  header->hashPrevBlock = hashPrevBlock;
  header->nVersion = (nVersion ^ versionMask);
  header->nBits = nBits;
  header->nTime = nTime;

#ifdef CHAIN_TYPE_ZEC
  header->nNonce = nonce.nonce;
  header->nSolution =
      ParseHex(nonce.solution.substr(getSolutionVintSize() * 2));

  auto sjob = std::static_pointer_cast<StratumJobBitcoin>(sjob_);

  header->hashMerkleRoot = sjob->merkleRoot_;
  header->hashFinalSaplingRoot = sjob->finalSaplingRoot_;

  Hex2Bin(sjob->coinbase1_.c_str(), sjob->coinbase1_.size(), *coinbaseBin);

#else
  header->nNonce = nonce;

  // compute merkle root
  generateCoinbaseTx(
      coinbaseBin,
      extraNonce1,
      extraNonce2Hex,
      isGrandPoolClient,
      extraGrandNonce1);
  header->hashMerkleRoot =
      ComputeCoinbaseMerkleRoot(*coinbaseBin, merkleBranch);
#endif
}

////////////////////////////////// ServerBitcoin ///////////////////////////////
ServerBitcoin::~ServerBitcoin() {
  for (ChainVarsBitcoin &chain : chainsBitcoin_) {
    if (chain.kafkaProducerAuxSolvedShare_ != nullptr) {
      delete chain.kafkaProducerAuxSolvedShare_;
    }
    if (chain.kafkaProducerRskSolvedShare_ != nullptr) {
      delete chain.kafkaProducerRskSolvedShare_;
    }
  }
}

bool ServerBitcoin::setupInternal(const libconfig::Config &config) {
  config.lookupValue("subpool.enabled", subPoolEnabled_);
  config.lookupValue("subpool.name", subPoolName_);
  config.lookupValue("subpool.ext_user_id", subPoolExtUserId_);
  if (subPoolEnabled_) {
    LOG(WARNING) << "[Option] "
                 << "Subpool " << subPoolName_ << " enabled";
  }
  if (subPoolExtUserId_ > 0) {
    LOG(FATAL) << "[Option] subpool.ext_user_id cannot > 0";
  }

  config.lookupValue("sserver.use_share_v1", useShareV1_);

  config.lookupValue("sserver.version_mask", versionMask_);
  config.lookupValue("sserver.extra_nonce2_size", extraNonce2Size_);
  if (extraNonce2Size_ > StratumMinerBitcoin::kMaxExtraNonce2Size_ ||
      extraNonce2Size_ < StratumMinerBitcoin::kMinExtraNonce2Size_) {
    LOG(ERROR) << "Wrong extraNonce2Size (should be 4~8): " << extraNonce2Size_;
    return false;
  }

  auto addChainVars = [&](const string &kafkaBrokers,
                          const string &auxSolvedShareTopic,
                          const string &rskSolvedShareTopic) {
    chainsBitcoin_.push_back({new KafkaProducer(
                                  kafkaBrokers.c_str(),
                                  auxSolvedShareTopic.c_str(),
                                  RD_KAFKA_PARTITION_UA),
                              new KafkaProducer(
                                  kafkaBrokers.c_str(),
                                  rskSolvedShareTopic.c_str(),
                                  RD_KAFKA_PARTITION_UA)});
  };

  bool multiChains = false;
  config.lookupValue("sserver.multi_chains", multiChains);

  if (multiChains) {
    const Setting &chains = config.lookup("chains");
    for (int i = 0; i < chains.getLength(); i++) {
      addChainVars(
          chains[i].lookup("kafka_brokers"),
          chains[i].lookup("auxpow_solved_share_topic"),
          chains[i].lookup("rsk_solved_share_topic"));
    }
    if (chains_.empty()) {
      LOG(FATAL) << "sserver.multi_chains enabled but chains empty!";
    }
  } else {
    addChainVars(
        config.lookup("kafka.brokers"),
        config.lookup("sserver.auxpow_solved_share_topic"),
        config.lookup("sserver.rsk_solved_share_topic"));
  }

  // kafkaProducerAuxSolvedShare_
  {
    map<string, string> options;
    // set to 1 (0 is an illegal value here), deliver msg as soon as possible.
    options["queue.buffering.max.ms"] = "1";

    for (ChainVarsBitcoin &chain : chainsBitcoin_) {
      if (!chain.kafkaProducerAuxSolvedShare_->setup(&options)) {
        LOG(ERROR) << "kafka kafkaProducerAuxSolvedShare_ setup failure";
        return false;
      }
      if (!chain.kafkaProducerAuxSolvedShare_->checkAlive()) {
        LOG(ERROR) << "kafka kafkaProducerAuxSolvedShare_ is NOT alive";
        return false;
      }
    }
  }

  // kafkaProducerRskSolvedShare_
  {
    map<string, string> options;
    // set to 1 (0 is an illegal value here), deliver msg as soon as possible.
    options["queue.buffering.max.ms"] = "1";

    for (ChainVarsBitcoin &chain : chainsBitcoin_) {
      if (!chain.kafkaProducerRskSolvedShare_->setup(&options)) {
        LOG(ERROR) << "kafka kafkaProducerRskSolvedShare_ setup failure";
        return false;
      }
      if (!chain.kafkaProducerRskSolvedShare_->checkAlive()) {
        LOG(ERROR) << "kafka kafkaProducerRskSolvedShare_ is NOT alive";
        return false;
      }
    }
  }

  return true;
}

JobRepository *ServerBitcoin::createJobRepository(
    size_t chainId,
    const char *kafkaBrokers,
    const char *consumerTopic,
    const string &fileLastNotifyTime,
    bool niceHashForced,
    uint64_t niceHashMinDiff,
    const std::string &niceHashMinDiffZookeeperPath) {
  return new JobRepositoryBitcoin(
      chainId,
      this,
      kafkaBrokers,
      consumerTopic,
      fileLastNotifyTime,
      niceHashForced,
      niceHashMinDiff,
      niceHashMinDiffZookeeperPath);
}

unique_ptr<StratumSession> ServerBitcoin::createConnection(
    struct bufferevent *bev, struct sockaddr *saddr, uint32_t sessionID) {
  return std::make_unique<StratumSessionBitcoin>(*this, bev, saddr, sessionID);
}

void ServerBitcoin::sendSolvedShare2Kafka(
    size_t chainId,
    const FoundBlock *foundBlock,
    const std::vector<char> &coinbaseBin) {
  //
  // solved share message:  FoundBlock + coinbase_Tx
  //
  string buf;
  buf.resize(sizeof(FoundBlock) + coinbaseBin.size());
  uint8_t *p = (uint8_t *)buf.data();

  // FoundBlock
  memcpy(p, (const uint8_t *)foundBlock, sizeof(FoundBlock));
  p += sizeof(FoundBlock);

  // coinbase TX
  memcpy(p, coinbaseBin.data(), coinbaseBin.size());

  ServerBase::sendSolvedShare2Kafka(chainId, buf.data(), buf.size());
}

void ServerBitcoin::checkShare(
    size_t chainId,
    const ShareBitcoin &share,
    uint32_t extraNonce1,
    const string &extraNonce2Hex,
    const uint32_t nTime,
    const BitcoinNonceType nonce,
    const uint32_t versionMask,
    const uint256 &jobTarget,
    const string &workFullName,
    const bool isGrandPoolClient,
    const uint32_t extraGrandNonce1,
    std::function<void(int32_t status, uint32_t bitsReached)> returnFn) {

  auto exJobPtr = std::static_pointer_cast<StratumJobExBitcoin>(
      GetJobRepository(chainId)->getStratumJobEx(share.jobid()));
  int32_t shareStatus = StratumStatus::UNKNOWN; // init shareStatus

  if (exJobPtr == nullptr) {
    returnFn(StratumStatus::JOB_NOT_FOUND, 0);
    return;
  }

  if (exJobPtr->isStale()) {
    shareStatus = StratumStatus::STALE_SHARE;
  }

  auto sjob = std::static_pointer_cast<StratumJobBitcoin>(exJobPtr->sjob_);

  if (StratumStatus::UNKNOWN == shareStatus && nTime < sjob->minTime_) {
    shareStatus = StratumStatus::TIME_TOO_OLD;
  }
  if (StratumStatus::UNKNOWN == shareStatus && nTime > sjob->nTime_ + 600) {
    shareStatus = StratumStatus::TIME_TOO_NEW;
  }

  // check version mask
  if (StratumStatus::UNKNOWN == shareStatus && versionMask != 0 &&
      ((~versionMask_) & versionMask) != 0) {
    shareStatus = StratumStatus::ILLEGAL_VERMASK;
  }

  CBlockHeader header;
  std::vector<char> coinbaseBin;
  exJobPtr->generateBlockHeader(
      &header,
      &coinbaseBin,
      extraNonce1,
      extraNonce2Hex,
      sjob->merkleBranch_,
      sjob->prevHash_,
      sjob->nBits_,
      sjob->nVersion_,
      nTime,
      nonce,
      versionMask,
      isGrandPoolClient,
      extraGrandNonce1);

  dispatchToShareWorker([this,
                         chainId,
                         share,
                         jobTarget,
                         shareStatus,
                         workFullName,
                         returnFn = std::move(returnFn),
                         sjob,
                         header,
                         coinbaseBin]() {
    int32_t shareStatusReturn = shareStatus;
#ifdef CHAIN_TYPE_LTC
    uint256 blkHash = header.GetPoWHash();
#else
    uint256 blkHash = header.GetHash();
#endif
    arith_uint256 bnBlockHash = UintToArith256(blkHash);
    arith_uint256 bnNetworkTarget = UintToArith256(sjob->networkTarget_);
    uint32_t bitsReached = bnBlockHash.GetCompact();

#ifdef CHAIN_TYPE_ZEC
    DLOG(INFO) << Strings::Format(
        "CBlockHeader nVersion: %08x, hashPrevBlock: %s, hashMerkleRoot: %s, "
        "hashFinalSaplingRoot: %s, nTime: %08x, nBits: %08x, nNonce: %s",
        header.nVersion,
        header.hashPrevBlock.ToString().c_str(),
        header.hashMerkleRoot.ToString().c_str(),
        header.hashFinalSaplingRoot.ToString().c_str(),
        header.nTime,
        header.nBits,
        header.nNonce.ToString().c_str());

    // check equihash solution
    if (isEnableSimulator_ == false &&
        CheckEquihashSolution(&header, Params()) == false) {
      if (StratumStatus::UNKNOWN == shareStatusReturn) {
        shareStatusReturn = StratumStatus::INVALID_SOLUTION;
      }
      dispatch(
          [shareStatusReturn, bitsReached, returnFn = std::move(returnFn)]() {
            returnFn(shareStatusReturn, bitsReached);
          });
      return;
    }
#endif

    //
    // found new block
    //
    if (StratumStatus::UNKNOWN == shareStatusReturn &&
        (isSubmitInvalidBlock_ == true || bnBlockHash <= bnNetworkTarget)) {
      //
      // found new block
      //
      FoundBlock foundBlock;
      foundBlock.jobId_ = share.jobid();
      foundBlock.workerId_ = share.workerhashid();
      foundBlock.userId_ =
          singleUserMode() ? singleUserId(chainId) : share.userid();
      foundBlock.height_ = sjob->height_;
      foundBlock.headerData_.set(header);
      snprintf(
          foundBlock.workerFullName_,
          sizeof(foundBlock.workerFullName_),
          "%s",
          workFullName.c_str());

      // send
      sendSolvedShare2Kafka(chainId, &foundBlock, coinbaseBin);

      if (sjob->proxyJobDifficulty_ > 0) {
        LOG(INFO) << ">>>> [" << chainName(chainId)
                  << "] solution found: " << blkHash.ToString()
                  << ", jobId: " << share.jobid()
                  << ", userId: " << share.userid() << ", by: " << workFullName
                  << " <<<<";
      } else {
        dispatch([this, chainId, height = sjob->height_]() {
          // mark jobs as stale
          GetJobRepository(chainId)->markAllJobsAsStale(height);
        });

        LOG(INFO) << ">>>> [" << chainName(chainId)
                  << "] found a new block: " << blkHash.ToString()
                  << ", jobId: " << share.jobid()
                  << ", userId: " << share.userid() << ", by: " << workFullName
                  << " <<<<";
      }
    }

    // print out high diff share, 2^10 = 1024
    if (sjob->proxyJobDifficulty_ == 0 &&
        (bnBlockHash >> 10) <= bnNetworkTarget) {
      LOG(INFO) << "high diff share, blkhash: " << blkHash.ToString()
                << ", diff: " << BitcoinDifficulty::TargetToDiff(blkHash)
                << ", networkDiff: "
                << BitcoinDifficulty::TargetToDiff(sjob->networkTarget_)
                << ", by: " << workFullName;
    }

    //
    // found new RSK block
    //
    if (!sjob->blockHashForMergedMining_.empty() &&
        (isSubmitInvalidBlock_ == true ||
         bnBlockHash <= UintToArith256(sjob->rskNetworkTarget_))) {
      //
      // build data needed to submit block to RSK
      //
      RskSolvedShareData shareData;
      shareData.jobId_ = share.jobid();
      shareData.workerId_ = share.workerhashid();
      shareData.userId_ = share.userid();
      // height = matching bitcoin block height
      shareData.height_ = sjob->height_;
      snprintf(
          shareData.feesForMiner_,
          sizeof(shareData.feesForMiner_),
          "%s",
          sjob->feesForMiner_.c_str());
      snprintf(
          shareData.rpcAddress_,
          sizeof(shareData.rpcAddress_),
          "%s",
          sjob->rskdRpcAddress_.c_str());
      snprintf(
          shareData.rpcUserPwd_,
          sizeof(shareData.rpcUserPwd_),
          "%s",
          sjob->rskdRpcUserPwd_.c_str());
      shareData.headerData_.set(header);
      snprintf(
          shareData.workerFullName_,
          sizeof(shareData.workerFullName_),
          "%s",
          workFullName.c_str());

      //
      // send to kafka topic
      //
      string buf;
      buf.resize(sizeof(RskSolvedShareData) + coinbaseBin.size());
      uint8_t *p = (uint8_t *)buf.data();

      // RskSolvedShareData
      memcpy(p, (const uint8_t *)&shareData, sizeof(RskSolvedShareData));
      p += sizeof(RskSolvedShareData);

      // coinbase TX
      memcpy(p, coinbaseBin.data(), coinbaseBin.size());

      sendRskSolvedShare2Kafka(chainId, buf.data(), buf.size());

      //
      // log the finding
      //
      LOG(INFO) << ">>>> found a new RSK block: " << blkHash.ToString()
                << ", jobId: " << share.jobid()
                << ", userId: " << share.userid() << ", by: " << workFullName
                << " <<<<";
    }

    //
    // found namecoin block
    //
    if (sjob->nmcAuxBits_ != 0 &&
        (isSubmitInvalidBlock_ == true ||
         bnBlockHash <= UintToArith256(sjob->nmcNetworkTarget_))) {
      //
      // build namecoin solved share message
      //
      string blockHeaderHex;
      Bin2Hex((const uint8_t *)&header, sizeof(CBlockHeader), blockHeaderHex);
      DLOG(INFO) << "blockHeaderHex: " << blockHeaderHex;

      string coinbaseTxHex;
      Bin2Hex(
          (const uint8_t *)coinbaseBin.data(),
          coinbaseBin.size(),
          coinbaseTxHex);
      DLOG(INFO) << "coinbaseTxHex: " << coinbaseTxHex;

      const string auxSolvedShare = Strings::Format(
          "{"
          "\"job_id\":%u,"
          "\"aux_block_hash\":\"%s\","
          "\"block_header\":\"%s\","
          "\"coinbase_tx\":\"%s\","
          "\"rpc_addr\":\"%s\","
          "\"rpc_userpass\":\"%s\""
          "}",
          share.jobid(),
          sjob->nmcAuxBlockHash_.ToString(),
          blockHeaderHex,
          coinbaseTxHex,
          sjob->nmcRpcAddr_,
          sjob->nmcRpcUserpass_);
      // send found merged mining aux block to kafka
      sendAuxSolvedShare2Kafka(
          chainId, auxSolvedShare.data(), auxSolvedShare.size());

      LOG(INFO) << ">>>> found namecoin block: " << sjob->nmcHeight_ << ", "
                << sjob->nmcAuxBlockHash_.ToString()
                << ", jobId: " << share.jobid()
                << ", userId: " << share.userid() << ", by: " << workFullName
                << " <<<<";
    }

    DLOG(INFO) << "blkHash: " << blkHash.ToString()
               << ", jobTarget: " << jobTarget.ToString()
               << ", networkTarget: " << sjob->networkTarget_.ToString();

    // check share diff
    if (StratumStatus::UNKNOWN == shareStatusReturn &&
        isEnableSimulator_ == false &&
        bnBlockHash > UintToArith256(jobTarget)) {
      shareStatusReturn = StratumStatus::LOW_DIFFICULTY;
    }

    // reach here and shareStatusReturn is initvalue means an valid share
    if (StratumStatus::UNKNOWN == shareStatusReturn)
      shareStatusReturn = StratumStatus::ACCEPT;

    dispatch(
        [shareStatusReturn, bitsReached, returnFn = std::move(returnFn)]() {
          returnFn(shareStatusReturn, bitsReached);
        });
    return;
  });
}

void ServerBitcoin::sendAuxSolvedShare2Kafka(
    size_t chainId, const char *data, size_t len) {
  chainsBitcoin_[chainId].kafkaProducerAuxSolvedShare_->produce(data, len);
}

void ServerBitcoin::sendRskSolvedShare2Kafka(
    size_t chainId, const char *data, size_t len) {
  chainsBitcoin_[chainId].kafkaProducerRskSolvedShare_->produce(data, len);
}
