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
#include "JobMakerBitcoin.h"
#include "CommonBitcoin.h"
#include "StratumBitcoin.h"
#include "BitcoinUtils.h"

#include <iostream>
#include <stdlib.h>

#include <glog/logging.h>
#include <librdkafka/rdkafka.h>

#include <hash.h>
#include <script/script.h>
#include <uint256.h>
#include <util.h>

#ifdef INCLUDE_BTC_KEY_IO_H //
#include <key_io.h> //  IsValidDestinationString for bch is not in this file.
#endif

#if defined(CHAIN_TYPE_BSV)
bool fRequireStandard = true;
#endif

#include "utilities_js.hpp"
#include "Utils.h"

#include <nlohmann/json.hpp>

using JSON = nlohmann::json;
using JSONException = nlohmann::detail::exception;

////////////////////////////////JobMakerHandlerBitcoin//////////////////////////////////
JobMakerHandlerBitcoin::JobMakerHandlerBitcoin()
  : currBestHeight_(0)
  , lastJobSendTime_(0)
  , isLastJobEmptyBlock_(false)
  , latestNmcAuxBlockHeight_(0)
  , previousRskWork_(nullptr)
  , currentRskWork_(nullptr)
  , isMergedMiningUpdate_(false)
  , previousVcashWork_(nullptr)
  , currentVcashWork_(nullptr) {
}

bool JobMakerHandlerBitcoin::init(shared_ptr<JobMakerDefinition> defPtr) {
  JobMakerHandler::init(defPtr);

  // select chain
  if (def()->testnet_) {
    SelectParams(CBaseChainParams::TESTNET);
    LOG(WARNING) << "using bitcoin testnet3";
  } else {
    SelectParams(CBaseChainParams::MAIN);
  }

  LOG(INFO) << "Block Version: " << std::hex << def()->blockVersion_;
  LOG(INFO) << "Coinbase Info: " << def()->coinbaseInfo_;
  LOG(INFO) << "Payout Address: " << def()->payoutAddr_;

  // check pool payout address
  if (!BitcoinUtils::IsValidDestinationString(def()->payoutAddr_)) {
    LOG(ERROR) << "invalid pool payout address";
    return false;
  }
  poolPayoutAddr_ = BitcoinUtils::DecodeDestination(def()->payoutAddr_);

  return true;
}

bool JobMakerHandlerBitcoin::initConsumerHandlers(
    const string &kafkaBrokers, vector<JobMakerConsumerHandler> &handlers) {
  std::vector<std::tuple<std::string, int>> topics{
      std::make_tuple(def()->rawGbtTopic_, 0),
      std::make_tuple(def()->auxPowGwTopic_, 0),
      std::make_tuple(def()->rskRawGwTopic_, 0),
      std::make_tuple(def()->vcashRawGwTopic_, 0)};

  auto kafkaConsumer =
      std::make_shared<KafkaQueueConsumer>(kafkaBrokers, topics);
  std::map<string, string> options{{"fetch.wait.max.ms", "5"}};
  kafkaConsumer->setup(RD_KAFKA_OFFSET_TAIL(1), &options);
  JobMakerConsumerHandler handler{
      kafkaConsumer, [this](const std::string &msg, const std::string &topic) {
        if (topic == def()->rawGbtTopic_) {
          return processRawGbtMsg(msg);
        } else if (topic == def()->auxPowGwTopic_) {
          return processAuxPowMsg(msg);
        } else if (topic == def()->rskRawGwTopic_) {
          return processRskGwMsg(msg);
        } else if (topic == def()->vcashRawGwTopic_) {
          return processVcashGwMsg(msg);
        }
        return false;
      }};
  handlers.push_back(handler);

  // sleep 3 seconds, wait for the latest N messages transfer from broker to
  // client
  std::this_thread::sleep_for(3s);

  /* pre-consume some messages for initialization */
  bool consume = true;
  while (consume) {
    rd_kafka_message_t *rkmessage;
    rkmessage = kafkaConsumer->consumer(5000 /* timeout ms */);
    if (rkmessage == nullptr || rkmessage->err) {
      break;
    }
    string topic = rd_kafka_topic_name(rkmessage->rkt);
    string msg((const char *)rkmessage->payload, rkmessage->len);
    if (topic == def()->rawGbtTopic_) {
      processRawGbtMsg(msg);
      consume = false;
    } else if (topic == def()->auxPowGwTopic_) {
      processAuxPowMsg(msg);
    } else if (topic == def()->rskRawGwTopic_) {
      processRskGwMsg(msg);
    } else if (topic == def()->vcashRawGwTopic_) {
      processVcashGwMsg(msg);
    }

    rd_kafka_message_destroy(rkmessage);
  }
  LOG(INFO) << "consume latest rawgbt messages done";

  checkSubPoolAddr();

  return true;
}

bool JobMakerHandlerBitcoin::addRawGbt(const string &msg) {
  JsonNode r;
  if (!JsonNode::parse(msg.c_str(), msg.c_str() + msg.size(), r)) {
    LOG(ERROR) << "parse rawgbt message to json fail";
    return false;
  }

  if (r["created_at_ts"].type() != Utilities::JS::type::Int ||
      r["block_template_base64"].type() != Utilities::JS::type::Str ||
      r["gbthash"].type() != Utilities::JS::type::Str) {
    LOG(ERROR) << "invalid rawgbt: missing fields";
    return false;
  }

  const uint256 gbtHash = uint256S(r["gbthash"].str());
  for (const auto &itr : lastestGbtHash_) {
    if (gbtHash == itr) {
      LOG(ERROR) << "duplicate gbt hash: " << gbtHash.ToString();
      return false;
    }
  }

  const uint32_t gbtTime = r["created_at_ts"].uint32();
  const int64_t timeDiff = (int64_t)time(nullptr) - (int64_t)gbtTime;
  if (labs(timeDiff) >= 60) {
    LOG(WARNING) << "rawgbt diff time is more than 60, ignore it";
    return false; // time diff too large, there must be some problems, so ignore
                  // it
  }
  if (labs(timeDiff) >= 3) {
    LOG(WARNING) << "rawgbt diff time is too large: " << timeDiff << " seconds";
  }

  const string gbt = DecodeBase64(r["block_template_base64"].str());
  assert(gbt.length() > 64); // valid gbt string's len at least 64 bytes

  JsonNode nodeGbt;
  if (!JsonNode::parse(gbt.c_str(), gbt.c_str() + gbt.length(), nodeGbt)) {
    LOG(ERROR) << "parse gbt message to json fail";
    return false;
  }
  assert(nodeGbt["result"]["height"].type() == Utilities::JS::type::Int);
  const uint32_t height = nodeGbt["result"]["height"].uint32();

#if defined(CHAIN_TYPE_BCH) || defined(CHAIN_TYPE_BSV)
  bool isLightVersion =
      nodeGbt["result"][LIGHTGBT_JOB_ID].type() == Utilities::JS::type::Str;
  bool isEmptyBlock = false;
  if (isLightVersion) {
    assert(
        nodeGbt["result"][LIGHTGBT_MERKLE].type() ==
        Utilities::JS::type::Array);
    isEmptyBlock = nodeGbt["result"][LIGHTGBT_MERKLE].array().size() == 0;
  } else {
    assert(
        nodeGbt["result"]["transactions"].type() == Utilities::JS::type::Array);
    isEmptyBlock = nodeGbt["result"]["transactions"].array().size() == 0;
  }
#else
  assert(
      nodeGbt["result"]["transactions"].type() == Utilities::JS::type::Array);
  const bool isEmptyBlock =
      nodeGbt["result"]["transactions"].array().size() == 0;
#endif

  if (rawgbtMap_.size() > 0) {
    const uint64_t bestKey = rawgbtMap_.rbegin()->first;
    const uint32_t bestTime = gbtKeyGetTime(bestKey);
    const uint32_t bestHeight = gbtKeyGetHeight(bestKey);

    // To prevent the job's block height ups and downs
    // when the block height of two bitcoind is not synchronized.
    // The block height downs must past twice the time of stratumJobInterval_
    // without the higher height GBT received.
    if (height < bestHeight && gbtTime - bestTime < 2 * def()->jobInterval_) {
      LOG(WARNING) << "skip low height GBT. height: " << height
                   << ", best height: " << bestHeight
                   << ", elapsed time after best GBT: " << (gbtTime - bestTime)
                   << "s";
      return false;
    }
  }

  const uint64_t key = makeGbtKey(gbtTime, isEmptyBlock, height);
  if (rawgbtMap_.find(key) == rawgbtMap_.end()) {
    rawgbtMap_.insert(std::make_pair(key, gbt));
  } else {
    LOG(ERROR) << "key already exist in rawgbtMap: " << key;
  }

  lastestGbtHash_.push_back(gbtHash);
  while (lastestGbtHash_.size() > 20) {
    lastestGbtHash_.pop_front();
  }

  LOG(INFO) << "add rawgbt, height: " << height
            << ", gbthash: " << r["gbthash"].str().substr(0, 16)
            << "..., gbtTime(UTC): " << date("%F %T", gbtTime)
            << ", isEmpty:" << isEmptyBlock;

  return true;
}

bool JobMakerHandlerBitcoin::findBestRawGbt(string &bestRawGbt) {
  static uint64_t lastSendBestKey = 0;

  // clean expired gbt first
  clearTimeoutGbt();
  clearTimeoutGw();
  clearVcashTimeoutGw();

  if (rawgbtMap_.size() == 0) {
    LOG(WARNING) << "RawGbt Map is empty";
    return false;
  }

  bool isFindNewHeight = false;
  bool needUpdateEmptyBlockJob = false;

  // rawgbtMap_ is sorted gbt by (timestamp + height + emptyFlag),
  // so the last item is the newest/best item.
  // @see makeGbtKey()
  const uint64_t bestKey = rawgbtMap_.rbegin()->first;

  const uint32_t bestHeight = gbtKeyGetHeight(bestKey);
  const bool currentGbtIsEmpty = gbtKeyIsEmptyBlock(bestKey);

  if (bestKey == lastSendBestKey) {
    LOG(WARNING) << "bestKey is the same as last one: " << lastSendBestKey;
  }

  // if last job is an empty block job, we need to
  // send a new non-empty job as quick as possible.
  if (bestHeight == currBestHeight_ && isLastJobEmptyBlock_ &&
      !currentGbtIsEmpty) {
    needUpdateEmptyBlockJob = true;
    LOG(INFO) << "--------update last empty block job--------";
  }

  // The height cannot reduce in normal.
  // However, if there is indeed a height reduce,
  // isReachTimeout() will allow the new job sending.
  if (bestHeight > currBestHeight_) {
    LOG(INFO) << ">>>> found new best height: " << bestHeight
              << ", curr: " << currBestHeight_ << " <<<<";
    isFindNewHeight = true;
  }

  if (isFindNewHeight || needUpdateEmptyBlockJob || isMergedMiningUpdate_ ||
      isReachTimeout()) {
    lastSendBestKey = bestKey;
    currBestHeight_ = bestHeight;
    LOG(INFO) << "====> send new job to kafka reason : \n"
              << (isFindNewHeight ? " height update." : "")
              << (needUpdateEmptyBlockJob ? " update empty block." : "")
              << (isMergedMiningUpdate_ ? " merge mining update." : "")
              << (isReachTimeout() ? " reach timeout." : "")
              << " ,bestKeyHeight : " << gbtKeyGetHeight(bestKey)
              << " ,bestKeyTimestamp : " << gbtKeyGetTime(bestKey)
              << " ,bestKeyIsEmpty : " << gbtKeyIsEmptyBlock(bestKey);

    bestRawGbt = rawgbtMap_.rbegin()->second.c_str();
    return true;
  }

  return false;
}

bool JobMakerHandlerBitcoin::isReachTimeout() {
  uint32_t intervalSeconds = def()->jobInterval_;

  if (lastJobSendTime_ + intervalSeconds <= time(nullptr)) {
    return true;
  }
  return false;
}

void JobMakerHandlerBitcoin::clearTimeoutGbt() {
  // Maps (and sets) are sorted, so the first element is the smallest,
  // and the last element is the largest.

  const uint32_t ts_now = time(nullptr);

  // Ensure that rawgbtMap_ has at least one element, even if it expires.
  // So jobmaker can always generate jobs even if blockchain node does not
  // update the response of getblocktemplate for a long time when there is no
  // new transaction.
  for (auto itr = rawgbtMap_.begin();
       rawgbtMap_.size() > 1 && itr != rawgbtMap_.end();) {
    const uint32_t ts = gbtKeyGetTime(itr->first);
    const bool isEmpty = gbtKeyIsEmptyBlock(itr->first);
    const uint32_t height = gbtKeyGetHeight(itr->first);

    // gbt expired time
    const uint32_t expiredTime =
        ts + (isEmpty ? def()->emptyGbtLifeTime_ : def()->gbtLifeTime_);

    if (expiredTime > ts_now) {
      // not expired
      ++itr;
    } else {
      // remove expired gbt
      LOG(INFO) << "remove timeout rawgbt: " << date("%F %T", ts) << "|" << ts
                << ", height:" << height
                << ", isEmptyBlock:" << (isEmpty ? 1 : 0);

      // c++11: returns an iterator to the next element in the map
      itr = rawgbtMap_.erase(itr);
    }
  }
}

void JobMakerHandlerBitcoin::clearTimeoutGw() {
  RskWork currentRskWork;
  RskWork previousRskWork;

  if (previousRskWork_ == nullptr || currentRskWork_ == nullptr) {
    return;
  }

  const uint32_t ts_now = time(nullptr);
  currentRskWork = *currentRskWork_;
  if (currentRskWork.getCreatedAt() + 120u < ts_now) {
    delete currentRskWork_;
    currentRskWork_ = nullptr;
  }

  previousRskWork = *previousRskWork_;
  if (previousRskWork.getCreatedAt() + 120u < ts_now) {
    delete previousRskWork_;
    previousRskWork_ = nullptr;
  }
}

void JobMakerHandlerBitcoin::clearVcashTimeoutGw() {
  VcashWork currentVcashWork;
  VcashWork previousVcashWork;

  if (previousVcashWork_ == nullptr || currentVcashWork_ == nullptr) {
    return;
  }

  const uint32_t ts_now = time(nullptr);
  currentVcashWork = *currentVcashWork_;
  if (currentVcashWork.getCreatedAt() + 600u < ts_now) {
    delete currentVcashWork_;
    currentVcashWork_ = nullptr;
  }

  previousVcashWork = *previousVcashWork_;
  if (previousVcashWork.getCreatedAt() + 600u < ts_now) {
    delete previousVcashWork_;
    previousVcashWork_ = nullptr;
  }
}

bool JobMakerHandlerBitcoin::triggerRskUpdate() {
  if (previousRskWork_ == nullptr || currentRskWork_ == nullptr) {
    return false;
  }

  RskWork currentRskWork = *currentRskWork_;
  RskWork previousRskWork = *previousRskWork_;

  bool notifyFlagUpdate = def()->rskmergedMiningNotifyPolicy_ == 1 &&
      currentRskWork.getNotifyFlag();
  bool differentHashUpdate = def()->rskmergedMiningNotifyPolicy_ == 2 &&
      (currentRskWork.getBlockHash() != previousRskWork.getBlockHash());

  bool isMergedMiningUpdate = notifyFlagUpdate || differentHashUpdate;
  DLOG_IF(INFO, isMergedMiningUpdate)
      << "it's time to update MergedMining  because rsk : "
      << (notifyFlagUpdate ? " notifyFlagUpdate " : " ")
      << (differentHashUpdate ? " differentHashUpdate " : " ");

  return isMergedMiningUpdate;
}

bool JobMakerHandlerBitcoin::triggerVcashUpdate() {
  if (previousVcashWork_ == nullptr || currentVcashWork_ == nullptr) {
    return false;
  }

  VcashWork currentVcashWork = *currentVcashWork_;
  VcashWork previousVcashWork = *previousVcashWork_;

  bool heightUpdate = def()->vcashmergedMiningNotifyPolicy_ == 1 &&
      currentVcashWork.getHeight() > previousVcashWork.getHeight();

  bool differentHashUpdate = def()->vcashmergedMiningNotifyPolicy_ == 2 &&
      (currentVcashWork.getBlockHash() != previousVcashWork.getBlockHash());

  bool isMergedMiningUpdate = heightUpdate || differentHashUpdate;
  DLOG_IF(INFO, isMergedMiningUpdate)
      << "it's time to update MergedMining  because vcash : "
      << (heightUpdate ? " heightUpdate " : " ")
      << (differentHashUpdate ? " differentHashUpdate " : " ");

  return isMergedMiningUpdate;
}

bool JobMakerHandlerBitcoin::processRawGbtMsg(const string &msg) {
  DLOG(INFO) << "JobMakerHandlerBitcoin::processRawGbtMsg: " << msg;
  return addRawGbt(msg);
}

bool JobMakerHandlerBitcoin::processAuxPowMsg(const string &msg) {
  uint32_t currentNmcBlockHeight = 0;
  string currentNmcBlockHash;
  // get block height
  {
    JsonNode r;
    if (!JsonNode::parse(msg.data(), msg.data() + msg.size(), r)) {
      LOG(ERROR) << "parse NmcAuxBlock message to json fail";
      return false;
    }

    if (r["height"].type() != Utilities::JS::type::Int ||
        r["hash"].type() != Utilities::JS::type::Str) {
      LOG(ERROR) << "nmc auxblock fields failure";
      return false;
    }

    currentNmcBlockHeight = r["height"].uint32();
    currentNmcBlockHash = r["hash"].str();
  }

  // set json string

  // backup old height / hash
  uint32_t latestNmcAuxBlockHeight = latestNmcAuxBlockHeight_;
  string latestNmcAuxBlockHash = latestNmcAuxBlockHash_;
  // update height / hash
  latestNmcAuxBlockHeight_ = currentNmcBlockHeight;
  latestNmcAuxBlockHash_ = currentNmcBlockHash;
  // update json
  latestNmcAuxBlockJson_ = msg;
  DLOG(INFO) << "latestAuxPowJson: " << latestNmcAuxBlockJson_;

  bool higherHeightUpdate = def()->auxmergedMiningNotifyPolicy_ == 1 &&
      currentNmcBlockHeight > latestNmcAuxBlockHeight;

  bool differentHashUpdate = def()->auxmergedMiningNotifyPolicy_ == 2 &&
      currentNmcBlockHash != latestNmcAuxBlockHash;

  bool isMergedMiningUpdate = higherHeightUpdate || differentHashUpdate;
  if (isMergedMiningUpdate) {
    isMergedMiningUpdate_ = true;
  }

  DLOG_IF(INFO, isMergedMiningUpdate)
      << "it's time to update MergedMining  because aux : "
      << (higherHeightUpdate ? " higherHeightUpdate " : " ")
      << (differentHashUpdate ? " differentHashUpdate " : " ");

  return isMergedMiningUpdate;
}

bool JobMakerHandlerBitcoin::processRskGwMsg(const string &rawGetWork) {
  // set json string
  RskWork *rskWork = new RskWork();
  if (rskWork->initFromGw(rawGetWork)) {

    if (previousRskWork_ != nullptr) {
      delete previousRskWork_;
      previousRskWork_ = nullptr;
    }

    previousRskWork_ = currentRskWork_;
    currentRskWork_ = rskWork;

    DLOG(INFO) << "currentRskBlockJson: " << rawGetWork;
  } else {
    delete rskWork;
  }

  bool isMergedMiningUpdate = triggerRskUpdate();
  if (isMergedMiningUpdate) {
    isMergedMiningUpdate_ = true;
  }
  return isMergedMiningUpdate;
}

bool JobMakerHandlerBitcoin::processVcashGwMsg(const string &rawGetWork) {
  // set json string
  VcashWork *vcashWork = new VcashWork();
  if (vcashWork->initFromGw(rawGetWork)) {

    if (previousVcashWork_ != nullptr) {
      delete previousVcashWork_;
      previousVcashWork_ = nullptr;
    }

    previousVcashWork_ = currentVcashWork_;
    currentVcashWork_ = vcashWork;

    DLOG(INFO) << "currentVcashBlockJson: " << rawGetWork;
  } else {
    delete vcashWork;
  }

  bool isMergedMiningUpdate = triggerVcashUpdate();
  if (isMergedMiningUpdate) {
    isMergedMiningUpdate_ = true;
  }
  return isMergedMiningUpdate;
}

string JobMakerHandlerBitcoin::makeStratumJob(const string &gbt) {
  DLOG(INFO) << "JobMakerHandlerBitcoin::makeStratumJob gbt: " << gbt;
  string latestNmcAuxBlockJson = latestNmcAuxBlockJson_;

  RskWork currentRskBlockJson;
  if (currentRskWork_ != nullptr) {
    currentRskBlockJson = *currentRskWork_;
  }

  VcashWork currentVcashBlockJson;
  if (currentVcashWork_ != nullptr) {
    currentVcashBlockJson = *currentVcashWork_;
  }

  StratumJobBitcoin sjob;
  {
    ScopeLock sl(subPoolLock_);
    if (!sjob.initFromGbt(
            gbt.c_str(),
            def()->coinbaseInfo_,
            poolPayoutAddr_,
            def()->subPool_,
            def()->blockVersion_,
            latestNmcAuxBlockJson,
            currentRskBlockJson,
            currentVcashBlockJson,
            isMergedMiningUpdate_,
            def()->grandPoolEnabled_)) {
      LOG(ERROR) << "init stratum job message from gbt str fail";
      return "";
    }
  }
  sjob.jobId_ = gen_->next();
  const string jobMsg = sjob.serializeToJson();

  // set last send time
  // TODO: fix Y2K38 issue
  lastJobSendTime_ = (uint32_t)time(nullptr);

  // is an empty block job
  isLastJobEmptyBlock_ = sjob.isEmptyBlock();

  LOG(INFO) << "--------producer stratum job, jobId: " << sjob.jobId_
            << ", height: " << sjob.height_ << "--------";

  isMergedMiningUpdate_ = false;
  return jobMsg;
}

string JobMakerHandlerBitcoin::makeStratumJobMsg() {
  string bestRawGbt;
  if (!findBestRawGbt(bestRawGbt)) {
    return "";
  }
  return makeStratumJob(bestRawGbt);
}

uint64_t JobMakerHandlerBitcoin::makeGbtKey(
    uint32_t gbtTime, bool isEmptyBlock, uint32_t height) {
  assert(height < 0x7FFFFFFFU);

  // gbtKey: 31 bit height + 1 bit non-empty flag + 32 bit timestamp
  return (((uint64_t)gbtTime)) | (((uint64_t)height) << 33) |
      (((uint64_t)(!isEmptyBlock)) << 32);
}

uint32_t JobMakerHandlerBitcoin::gbtKeyGetTime(uint64_t gbtKey) {
  return (uint32_t)gbtKey;
}

uint32_t JobMakerHandlerBitcoin::gbtKeyGetHeight(uint64_t gbtKey) {
  return (uint32_t)((gbtKey >> 33) & 0x7FFFFFFFULL);
}

bool JobMakerHandlerBitcoin::gbtKeyIsEmptyBlock(uint64_t gbtKey) {
  return !((bool)((gbtKey >> 32) & 1ULL));
}

bool JobMakerHandlerBitcoin::updateSubPoolAddr(size_t index) {
  try {
    SubPoolInfo oldPool;
    {
      ScopeLock sl(subPoolLock_);
      oldPool = def()->subPool_[index];
    }

    string str;
    if (jobmaker_->zk()->getValueW(
            oldPool.zkUpdatePath_,
            str,
            SUBPOOL_JSON_MAX_SIZE,
            handleSubPoolUpdateEvent,
            this)) {
      JSON response = {
          {"created_at", date("%F %T")},
          {"host",
           {
               {"hostname", IpAddress::getHostName()},
               {"ip", IpAddress::getInterfaceAddrs()},
           }},
          {"subpool_name", oldPool.name_},
          {"old",
           {
               {"coinbase_info", oldPool.coinbaseInfo_},
               {"payout_addr",
                BitcoinUtils::EncodeDestination(oldPool.payoutAddr_)},
           }},
      };

      string errMsg = "success";
      auto sendResponse = [this, &response, &oldPool, &errMsg](bool success) {
        response["success"] = success;
        response["err_msg"] = errMsg;

        jobmaker_->zk()->setNode(
            oldPool.zkUpdatePath_ + "/ack", response.dump());
      };

      if (str.empty()) {
        errMsg = "empty request";
        sendResponse(false);
        return false;
      }

      auto json = JSON::parse(str);

      if (!json.is_object() || !json["coinbase_info"].is_string() ||
          !json["payout_addr"].is_string()) {
        errMsg = "field missing or wrong field type";
        LOG(ERROR) << "JobMakerHandlerBitcoin::updateSubPoolAddr(): "
                   << "[subpool " << oldPool.name_ << "] " << errMsg
                   << ", raw json: " << str;

        sendResponse(false);
        return false;
      }

      string payoutAddrStr = json["payout_addr"].get<string>();
      string coinbaseInfo = json["coinbase_info"].get<string>();

      if (!BitcoinUtils::IsValidDestinationString(payoutAddrStr)) {
        errMsg = "invalid payout address";
        LOG(ERROR) << "[subpool " << oldPool.name_ << "] update failed, "
                   << errMsg << ": " << payoutAddrStr << ", raw json: " << str;

        sendResponse(false);
        return false;
      }

      if (coinbaseInfo.size() > (size_t)def()->subPoolCoinbaseMaxLen_) {
        coinbaseInfo.resize(def()->subPoolCoinbaseMaxLen_);
        errMsg = "coinbase info truncated to " +
            std::to_string(def()->subPoolCoinbaseMaxLen_) + " bytes";
      }

      LOG(INFO) << "[subpool " << oldPool.name_
                << "] coinbase updated, coinbaseInfo: " << oldPool.coinbaseInfo_
                << " -> " << coinbaseInfo << ", payoutAddr: "
                << BitcoinUtils::EncodeDestination(oldPool.payoutAddr_)
                << " -> " << payoutAddrStr;

      auto payoutAddr = BitcoinUtils::DecodeDestination(payoutAddrStr);
      {
        ScopeLock sl(subPoolLock_);
        auto &pool = defWithoutConst()->subPool_[index];
        pool.coinbaseInfo_ = coinbaseInfo;
        pool.payoutAddr_ = payoutAddr;
      }

      response["new"] = {
          {"coinbase_info", coinbaseInfo},
          {"payout_addr", BitcoinUtils::EncodeDestination(payoutAddr)},
      };
      sendResponse(true);
      return true;
    }

    LOG(ERROR) << "JobMakerHandlerBitcoin::updateSubPoolAddr(): "
                  "zk->getValueW() failed, path: "
               << oldPool.zkUpdatePath_;

  } catch (const std::exception &ex) {
    LOG(ERROR) << "JobMakerHandlerBitcoin::updateSubPoolAddr(): " << ex.what();
  } catch (...) {
    LOG(ERROR)
        << "JobMakerHandlerBitcoin::updateSubPoolAddr(): unknown exception";
  }

  return false;
}

void JobMakerHandlerBitcoin::checkSubPoolAddr() {
  if (def()->subPool_.empty()) {
    return;
  }
  {
    size_t count = 0;
    for (auto itr : def()->subPool_) {
      if (!itr.zkUpdatePath_.empty()) {
        count++;
      }
    }
    if (count < 1) {
      return;
    }
  }

  jobmaker_->initZk();

  updateSubPoolAddrThread_ = std::thread([this]() {
    auto checkSubPool = [this](const SubPoolInfo &itr, size_t i) -> bool {
      try {
        // create node if not exists
        jobmaker_->zk()->createNodesRecursively(itr.zkUpdatePath_ + "/ack");

        auto str =
            jobmaker_->zk()->getValue(itr.zkUpdatePath_, SUBPOOL_JSON_MAX_SIZE);

        if (str.empty()) {
          jobmaker_->zk()->watchNode(
              itr.zkUpdatePath_, handleSubPoolUpdateEvent, this);
          return false;
        }

        auto json = JSON::parse(str);
        bool updated = false;

        if (!json.is_object() || !json["coinbase_info"].is_string() ||
            !json["payout_addr"].is_string()) {
          LOG(ERROR) << "JobMakerHandlerBitcoin::checkSubPoolAddr(): "
                     << "[subpool " << itr.name_
                     << "] wrong field type, raw json: " << str;
          return false;
        }

        string payoutAddrStr = json["payout_addr"].get<string>();
        string coinbaseInfo = json["coinbase_info"].get<string>();

        if (coinbaseInfo.size() > (size_t)def()->subPoolCoinbaseMaxLen_) {
          coinbaseInfo.resize(def()->subPoolCoinbaseMaxLen_);
        }

        if (!BitcoinUtils::IsValidDestinationString(payoutAddrStr)) {
          LOG(ERROR) << "[subpool " << itr.name_
                     << "] update failed, invalid payout address: "
                     << payoutAddrStr << ", raw json: " << str;
          return false;
        }

        auto payoutAddr = BitcoinUtils::DecodeDestination(payoutAddrStr);

        if (payoutAddr != itr.payoutAddr_) {
          updated = true;
          LOG(INFO) << "[subpool " << itr.name_
                    << "] payout address changed, old: "
                    << BitcoinUtils::EncodeDestination(itr.payoutAddr_)
                    << ", new: " << payoutAddrStr;
        }

        if (coinbaseInfo != itr.coinbaseInfo_) {
          updated = true;
          LOG(INFO) << "[subpool " << itr.name_
                    << "] coinbase info changed, old: " << itr.coinbaseInfo_
                    << ", new: " << coinbaseInfo;
        }

        return updated;
      } catch (const std::exception &ex) {
        LOG(ERROR) << "JobMakerHandlerBitcoin::checkSubPoolAddr(): "
                   << ex.what();
      } catch (...) {
        LOG(ERROR) << "JobMakerHandlerBitcoin::checkSubPoolAddr(): unknown "
                      "exception";
      }
      return false;
    };

    auto sleepTime = 300s;

    // init
    {
      vector<SubPoolInfo> subpool;
      {
        ScopeLock sl(subPoolLock_);
        subpool = def()->subPool_;
      }

      for (size_t i = 0; i < subpool.size(); i++) {
        auto itr = subpool[i];
        if (!itr.zkUpdatePath_.empty()) {
          if (!updateSubPoolAddr(i)) {
            watchSubPoolAddr(itr.zkUpdatePath_);
            sleepTime = 30s;
          }
        }
      }
    }

    while (jobmaker_->running()) {
      std::this_thread::sleep_for(sleepTime);
      sleepTime = 300s;

      vector<SubPoolInfo> subpool;
      {
        ScopeLock sl(subPoolLock_);
        subpool = def()->subPool_;
      }

      for (size_t i = 0; i < subpool.size(); i++) {
        auto itr = subpool[i];
        if (checkSubPool(itr, i)) {
          if (!updateSubPoolAddr(i)) {
            watchSubPoolAddr(itr.zkUpdatePath_);
            sleepTime = 30s;
          }
        }
      }
    }
  });
}

void JobMakerHandlerBitcoin::watchSubPoolAddr(const string &path) {
  try {
    jobmaker_->zk()->watchNode(path, handleSubPoolUpdateEvent, this);
  } catch (const std::exception &ex) {
    LOG(ERROR) << "JobMakerHandlerBitcoin::watchSubPoolAddr(): "
                  "zk->getValueW() failed: "
               << ex.what();
  } catch (...) {
    LOG(ERROR)
        << "JobMakerHandlerBitcoin::watchSubPoolAddr(): unknown exception";
  }
}

void JobMakerHandlerBitcoin::handleSubPoolUpdateEvent(
    zhandle_t *zh, int type, int state, const char *path, void *pThis) {
  if (path == nullptr || pThis == nullptr) {
    return;
  }
  DLOG(INFO) << "JobMakerHandlerBitcoin::handleSubPoolUpdateEvent, type: "
             << type << ", state: " << state << ", path: " << path;

  auto handle = static_cast<JobMakerHandlerBitcoin *>(pThis);
  vector<SubPoolInfo> subpool;
  {
    ScopeLock sl(handle->subPoolLock_);
    subpool = handle->def()->subPool_;
  }
  for (size_t i = 0; i < subpool.size(); i++) {
    if (subpool[i].zkUpdatePath_ == path) {
      if (!handle->updateSubPoolAddr(i)) {
        handle->watchSubPoolAddr(path);
      }
      break;
    }
  }
}
