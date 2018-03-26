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
#include "JobMaker.h"

#include <iostream>
#include <stdlib.h>

#include <glog/logging.h>
#include <librdkafka/rdkafka.h>

#include <hash.h>
#include <script/script.h>
#include <uint256.h>
#include <util.h>
#include <utilstrencodings.h>

#include "utilities_js.hpp"
#include "Utils.h"
#include "BitcoinUtils.h"

///////////////////////////////////  JobMaker  /////////////////////////////////
// JobMaker::JobMaker(const string &kafkaBrokers,  uint32_t stratumJobInterval,
//                    const string &payoutAddr, uint32_t gbtLifeTime,
//                    uint32_t emptyGbtLifeTime, const string &fileLastJobTime,
//                    uint32_t rskNotifyPolicy,
//                    uint32_t blockVersion, const string &poolCoinbaseInfo):
// running_(true),
// kafkaBrokers_(kafkaBrokers),
// kafkaProducer_(kafkaBrokers_.c_str(), KAFKA_TOPIC_STRATUM_JOB, RD_KAFKA_PARTITION_UA/* partition */),
// kafkaRawGbtConsumer_(kafkaBrokers_.c_str(), KAFKA_TOPIC_RAWGBT,       0/* partition */),
// kafkaNmcAuxConsumer_(kafkaBrokers_.c_str(), KAFKA_TOPIC_NMC_AUXBLOCK, 0/* partition */),
// kafkaRawGwConsumer_(kafkaBrokers_.c_str(), KAFKA_TOPIC_RAWGW, 0/* partition */),
// previousRskWork_(nullptr), currentRskWork_(nullptr), rskNotifyPolicy_(rskNotifyPolicy),
// currBestHeight_(0), lastJobSendTime_(0),
// isLastJobEmptyBlock_(false),
// stratumJobInterval_(stratumJobInterval),
// poolCoinbaseInfo_(poolCoinbaseInfo), poolPayoutAddrStr_(payoutAddr),
// kGbtLifeTime_(gbtLifeTime), kEmptyGbtLifeTime_(emptyGbtLifeTime),
// fileLastJobTime_(fileLastJobTime),
// blockVersion_(blockVersion)
// {
// 	LOG(INFO) << "Block Version: " << std::hex << blockVersion_;
// 	LOG(INFO) << "Coinbase Info: " << poolCoinbaseInfo_;
//   LOG(INFO) << "Payout Address: " << poolPayoutAddrStr_;

//   RskWork::setIsCleanJob(rskNotifyPolicy != 0);
// }

JobMaker::JobMaker(const JobMakerDefinition def,
                   const string &brokers) : def_(def),
                                            running_(true),
                                            kafkaProducer_(brokers.c_str(), def.producerTopic.c_str(), RD_KAFKA_PARTITION_UA),
                                            kafkaRawGwConsumer_(brokers.c_str(), def.consumerTopic.c_str(), 0)
{
}

JobMaker::~JobMaker() {
  // if (threadConsumeNmcAuxBlock_.joinable())
  //   threadConsumeNmcAuxBlock_.join();
}

void JobMaker::stop() {
  if (!running_) {
    return;
  }
  running_ = false;
  LOG(INFO) << "stop jobmaker";
}

bool JobMaker::init() {
    if (nullptr == def_.handler)
      return false;
    
    def_.handler->init(def_);
  // check pool payout address
  // if (!IsValidDestinationString(poolPayoutAddrStr_)) {
  //   LOG(ERROR) << "invalid pool payout address";
  //   return false;
  // }

  // poolPayoutAddr_ = DecodeDestination(poolPayoutAddrStr_);

  /* setup kafka */
  {
    map<string, string> options;
    // set to 1 (0 is an illegal value here), deliver msg as soon as possible.
    options["queue.buffering.max.ms"] = "1";
    if (!kafkaProducer_.setup(&options)) {
      LOG(ERROR) << "kafka producer setup failure";
      return false;
    }
    if (!kafkaProducer_.checkAlive()) {
      LOG(ERROR) << "kafka producer is NOT alive";
      return false;
    }
  }

  if (!initConsumer())
    return false;

  return true;
}

bool JobMaker::initConsumer() {
  //  const int32_t consumeLatestN = 20;

    //
  // consumer RawGbt, offset: latest N messages
  //
  // {
  //   map<string, string> consumerOptions;
  //   consumerOptions["fetch.wait.max.ms"] = "5";
  //   if (!kafkaRawGbtConsumer_.setup(RD_KAFKA_OFFSET_TAIL(consumeLatestN), &consumerOptions)) {
  //     LOG(ERROR) << "kafka consumer rawgbt setup failure";
  //     return false;
  //   }
  //   if (!kafkaRawGbtConsumer_.checkAlive()) {
  //     LOG(ERROR) << "kafka consumer rawgbt is NOT alive";
  //     return false;
  //   }
  // }
  // // sleep 3 seconds, wait for the latest N messages transfer from broker to client
  // sleep(3);

  //
  // consumer, namecoin aux block
  //
  // {
  //   map<string, string> consumerOptions;
  //   consumerOptions["fetch.wait.max.ms"] = "5";
  //   if (!kafkaNmcAuxConsumer_.setup(RD_KAFKA_OFFSET_TAIL(1), &consumerOptions)) {
  //     LOG(ERROR) << "kafka consumer nmc aux block setup failure";
  //     return false;
  //   }
  //   if (!kafkaNmcAuxConsumer_.checkAlive()) {
  //     LOG(ERROR) << "kafka consumer nmc aux block is NOT alive";
  //     return false;
  //   }
  // }
  // sleep(1);

  // //
  // // consumer latest NmcAuxBlock
  // //
  // {
  //   rd_kafka_message_t *rkmessage;
  //   rkmessage = kafkaNmcAuxConsumer_.consumer(1000/* timeout ms */);
  //   if (rkmessage != nullptr) {
  //     consumeNmcAuxBlockMsg(rkmessage);
  //     rd_kafka_message_destroy(rkmessage);
  //   }
  // }

  //
  // consumer RSK messages
  //
  {
    map<string, string> consumerOptions;
    consumerOptions["fetch.wait.max.ms"] = "5";
    if (!kafkaRawGwConsumer_.setup(RD_KAFKA_OFFSET_TAIL(1), &consumerOptions)) {
      LOG(ERROR) << "kafka consumer rawgw block setup failure";
      return false;
    }
    if (!kafkaRawGwConsumer_.checkAlive()) {
      LOG(ERROR) << "kafka consumer rawgw block is NOT alive";
      return false;
    }
  }
  sleep(1);

  //
  // consumer latest RSK get work
  //
  // {
  //   rd_kafka_message_t *rkmessage;
  //   rkmessage = kafkaRawGwConsumer_.consumer(1000/* timeout ms */);
  //   if (rkmessage != nullptr) {
  //     consumeRawGwMsg(rkmessage);
  //     rd_kafka_message_destroy(rkmessage);
  //   }
  // }
  //
  // consumer latest RawGbt N messages
  //
  // read latest gbtmsg from kafka
  // LOG(INFO) << "consume latest rawgbt message from kafka...";
  // for (int32_t i = 0; i < consumeLatestN; i++) {
  //   rd_kafka_message_t *rkmessage;
  //   rkmessage = kafkaRawGbtConsumer_.consumer(5000/* timeout ms */);
  //   if (rkmessage == nullptr) {
  //     break;
  //   }
  //   consumeRawGbtMsg(rkmessage, false);
  //   rd_kafka_message_destroy(rkmessage);
  // }
  // LOG(INFO) << "consume latest rawgbt messages done";
  //checkAndSendStratumJob(false);
  return true;
}

void JobMaker::consumeNmcAuxBlockMsg(rd_kafka_message_t *rkmessage) {
  // check error
  if (rkmessage->err) {
    if (rkmessage->err == RD_KAFKA_RESP_ERR__PARTITION_EOF) {
      // Reached the end of the topic+partition queue on the broker.
      // Not really an error.
      //      LOG(INFO) << "consumer reached end of " << rd_kafka_topic_name(rkmessage->rkt)
      //      << "[" << rkmessage->partition << "] "
      //      << " message queue at offset " << rkmessage->offset;
      // acturlly
      return;
    }

    LOG(ERROR) << "consume error for topic " << rd_kafka_topic_name(rkmessage->rkt)
    << "[" << rkmessage->partition << "] offset " << rkmessage->offset
    << ": " << rd_kafka_message_errstr(rkmessage);

    if (rkmessage->err == RD_KAFKA_RESP_ERR__UNKNOWN_PARTITION ||
        rkmessage->err == RD_KAFKA_RESP_ERR__UNKNOWN_TOPIC) {
      LOG(FATAL) << "consume fatal";
      stop();
    }
    return;
  }

  // set json string
  LOG(INFO) << "received nmcauxblock message, len: " << rkmessage->len;
  {
    ScopeLock sl(auxJsonlock_);
    latestNmcAuxBlockJson_ = string((const char *)rkmessage->payload, rkmessage->len);
    DLOG(INFO) << "latestNmcAuxBlockJson: " << latestNmcAuxBlockJson_;
  }
}

void JobMaker::consumeRawGbtMsg(rd_kafka_message_t *rkmessage, bool needToSend) {
  // check error
  if (rkmessage->err) {
    if (rkmessage->err == RD_KAFKA_RESP_ERR__PARTITION_EOF) {
      // Reached the end of the topic+partition queue on the broker.
      // Not really an error.
//      LOG(INFO) << "consumer reached end of " << rd_kafka_topic_name(rkmessage->rkt)
//      << "[" << rkmessage->partition << "] "
//      << " message queue at offset " << rkmessage->offset;
      // acturlly
      return;
    }

    LOG(ERROR) << "consume error for topic " << rd_kafka_topic_name(rkmessage->rkt)
               << "[" << rkmessage->partition << "] offset " << rkmessage->offset
               << ": " << rd_kafka_message_errstr(rkmessage);

    if (rkmessage->err == RD_KAFKA_RESP_ERR__UNKNOWN_PARTITION ||
        rkmessage->err == RD_KAFKA_RESP_ERR__UNKNOWN_TOPIC) {
      LOG(FATAL) << "consume fatal";
      stop();
    }
    return;
  }

  LOG(INFO) << "received rawgbt message, len: " << rkmessage->len;
  addRawgbt((const char *)rkmessage->payload, rkmessage->len);

  if (needToSend) {
    checkAndSendStratumJob(false);
  }
}

void JobMaker::runThreadConsumeNmcAuxBlock() {
  // const int32_t timeoutMs = 1000;

  // while (running_) {
  //   rd_kafka_message_t *rkmessage;
  //   rkmessage = kafkaNmcAuxConsumer_.consumer(timeoutMs);
  //   if (rkmessage == nullptr) /* timeout */
  //     continue;

  //   consumeNmcAuxBlockMsg(rkmessage);

  //   /* Return message to rdkafka */
  //   rd_kafka_message_destroy(rkmessage);
  // }
}

/**
  Beginning of methods needed to consume a raw get work message and extract its info.
  Info will then be used to create add RSK merge mining data into stratum jobs.

  @author Martin Medina
  @copyright RSK Labs Ltd.
*/
void JobMaker::consumeRawGwMsg(rd_kafka_message_t *rkmessage)
{
  // check error
  if (rkmessage->err)
  {
    if (rkmessage->err == RD_KAFKA_RESP_ERR__PARTITION_EOF)
    {
      // Reached the end of the topic+partition queue on the broker.
      return;
    }

    LOG(ERROR) << "consume error for topic " << rd_kafka_topic_name(rkmessage->rkt)
               << "[" << rkmessage->partition << "] offset " << rkmessage->offset
               << ": " << rd_kafka_message_errstr(rkmessage);

    if (rkmessage->err == RD_KAFKA_RESP_ERR__UNKNOWN_PARTITION ||
        rkmessage->err == RD_KAFKA_RESP_ERR__UNKNOWN_TOPIC)
    {
      LOG(FATAL) << "consume fatal";
      stop();
    }
    return;
  }

  // set json string
  LOG(INFO) << "received rawgw message len: " << rkmessage->len;
  //{
  //ScopeLock sl(rskWorkAccessLock_);

  string msg((const char *)rkmessage->payload, rkmessage->len);
  if (def_.handler && def_.handler->processMsg(msg)) {
    const string producerMsg = move(def_.handler->buildStratumJobMsg());
    if (producerMsg.size() > 0) {
      LOG(INFO) << "new " << def_.producerTopic << " job: " << producerMsg;
      kafkaProducer_.produce(producerMsg.data(), producerMsg.size());
    }
  }
  // RskWork *rskWork = createWork();
  // if(rskWork->initFromGw(rawGetWork)) {

  //   if (previousRskWork_ != nullptr) {
  //     delete previousRskWork_;
  //     previousRskWork_ = nullptr;
  //   }

  //   previousRskWork_ = currentRskWork_;
  //   currentRskWork_ = rskWork;

  //   DLOG(INFO) << "currentRskBlockJson: " << rawGetWork;
  // } else {
  //   delete rskWork;
  // }
  //}

  // if(triggerRskUpdate()) {
  //   checkAndSendStratumJob(true);
  // }
}

RskWork* JobMaker::createWork() {
  return new RskWork(); 
}

bool JobMaker::triggerRskUpdate() {
  RskWork currentRskWork;
  RskWork previousRskWork;
  {
    ScopeLock sl(rskWorkAccessLock_);
    if (previousRskWork_ == nullptr || currentRskWork_ == nullptr) {
      return false;
    }
    currentRskWork = *currentRskWork_;
    previousRskWork = *previousRskWork_;
  }

  bool notify_flag_update = rskNotifyPolicy_ == 1 && currentRskWork.getNotifyFlag();
  bool different_block_hashUpdate = rskNotifyPolicy_ == 2 && 
                                      (currentRskWork.getBlockHash() != 
                                        previousRskWork.getBlockHash());

  return notify_flag_update || different_block_hashUpdate;
}

void JobMaker::clearTimeoutGw()
{
  RskWork currentRskWork;
  RskWork previousRskWork;
  {
    ScopeLock sl(rskWorkAccessLock_);
    if (previousRskWork_ == nullptr || currentRskWork_ == nullptr)
    {
      return;
    }

    const uint32_t ts_now = time(nullptr);
    currentRskWork = *currentRskWork_;

    if (currentRskWork.getCreatedAt() + 120u < ts_now)
    {
      delete currentRskWork_;
      currentRskWork_ = nullptr;
    }

    previousRskWork = *previousRskWork_;
    if (previousRskWork.getCreatedAt() + 120u < ts_now)
    {
      delete previousRskWork_;
      previousRskWork_ = nullptr;
    }
  }
}

void JobMaker::runThreadConsumeRawGw() {
  const int32_t timeoutMs = 1000;
  //LOG(INFO) << "runThreadConsumeRawGw " << running_;
  while (running_) {
    rd_kafka_message_t *rkmessage;
    rkmessage = kafkaRawGwConsumer_.consumer(timeoutMs);
    if (rkmessage == nullptr) /* timeout */ {
      continue;
    }

    consumeRawGwMsg(rkmessage);

    /* Return message to rdkafka */
    rd_kafka_message_destroy(rkmessage);
    usleep(def_.consumerInterval * 1000);
  }
}
//// End of methods added to merge mine for RSK

void JobMaker::run() {
  runThreadConsumeRawGw();
  // start Nmc Aux Block consumer thread
  // threadConsumeNmcAuxBlock_ = thread(&JobMaker::runThreadConsumeNmcAuxBlock, this);

  //   // start Rsk RawGw consumer thread
  //   threadConsumeRskRawGw_ = thread(&JobMaker::runThreadConsumeRawGw, this);

  // const int32_t timeoutMs = 1000;

  // while (running_) {
  //   rd_kafka_message_t *rkmessage;
  //   rkmessage = kafkaRawGbtConsumer_.consumer(timeoutMs);
  //   if (rkmessage == nullptr) /* timeout */
  //     continue;

  //   consumeRawGbtMsg(rkmessage, true);

  //   /* Return message to rdkafka */
  //   rd_kafka_message_destroy(rkmessage);

  // } /* /while */
}

void JobMaker::addRawgbt(const char *str, size_t len) {
  JsonNode r;
  if (!JsonNode::parse(str, str + len, r)) {
    LOG(ERROR) << "parse rawgbt message to json fail";
    return;
  }
  if (r["created_at_ts"].type()         != Utilities::JS::type::Int ||
      r["block_template_base64"].type() != Utilities::JS::type::Str ||
      r["gbthash"].type()               != Utilities::JS::type::Str) {
    LOG(ERROR) << "invalid rawgbt: missing fields";
    return;
  }

  const uint256 gbtHash = uint256S(r["gbthash"].str());
  for (const auto &itr : lastestGbtHash_) {
    if (gbtHash == itr) {
      LOG(ERROR) << "duplicate gbt hash: " << gbtHash.ToString();
      return;
    }
  }

  const uint32_t gbtTime = r["created_at_ts"].uint32();
  const int64_t timeDiff = (int64_t)time(nullptr) - (int64_t)gbtTime;
  if (labs(timeDiff) >= 60) {
    LOG(WARNING) << "rawgbt diff time is more than 60, ignore it";
    return;  // time diff too large, there must be some problems, so ignore it
  }
  if (labs(timeDiff) >= 3) {
    LOG(WARNING) << "rawgbt diff time is too large: " << timeDiff << " seconds";
  }

  const string gbt = DecodeBase64(r["block_template_base64"].str());
  assert(gbt.length() > 64);  // valid gbt string's len at least 64 bytes

  JsonNode nodeGbt;
  if (!JsonNode::parse(gbt.c_str(), gbt.c_str() + gbt.length(), nodeGbt)) {
    LOG(ERROR) << "parse gbt message to json fail";
    return;
  }
  assert(nodeGbt["result"]["height"].type() == Utilities::JS::type::Int);
  const uint32_t height = nodeGbt["result"]["height"].uint32();

  assert(nodeGbt["result"]["transactions"].type() == Utilities::JS::type::Array);
  const bool isEmptyBlock = nodeGbt["result"]["transactions"].array().size() == 0;

  {
    ScopeLock sl(lock_);

    if (rawgbtMap_.size() > 0) {
      const uint64_t bestKey = rawgbtMap_.rbegin()->first;
      const uint32_t bestTime = gbtKeyGetTime(bestKey);
      const uint32_t bestHeight = gbtKeyGetHeight(bestKey);
      const bool     bestIsEmpty = gbtKeyIsEmptyBlock(bestKey);

      // To prevent the job's block height ups and downs
      // when the block height of two bitcoind is not synchronized.
      // The block height downs must past twice the time of stratumJobInterval_
      // without the higher height GBT received.
      if (height < bestHeight && !bestIsEmpty && 
          gbtTime - bestTime < 2 * stratumJobInterval_) {
        LOG(WARNING) << "skip low height GBT. height: " << height
                     << ", best height: " << bestHeight
                     << ", elapsed time after best GBT: " << (gbtTime - bestTime) << "s";
        return;
      }
    }

    const uint64_t key = makeGbtKey(gbtTime, isEmptyBlock, height);
    if (rawgbtMap_.find(key) == rawgbtMap_.end()) {
      rawgbtMap_.insert(std::make_pair(key, gbt));
    } else {
      LOG(ERROR) << "key already exist in rawgbtMap: " << key;
    }
  }

  lastestGbtHash_.push_back(gbtHash);
  while (lastestGbtHash_.size() > 20) {
    lastestGbtHash_.pop_front();
  }

  LOG(INFO) << "add rawgbt, height: "<< height << ", gbthash: "
  << r["gbthash"].str().substr(0, 16) << "..., gbtTime(UTC): " << date("%F %T", gbtTime)
  << ", isEmpty:" << isEmptyBlock;
}

void JobMaker::clearTimeoutGbt() {
  // Maps (and sets) are sorted, so the first element is the smallest,
  // and the last element is the largest.

  const uint32_t ts_now = time(nullptr);

  for (auto itr = rawgbtMap_.begin(); itr != rawgbtMap_.end(); ) {
    const uint32_t ts  = gbtKeyGetTime(itr->first);
    const bool isEmpty = gbtKeyIsEmptyBlock(itr->first);
    const uint32_t height = gbtKeyGetHeight(itr->first);

    // gbt expired time
    const uint32_t expiredTime = ts + (isEmpty ? kEmptyGbtLifeTime_ : kGbtLifeTime_);

    if (expiredTime > ts_now) {
      // not expired
      ++itr;
    } else {
      // remove expired gbt
      LOG(INFO) << "remove timeout rawgbt: " << date("%F %T", ts) << "|" << ts <<
      ", height:" << height << ", isEmptyBlock:" << (isEmpty ? 1 : 0);

      // c++11: returns an iterator to the next element in the map
      itr = rawgbtMap_.erase(itr);
    }
  }
}

void JobMaker::sendStratumJob(const char *gbt) {
  string latestNmcAuxBlockJson;
  {
    ScopeLock sl(auxJsonlock_);
    latestNmcAuxBlockJson = latestNmcAuxBlockJson_;
  }

  RskWork currentRskBlockJson;
  {
    ScopeLock sl(rskWorkAccessLock_);
    if (currentRskWork_ != nullptr) {
      currentRskBlockJson = *currentRskWork_;
    }
  }

  StratumJob sjob;
  if (!sjob.initFromGbt(gbt, poolCoinbaseInfo_, poolPayoutAddr_, blockVersion_,
                        latestNmcAuxBlockJson, currentRskBlockJson)) {
    LOG(ERROR) << "init stratum job message from gbt str fail";
    return;
  }
  const string msg = sjob.serializeToJson();

  // sent to kafka
  kafkaProducer_.produce(msg.data(), msg.size());

  // set last send time
  lastJobSendTime_ = (uint32_t)time(nullptr);

  // is an empty block job
  isLastJobEmptyBlock_ = sjob.isEmptyBlock();

  // save send timestamp to file, for monitor system
  if (!fileLastJobTime_.empty())
  	writeTime2File(fileLastJobTime_.c_str(), (uint32_t)time(nullptr));

  LOG(INFO) << "--------producer stratum job, jobId: " << sjob.jobId_
  << ", height: " << sjob.height_ << "--------";
  LOG(INFO) << "sjob: " << msg;
}

bool JobMaker::isReachTimeout() {
  uint32_t intervalSeconds = stratumJobInterval_;

  if (lastJobSendTime_ + intervalSeconds <= time(nullptr)) {
    return true;
  }
  return false;
}

void JobMaker::checkAndSendStratumJob(bool isRskUpdate) {
  static uint64_t lastSendBestKey = 0;

  ScopeLock sl(lock_);

  // clean expired gbt first
  clearTimeoutGbt();
  clearTimeoutGw();

  if (rawgbtMap_.size() == 0) {
    LOG(WARNING) << "RawGbt Map is empty";
    return;
  }

  bool isFindNewHeight = false;
  bool needUpdateEmptyBlockJob = false;

  // rawgbtMap_ is sorted gbt by (timestamp + height + emptyFlag),
  // so the last item is the newest/best item.
  // @see makeGbtKey()
  const uint64_t bestKey = rawgbtMap_.rbegin()->first;

  const uint32_t bestHeight = gbtKeyGetHeight(bestKey);
  const bool currentGbtIsEmpty = gbtKeyIsEmptyBlock(bestKey);
  
  // if last job is an empty block job, we need to 
  // send a new non-empty job as quick as possible.
  if (bestHeight == currBestHeight_ && isLastJobEmptyBlock_ && !currentGbtIsEmpty) {
    needUpdateEmptyBlockJob = true;
    LOG(INFO) << "--------update last empty block job--------";
  }

  if (!needUpdateEmptyBlockJob && !isRskUpdate && bestKey == lastSendBestKey) {
    LOG(WARNING) << "bestKey is the same as last one: " << lastSendBestKey;
    return;
  }

  // The height cannot reduce in normal.
  // However, if there is indeed a height reduce,
  // isReachTimeout() will allow the new job sending.
  if (bestHeight > currBestHeight_) {
    LOG(INFO) << ">>>> found new best height: " << bestHeight
              << ", curr: " << currBestHeight_ << " <<<<";
    isFindNewHeight = true;
  }

  if (isFindNewHeight || needUpdateEmptyBlockJob || isRskUpdate || isReachTimeout()) {
    lastSendBestKey     = bestKey;
    currBestHeight_     = bestHeight;

    sendStratumJob(rawgbtMap_.rbegin()->second.c_str());
  }
}

uint64_t JobMaker::makeGbtKey(uint32_t gbtTime, bool isEmptyBlock, uint32_t height) {
  assert(height < 0x7FFFFFFFU);

  //
  // gbtKey:
  //  --------------------------------------------------------------------------------------
  // |               32 bits               |               31 bits              | 1 bit     |
  // | xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx | xxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx | x         |
  // |               gbtTime               |               height               | emptyFlag |
  //  --------------------------------------------------------------------------------------
  // use (!isEmptyBlock) so the key of non-empty block will large than the key of empty block.
  //
  return (((uint64_t)gbtTime) << 32) | (((uint64_t)height) << 1) | ((uint64_t)(!isEmptyBlock));
}

uint32_t JobMaker::gbtKeyGetTime(uint64_t gbtKey) {
  return (uint32_t)(gbtKey >> 32);
}

uint32_t JobMaker::gbtKeyGetHeight(uint64_t gbtKey) {
  return (uint32_t)((gbtKey >> 1) & 0x7FFFFFFFULL);
}

bool JobMaker::gbtKeyIsEmptyBlock(uint64_t gbtKey) {
  return !((bool)(gbtKey & 1ULL));
}

////////////////////////////////JobMakerEth//////////////////////////////////
// JobMakerEth::JobMakerEth(const string &kafkaBrokers, uint32_t stratumJobInterval,
//                          const string &payoutAddr, const string &fileLastJobTime,
//                          uint32_t rskNotifyPolicy, uint32_t blockVersion,
//                          const string &poolCoinbaseInfo) : 
//                          JobMaker(kafkaBrokers, stratumJobInterval,
//                          payoutAddr, 0, 0, fileLastJobTime,
//                          rskNotifyPolicy, blockVersion,
//                          poolCoinbaseInfo)
// {
// }

// bool JobMakerEth::initConsumer()
// {
//   //
//   // consumer gw messages
//   //
//   {
//     map<string, string> consumerOptions;
//     consumerOptions["fetch.wait.max.ms"] = "5";
//     if (!kafkaRawGwConsumer_.setup(RD_KAFKA_OFFSET_TAIL(1), &consumerOptions))
//     {
//       LOG(ERROR) << "kafka consumer rawgw block setup failure";
//       return false;
//     }
//     if (!kafkaRawGwConsumer_.checkAlive())
//     {
//       LOG(ERROR) << "kafka consumer rawgw block is NOT alive";
//       return false;
//     }
//   }
//   sleep(1);

//   return true;
// }

// void JobMakerEth::run() {
//     runThreadConsumeRawGw();
// }

// RskWork* JobMakerEth::createWork() {
//   return new RskWorkEth(); 
// }

// bool JobMakerEth::triggerRskUpdate() {
//   RskWork currentRskWork;
//   RskWork previousRskWork;
//   {
//     ScopeLock sl(rskWorkAccessLock_);
//     if (previousRskWork_ == nullptr) {
//       if (currentRskWork_ == nullptr)
//         return false;
//       else //first job
//         return true;
//     }

//     currentRskWork = *currentRskWork_;
//     previousRskWork = *previousRskWork_;
//   }

//   // Check if header changes so the new workpackage is really new
//   return currentRskWork.getBlockHash() != previousRskWork.getBlockHash();
// }

// void JobMakerEth::checkAndSendStratumJob(bool isRskUpdate) {
//   // static uint64_t lastSendBestKey = 0;
//   ScopeLock sl(lock_);

//   // clean expired gbt first
//   clearTimeoutGw();

//   sendGwStratumJob();
// }

// void JobMakerEth::sendGwStratumJob() {
//   RskWorkEth currentRskBlockJson;
//   {
//     ScopeLock sl(rskWorkAccessLock_);
//     if (currentRskWork_ != nullptr)
//       currentRskBlockJson = *(dynamic_cast<RskWorkEth*>(currentRskWork_));
//   }

//   if (0 == currentRskBlockJson.getRpcAddress().length())
//     return;

//   const string request = "{\"jsonrpc\":\"2.0\",\"method\":\"eth_getBlockByNumber\",\"params\":[\"pending\", false],\"id\":2}";
//   string response;
//   bool res = bitcoindRpcCall(currentRskBlockJson.getRpcAddress().c_str(), currentRskBlockJson.getRpcUserPwd().c_str(), request.c_str(), response);
//   if (!res)
//     LOG(ERROR) << "get pending block failed";

//   StratumJobEth sjob;
//   if (!sjob.initFromGw(poolPayoutAddr_, currentRskBlockJson, response)) {
//     LOG(ERROR) << "init stratum job message from gw str fail";
//     return;
//   }

//   const string msg = sjob.serializeToJson();
//   // sent to kafka
//   DLOG(INFO) << "produce eth job: " << msg;
//   kafkaProducer_.produce(msg.data(), msg.size());
// }

////////////////////////////////JobMakerHandlerEth//////////////////////////////////
bool JobMakerHandlerEth::processMsg(const string &msg)
{
  shared_ptr<RskWork> rskWork = make_shared<RskWorkEth>();
  if (rskWork->initFromGw(msg))
  {
    previousRskWork_ = currentRskWork_;
    currentRskWork_ = rskWork;
    DLOG(INFO) << "currentRskBlockJson: " << msg;
  }
  else
    return false;

  clearTimeoutMsg();

  if (nullptr == previousRskWork_ && nullptr == currentRskWork_)
    return false;

  //first job 
  if (nullptr == previousRskWork_ && currentRskWork_ != nullptr)
    return true;

  // Check if header changes so the new workpackage is really new
  return currentRskWork_->getBlockHash() != previousRskWork_->getBlockHash(); 
}

void JobMakerHandlerEth::clearTimeoutMsg() {
  const uint32_t now = time(nullptr);
  if(currentRskWork_ != nullptr && currentRskWork_->getCreatedAt() + def_.maxJobDelay < now) 
      currentRskWork_ = nullptr;

  if(previousRskWork_ != nullptr && previousRskWork_->getCreatedAt() + def_.maxJobDelay < now) 
      previousRskWork_ = nullptr;
}

string JobMakerHandlerEth::buildStratumJobMsg()
{
  if (nullptr == currentRskWork_)
    return "";

  if (0 == currentRskWork_->getRpcAddress().length())
    return "";

  RskWorkEth *currentRskBlockJson = dynamic_cast<RskWorkEth*>(currentRskWork_.get());
  if (nullptr == currentRskBlockJson)
    return "";

  const string request = "{\"jsonrpc\":\"2.0\",\"method\":\"eth_getBlockByNumber\",\"params\":[\"pending\", false],\"id\":2}";
  string response;
  bool res = bitcoindRpcCall(currentRskBlockJson->getRpcAddress().c_str(), currentRskBlockJson->getRpcUserPwd().c_str(), request.c_str(), response);
  if (!res)
    LOG(ERROR) << "get pending block failed";

  StratumJobEth sjob;
  if (!sjob.initFromGw(*currentRskBlockJson, response)) {
    LOG(ERROR) << "init stratum job message from gw str fail";
    return "";
  }

  return sjob.serializeToJson();
}

////////////////////////////////JobMakerHandlerSia//////////////////////////////////
JobMakerHandlerSia::JobMakerHandlerSia() : time_(0)
{
}

bool JobMakerHandlerSia::processMsg(const string& msg) {
  JsonNode j;
  if (!JsonNode::parse(msg.c_str(), msg.c_str() + msg.length(), j))
  {
    LOG(ERROR) << "deserialize sia work failed " << msg;
    return false;
  }

  if (!validate(j))
    return false;

  string header = move(j["hHash"].str());
  if (header == header_) 
    return false;
  
  header_ = move(header);
  target_ = move(j["target"].str());
  time_ =  j["created_at_ts"].uint32();

  return true;
}

bool JobMakerHandlerSia::validate(JsonNode &work)
{
  // check fields are valid
  if (work.type() != Utilities::JS::type::Obj ||
    work["created_at_ts"].type() != Utilities::JS::type::Int ||
    work["rskdRpcAddress"].type() != Utilities::JS::type::Str ||
    work["rskdRpcUserPwd"].type() != Utilities::JS::type::Str ||
    work["target"].type() != Utilities::JS::type::Str ||
    work["hHash"].type() != Utilities::JS::type::Str) {
      LOG(ERROR) << "work format not expected";
    return false;
    }

  // check timestamp
  if (work["created_at_ts"].uint32() + def_.maxJobDelay < time(nullptr))
  {
    LOG(ERROR) << "too old sia work: " << date("%F %T", work["created_at_ts"].uint32());
    return false;
  }

  return true;
}

string JobMakerHandlerSia::buildStratumJobMsg()
{
  if (0 == header_.size() ||
      0 == target_.size())
    return "";

  return Strings::Format("{\"created_at_ts\":%u"
                         ",\"target\":\"%s\""
                         ",\"hHash\":\"%s\""
                         "}",
                         time_,
                         target_.c_str(),
                         header_.c_str());
}