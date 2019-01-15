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

#include "rsk/RskSolvedShareData.h"

#include <arith_uint256.h>
#include "hash.h"
#include "primitives/block.h"

#include <boost/make_unique.hpp>

using namespace std;

//////////////////////////////////// JobRepositoryBitcoin /////////////////////////////////

StratumJob* JobRepositoryBitcoin::createStratumJob() {
  return new StratumJobBitcoin();
}

StratumJobEx* JobRepositoryBitcoin::createStratumJobEx(StratumJob *sjob, bool isClean)
{
  return new StratumJobExBitcoin(chainId_, sjob, isClean);
}


void JobRepositoryBitcoin::broadcastStratumJob(StratumJob *sjobBase) {
  StratumJobBitcoin* sjob = dynamic_cast<StratumJobBitcoin*>(sjobBase);
  if(!sjob)
  {
    LOG(FATAL) << "JobRepositoryBitcoin::broadcastStratumJob error: cast StratumJobBitcoin failed";
    return;
  }
  bool isClean = false;
  if (latestPrevBlockHash_ != sjob->prevHash_) {
    isClean = true;
    latestPrevBlockHash_ = sjob->prevHash_;
    LOG(INFO) << "received new height stratum job, height: " << sjob->height_
    << ", prevhash: " << sjob->prevHash_.ToString();
  }

  bool isMergedMiningClean = sjob->isMergedMiningCleanJob_;

  // 
  // The `clean_jobs` field should be `true` ONLY IF a new block found in Bitcoin blockchains.
  // Most miner implements will never submit their previous shares if the field is `true`.
  // There will be a huge loss of hashrates and earnings if the field is often `true`.
  // 
  // There is the definition from <https://slushpool.com/help/manual/stratum-protocol>:
  // 
  // clean_jobs - When true, server indicates that submitting shares from previous jobs
  // don't have a sense and such shares will be rejected. When this flag is set,
  // miner should also drop all previous jobs.
  // 
  shared_ptr<StratumJobEx> exJob(createStratumJobEx(sjob, isClean));
  {
    ScopeLock sl(lock_);

    if (isClean) {
      // mark all jobs as stale, should do this before insert new job
      for (auto it : exJobs_) {
        it.second->markStale();
      }
    }

    // insert new job
    exJobs_[sjob->jobId_] = exJob;
  }

  // if job has clean flag, call server to send job
  if (isClean || isMergedMiningClean) {
    sendMiningNotify(exJob);
    return;
  }

  // if last job is an empty block job(clean=true), we need to send a
  // new non-empty job as quick as possible.
  if (isClean == false && exJobs_.size() >= 2) {
    auto itr = exJobs_.rbegin();
    shared_ptr<StratumJobEx> exJob1 = itr->second;
    itr++;
    shared_ptr<StratumJobEx> exJob2 = itr->second;

    StratumJobBitcoin* sjob1 = dynamic_cast<StratumJobBitcoin*>(exJob1->sjob_);
    StratumJobBitcoin* sjob2 = dynamic_cast<StratumJobBitcoin*>(exJob2->sjob_);

    if (exJob2->isClean_ == true &&
        sjob2->merkleBranch_.size() == 0 &&
        sjob1->merkleBranch_.size() != 0) {
      sendMiningNotify(exJob);
    }
  }
}

StratumJobExBitcoin::StratumJobExBitcoin(size_t chainId, StratumJob *sjob, bool isClean)
  : StratumJobEx(chainId, sjob, isClean)
{
  init();
}


void StratumJobExBitcoin::init() {
  StratumJobBitcoin* sjob = dynamic_cast<StratumJobBitcoin*>(sjob_);
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
      merkleBranchStr.resize(merkleBranchStr.length() - 1);  // remove last ','
    }
  }

  // we don't put jobId here, session will fill with the shortJobId
  miningNotify1_ = "{\"id\":null,\"method\":\"mining.notify\",\"params\":[\"";

  miningNotify2_ = Strings::Format("\",\"%s\",\"",
                                   sjob->prevHashBeStr_.c_str());

  // coinbase1_ may be modified when USER_DEFINED_COINBASE enabled,
  // so put it into a single variable.
  coinbase1_ = sjob->coinbase1_.c_str();

  miningNotify3_ = Strings::Format("\",\"%s\""
                                   ",[%s]"
                                   ",\"%08x\",\"%08x\",\"%08x\",%s"
                                   "]}\n",
                                   sjob->coinbase2_.c_str(),
                                   merkleBranchStr.c_str(),
                                   sjob->nVersion_, sjob->nBits_, sjob->nTime_,
                                   isClean_ ? "true" : "false");
  // always set clean to true, reset of them is the same with miningNotify2_
  miningNotify3Clean_ = Strings::Format("\",\"%s\""
                                   ",[%s]"
                                   ",\"%08x\",\"%08x\",\"%08x\",true"
                                   "]}\n",
                                   sjob->coinbase2_.c_str(),
                                   merkleBranchStr.c_str(),
                                   sjob->nVersion_, sjob->nBits_, sjob->nTime_);

}


void StratumJobExBitcoin::generateCoinbaseTx(std::vector<char> *coinbaseBin,
                                      const uint32_t extraNonce1,
                                      const string &extraNonce2Hex,
                                      string *userCoinbaseInfo) {
  string coinbaseHex;
  const string extraNonceStr = Strings::Format("%08x%s", extraNonce1, extraNonce2Hex.c_str());
  StratumJobBitcoin* sjob = dynamic_cast<StratumJobBitcoin*>(sjob_);
  string coinbase1 = sjob->coinbase1_;

#ifdef USER_DEFINED_COINBASE
  if (userCoinbaseInfo != nullptr) {
    string userCoinbaseHex;
    Bin2Hex((uint8*)(*userCoinbaseInfo).c_str(), (*userCoinbaseInfo).size(), userCoinbaseHex);
    // replace the last `userCoinbaseHex.size()` bytes to `userCoinbaseHex`
    coinbase1.replace(coinbase1.size()-userCoinbaseHex.size(), userCoinbaseHex.size(), userCoinbaseHex);
  }
#endif

  coinbaseHex.append(coinbase1);
  coinbaseHex.append(extraNonceStr);
  coinbaseHex.append(sjob->coinbase2_);
  Hex2Bin((const char *)coinbaseHex.c_str(), *coinbaseBin);
}

void StratumJobExBitcoin::generateBlockHeader(CBlockHeader *header,
                                       std::vector<char> *coinbaseBin,
                                       const uint32_t extraNonce1,
                                       const string &extraNonce2Hex,
                                       const vector<uint256> &merkleBranch,
                                       const uint256 &hashPrevBlock,
                                       const uint32_t nBits, const int32_t nVersion,
                                       const uint32_t nTime, const uint32_t nonce,
                                       const uint32_t versionMask,
                                       string *userCoinbaseInfo) {
  generateCoinbaseTx(coinbaseBin, extraNonce1, extraNonce2Hex, userCoinbaseInfo);

  header->hashPrevBlock = hashPrevBlock;
  header->nVersion      = (nVersion ^ versionMask);
  header->nBits         = nBits;
  header->nTime         = nTime;
  header->nNonce        = nonce;

  // hashMerkleRoot
  header->hashMerkleRoot = Hash(coinbaseBin->begin(), coinbaseBin->end());

  for (const uint256 & step : merkleBranch) {
    header->hashMerkleRoot = Hash(BEGIN(header->hashMerkleRoot),
                                  END  (header->hashMerkleRoot),
                                  BEGIN(step),
                                  END  (step));
  }
}
////////////////////////////////// ServerBitcoin ///////////////////////////////
ServerBitcoin::ServerBitcoin()
  : ServerBase()
  , versionMask_(0)
{
}

ServerBitcoin::~ServerBitcoin()
{
  for (ChainVarsBitcoin &chain : chainsBitcoin_) {
    if (chain.kafkaProducerAuxSolvedShare_ != nullptr) {
      delete chain.kafkaProducerAuxSolvedShare_;
    }
    if (chain.kafkaProducerRskSolvedShare_ != nullptr) {
      delete chain.kafkaProducerRskSolvedShare_;
    }
  }
}

uint32_t ServerBitcoin::getVersionMask() const {
  return versionMask_;
}

bool ServerBitcoin::setupInternal(const libconfig::Config &config)
{
  config.lookupValue("sserver.version_mask", versionMask_);

    auto addChainVars = [&](
    const string &kafkaBrokers,
    const string &auxSolvedShareTopic,
    const string &rskSolvedShareTopic
  ) {
    chainsBitcoin_.push_back({
      new KafkaProducer(kafkaBrokers.c_str(), auxSolvedShareTopic.c_str(), RD_KAFKA_PARTITION_UA),
      new KafkaProducer(kafkaBrokers.c_str(), rskSolvedShareTopic.c_str(), RD_KAFKA_PARTITION_UA)
    });
  };

  bool multiChains = false;
  config.lookupValue("sserver.multi_chains", multiChains);

  if (multiChains) {
    const Setting &chains = config.lookup("chains");
    for (int i = 0; i < chains.getLength(); i++) {
      addChainVars(
        chains[i].lookup("kafka_brokers"),
        chains[i].lookup("auxpow_solved_share_topic"),
        chains[i].lookup("rsk_solved_share_topic")
      );
    }
    if (chains_.empty()) {
      LOG(FATAL) << "sserver.multi_chains enabled but chains empty!";
    }
  }
  else {
    addChainVars(
      config.lookup("kafka.brokers"),
      config.lookup("sserver.auxpow_solved_share_topic"),
      config.lookup("sserver.rsk_solved_share_topic")
    );
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
  const string &fileLastNotifyTime
) {
  return new JobRepositoryBitcoin(chainId, this, kafkaBrokers, consumerTopic, fileLastNotifyTime);
}

unique_ptr<StratumSession> ServerBitcoin::createConnection(struct bufferevent *bev, struct sockaddr *saddr, uint32_t sessionID)
{
  return boost::make_unique<StratumSessionBitcoin>(*this, bev, saddr, sessionID);
}

void ServerBitcoin::sendSolvedShare2Kafka(
  size_t chainId,
  const FoundBlock *foundBlock,
  const std::vector<char> &coinbaseBin
) {
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

int ServerBitcoin::checkShare(
  size_t chainId,
  const ShareBitcoin &share,
  const uint32_t extraNonce1, const string &extraNonce2Hex,
  const uint32_t nTime, const uint32_t nonce,
  const uint32_t versionMask,
  const uint256 &jobTarget, const string &workFullName,
  string *userCoinbaseInfo
) {
  shared_ptr<StratumJobEx> exJobPtrShared = GetJobRepository(chainId)->getStratumJobEx(share.jobid());
  StratumJobExBitcoin* exJobPtr = static_cast<StratumJobExBitcoin*>(exJobPtrShared.get());
  if (exJobPtr == nullptr) {
    return StratumStatus::JOB_NOT_FOUND;
  }
  StratumJobBitcoin *sjob = dynamic_cast<StratumJobBitcoin*>(exJobPtr->sjob_);

  if (exJobPtr->isStale()) {
    return StratumStatus::JOB_NOT_FOUND;
  }
  if (nTime < sjob->minTime_) {
    return StratumStatus::TIME_TOO_OLD;
  }
  if (nTime > sjob->nTime_ + 600) {
    return StratumStatus::TIME_TOO_NEW;
  }

  // check version mask
  if (versionMask != 0 && ((~versionMask_) & versionMask) != 0) {
    return StratumStatus::ILLEGAL_VERMASK;
  }

  CBlockHeader header;
  std::vector<char> coinbaseBin;
  exJobPtr->generateBlockHeader(&header, &coinbaseBin,
                                extraNonce1, extraNonce2Hex,
                                sjob->merkleBranch_, sjob->prevHash_,
                                sjob->nBits_, sjob->nVersion_, nTime, nonce,
                                versionMask,
                                userCoinbaseInfo);

#ifdef CHAIN_TYPE_LTC
    uint256 blkHash = header.GetPoWHash();
#else
  uint256 blkHash = header.GetHash();
#endif
  arith_uint256 bnBlockHash     = UintToArith256(blkHash);
  arith_uint256 bnNetworkTarget = UintToArith256(sjob->networkTarget_);

  //
  // found new block
  //
  if (isSubmitInvalidBlock_ == true || bnBlockHash <= bnNetworkTarget) {
    //
    // build found block
    //
    FoundBlock foundBlock;
    foundBlock.jobId_    = share.jobid();
    foundBlock.workerId_ = share.workerhashid();
    foundBlock.userId_   = share.userid();
    foundBlock.height_   = sjob->height_;
    memcpy(foundBlock.header80_, (const uint8_t *)&header, sizeof(CBlockHeader));
    snprintf(foundBlock.workerFullName_, sizeof(foundBlock.workerFullName_),
             "%s", workFullName.c_str());
    // send
    sendSolvedShare2Kafka(chainId, &foundBlock, coinbaseBin);

    // mark jobs as stale
    GetJobRepository(chainId)->markAllJobsAsStale();

    LOG(INFO) << ">>>> found a new block: " << blkHash.ToString()
    << ", jobId: " << share.jobid() << ", userId: " << share.userid()
    << ", by: " << workFullName << " <<<<";
  }

  // print out high diff share, 2^10 = 1024
  if ((bnBlockHash >> 10) <= bnNetworkTarget) {
    LOG(INFO) << "high diff share, blkhash: " << blkHash.ToString()
    << ", diff: " << TargetToDiff(blkHash)
    << ", networkDiff: " << TargetToDiff(sjob->networkTarget_)
    << ", by: " << workFullName;
  }

  //
  // found new RSK block
  //
  if (!sjob->blockHashForMergedMining_.empty() &&
      (isSubmitInvalidBlock_ == true || bnBlockHash <= UintToArith256(sjob->rskNetworkTarget_))) {
    //
    // build data needed to submit block to RSK
    //
    RskSolvedShareData shareData;
    shareData.jobId_    = share.jobid();
    shareData.workerId_ = share.workerhashid();
    shareData.userId_   = share.userid();
    // height = matching bitcoin block height
    shareData.height_   = sjob->height_;
    snprintf(shareData.feesForMiner_, sizeof(shareData.feesForMiner_), "%s", sjob->feesForMiner_.c_str());
    snprintf(shareData.rpcAddress_, sizeof(shareData.rpcAddress_), "%s", sjob->rskdRpcAddress_.c_str());
    snprintf(shareData.rpcUserPwd_, sizeof(shareData.rpcUserPwd_), "%s", sjob->rskdRpcUserPwd_.c_str());
    memcpy(shareData.header80_, (const uint8_t *)&header, sizeof(CBlockHeader));
    snprintf(shareData.workerFullName_, sizeof(shareData.workerFullName_), "%s", workFullName.c_str());
    
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
    << ", jobId: " << share.jobid() << ", userId: " << share.userid()
    << ", by: " << workFullName << " <<<<";
  }

  //
  // found namecoin block
  //
  if (sjob->nmcAuxBits_ != 0 &&
      (isSubmitInvalidBlock_ == true || bnBlockHash <= UintToArith256(sjob->nmcNetworkTarget_))) {
    //
    // build namecoin solved share message
    //
    string blockHeaderHex;
    Bin2Hex((const uint8_t *)&header, sizeof(CBlockHeader), blockHeaderHex);
    DLOG(INFO) << "blockHeaderHex: " << blockHeaderHex;

    string coinbaseTxHex;
    Bin2Hex((const uint8_t *)coinbaseBin.data(), coinbaseBin.size(), coinbaseTxHex);
    DLOG(INFO) << "coinbaseTxHex: " << coinbaseTxHex;

    const string auxSolvedShare = Strings::Format("{\"job_id\":%" PRIu64","
                                                     " \"aux_block_hash\":\"%s\","
                                                     " \"block_header\":\"%s\","
                                                     " \"coinbase_tx\":\"%s\","
                                                     " \"rpc_addr\":\"%s\","
                                                     " \"rpc_userpass\":\"%s\""
                                                     "}",
                                                     share.jobid(),
                                                     sjob->nmcAuxBlockHash_.ToString().c_str(),
                                                     blockHeaderHex.c_str(),
                                                     coinbaseTxHex.c_str(),
                                                     sjob->nmcRpcAddr_.size()     ? sjob->nmcRpcAddr_.c_str()     : "",
                                                     sjob->nmcRpcUserpass_.size() ? sjob->nmcRpcUserpass_.c_str() : "");
    // send found merged mining aux block to kafka
    sendAuxSolvedShare2Kafka(chainId, auxSolvedShare.data(), auxSolvedShare.size());

    LOG(INFO) << ">>>> found namecoin block: " << sjob->nmcHeight_ << ", "
    << sjob->nmcAuxBlockHash_.ToString()
    << ", jobId: " << share.jobid() << ", userId: " << share.userid()
    << ", by: " << workFullName << " <<<<";
  }

  DLOG(INFO) << "blkHash: " << blkHash.ToString() << ", jobTarget: "
  << jobTarget.ToString() << ", networkTarget: " << sjob->networkTarget_.ToString();

  // check share diff
  if (isEnableSimulator_ == false && bnBlockHash > UintToArith256(jobTarget)) {
    return StratumStatus::LOW_DIFFICULTY;
  }

  // reach here means an valid share
  return StratumStatus::ACCEPT;
}

void ServerBitcoin::sendAuxSolvedShare2Kafka(size_t chainId, const char *data, size_t len) {
  chainsBitcoin_[chainId].kafkaProducerAuxSolvedShare_->produce(data, len);
}

void ServerBitcoin::sendRskSolvedShare2Kafka(size_t chainId, const char *data, size_t len) {
  chainsBitcoin_[chainId].kafkaProducerRskSolvedShare_->produce(data, len);
}
