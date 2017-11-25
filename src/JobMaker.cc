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

#include "bitcoin/hash.h"
#include "bitcoin/script/script.h"
#include "bitcoin/uint256.h"
#include "bitcoin/util.h"
#include "bitcoin/utilstrencodings.h"

#include "utilities_js.hpp"
#include "Utils.h"


///////////////////////////////////  JobMaker  /////////////////////////////////
JobMaker::JobMaker(const string &kafkaBrokers,  uint32_t stratumJobInterval,
                   const string &payoutAddr, uint32_t gbtLifeTime,
                   uint32_t emptyGbtLifeTime, const string &fileLastJobTime,
                   uint32_t blockVersion, const string &poolCoinbaseInfo):
running_(true),
kafkaBrokers_(kafkaBrokers),
kafkaProducer_(kafkaBrokers_.c_str(), KAFKA_TOPIC_STRATUM_JOB, RD_KAFKA_PARTITION_UA/* partition */),
kafkaRawGbtConsumer_(kafkaBrokers_.c_str(), KAFKA_TOPIC_RAWGBT,       0/* partition */),
kafkaNmcAuxConsumer_(kafkaBrokers_.c_str(), KAFKA_TOPIC_NMC_AUXBLOCK, 0/* partition */),
currBestHeight_(0), lastJobSendTime_(0),
isLastJobEmptyBlock_(false), isLastJobNewHeight_(false),
stratumJobInterval_(stratumJobInterval),
poolCoinbaseInfo_(poolCoinbaseInfo), poolPayoutAddr_(payoutAddr),
kGbtLifeTime_(gbtLifeTime), kEmptyGbtLifeTime_(emptyGbtLifeTime),
fileLastJobTime_(fileLastJobTime),
blockVersion_(blockVersion)
{
	LOG(INFO) << "Block Version: " << std::hex << blockVersion_;
	LOG(INFO) << "Coinbase Info: " << poolCoinbaseInfo_;
}

JobMaker::~JobMaker() {
  if (threadConsumeNmcAuxBlock_.joinable())
    threadConsumeNmcAuxBlock_.join();
}

void JobMaker::stop() {
  if (!running_) {
    return;
  }
  running_ = false;
  LOG(INFO) << "stop jobmaker";
}

bool JobMaker::init() {
  const int32_t consumeLatestN = 20;
  // check pool payout address
  if (!poolPayoutAddr_.IsValid()) {
    LOG(ERROR) << "invalid pool payout address";
    return false;
  }

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

  //
  // consumer RawGbt, offset: latest N messages
  //
  {
    map<string, string> consumerOptions;
    consumerOptions["fetch.wait.max.ms"] = "5";
    if (!kafkaRawGbtConsumer_.setup(RD_KAFKA_OFFSET_TAIL(consumeLatestN), &consumerOptions)) {
      LOG(ERROR) << "kafka consumer rawgbt setup failure";
      return false;
    }
    if (!kafkaRawGbtConsumer_.checkAlive()) {
      LOG(ERROR) << "kafka consumer rawgbt is NOT alive";
      return false;
    }
  }
  // sleep 3 seconds, wait for the latest N messages transfer from broker to client
  sleep(3);

  //
  // consumer, namecoin aux block
  //
  {
    map<string, string> consumerOptions;
    consumerOptions["fetch.wait.max.ms"] = "5";
    if (!kafkaNmcAuxConsumer_.setup(RD_KAFKA_OFFSET_TAIL(1), &consumerOptions)) {
      LOG(ERROR) << "kafka consumer nmc aux block setup failure";
      return false;
    }
    if (!kafkaNmcAuxConsumer_.checkAlive()) {
      LOG(ERROR) << "kafka consumer nmc aux block is NOT alive";
      return false;
    }
  }
  sleep(1);

  //
  // consumer latest NmcAuxBlock
  //
  {
    rd_kafka_message_t *rkmessage;
    rkmessage = kafkaNmcAuxConsumer_.consumer(1000/* timeout ms */);
    if (rkmessage != nullptr) {
      consumeNmcAuxBlockMsg(rkmessage);
      rd_kafka_message_destroy(rkmessage);
    }
  }

  //
  // consumer latest RawGbt N messages
  //
  // read latest gbtmsg from kafka
  LOG(INFO) << "consume latest rawgbt message from kafka...";
  for (int32_t i = 0; i < consumeLatestN; i++) {
    rd_kafka_message_t *rkmessage;
    rkmessage = kafkaRawGbtConsumer_.consumer(5000/* timeout ms */);
    if (rkmessage == nullptr) {
      break;
    }
    consumeRawGbtMsg(rkmessage, false);
    rd_kafka_message_destroy(rkmessage);
  }
  LOG(INFO) << "consume latest rawgbt messages done";
  checkAndSendStratumJob();

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
    checkAndSendStratumJob();
  }
}

void JobMaker::runThreadConsumeNmcAuxBlock() {
  const int32_t timeoutMs = 1000;

  while (running_) {
    rd_kafka_message_t *rkmessage;
    rkmessage = kafkaNmcAuxConsumer_.consumer(timeoutMs);
    if (rkmessage == nullptr) /* timeout */
      continue;

    consumeNmcAuxBlockMsg(rkmessage);

    /* Return message to rdkafka */
    rd_kafka_message_destroy(rkmessage);
  }
}

void JobMaker::run() {
  // start Nmc Aux Block consumer thread
  threadConsumeNmcAuxBlock_ = thread(&JobMaker::runThreadConsumeNmcAuxBlock, this);

  const int32_t timeoutMs = 1000;

  while (running_) {
    rd_kafka_message_t *rkmessage;
    rkmessage = kafkaRawGbtConsumer_.consumer(timeoutMs);
    if (rkmessage == nullptr) /* timeout */
      continue;

    consumeRawGbtMsg(rkmessage, true);

    /* Return message to rdkafka */
    rd_kafka_message_destroy(rkmessage);

  } /* /while */
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
    LOG(WARNING) << "rawgbt diff time is more than 60, ingore it";
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
  assert(height < 0x7FFFFFFFU);

  assert(nodeGbt["result"]["transactions"].type() == Utilities::JS::type::Array);
  // 0x80000000 : 1000 0000 0000 0000 0000 0000 0000 0000
  const uint64_t emptyFlag = nodeGbt["result"]["transactions"].array().size() == 0 ? 0x80000000ULL : 0;

  {
    ScopeLock sl(lock_);
    const uint64_t key = ((uint64_t)gbtTime << 32) + emptyFlag + (uint64_t)height;
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
  << ", isEmpty:" << (emptyFlag != 0);
}

void JobMaker::clearTimeoutGbt() {
  // Maps (and sets) are sorted, so the first element is the smallest,
  // and the last element is the largest.

  const uint32_t ts_now = time(nullptr);

  for (auto itr = rawgbtMap_.begin(); itr != rawgbtMap_.end(); ) {
    const uint32_t ts  = (uint32_t)(itr->first >> 32);
    const bool isEmpty = (itr->first & 0x80000000ULL) != 0;
    // 0x7FFFFFFF : 0111 1111 1111 1111 1111 1111 1111 1111
    const uint32_t height = (uint32_t)(itr->first & 0x7FFFFFFFULL);

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

  StratumJob sjob;
  if (!sjob.initFromGbt(gbt, poolCoinbaseInfo_, poolPayoutAddr_, blockVersion_,
                        latestNmcAuxBlockJson)) {
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

void JobMaker::findNewBestHeight(std::map<uint64_t/* height + ts */, const char *> *gbtByHeight) {
  for (const auto &itr : rawgbtMap_) {
    const uint32_t timestamp = (uint32_t)((itr.first >> 32) & 0x00000000FFFFFFFFULL);
    const uint32_t height    = (uint32_t)(itr.first         & 0x000000007FFFFFFFULL);

    // using Map to sort by: height + timestamp, without empty block flag
    const uint64_t key = ((uint64_t)height << 32) + (uint64_t)timestamp;
    gbtByHeight->insert(std::make_pair(key, itr.second.c_str()));
  }
}

bool JobMaker::isReachTimeout() {
  uint32_t intervalSeconds = stratumJobInterval_;

  // if last job is new height and empty block, reduce interval seconds
  if (isLastJobNewHeight_ && isLastJobEmptyBlock_) {
    intervalSeconds = 10;
  }

  if (lastJobSendTime_ + intervalSeconds <= time(nullptr)) {
    return true;
  }
  return false;
}

void JobMaker::checkAndSendStratumJob() {
  static uint64_t lastSendBestKey = 0; /* height + ts */

  ScopeLock sl(lock_);
  if (rawgbtMap_.size() == 0) {
    return;
  }
  std::map<uint64_t/* height + ts */, const char *> gbtByHeight;

  // clean expired gbt first
  clearTimeoutGbt();

  // we need to build 'gbtByHeight' first
  findNewBestHeight(&gbtByHeight);
  bool isFindNewHeight = false;
  bool needUpdateEmptyBlockJob = false;

  // key: empty_block_flag + height + timestamp
  const uint64_t bestKey = gbtByHeight.rbegin()->first;
  const bool currentGbtIsEmpty = (gbtByHeight.rbegin()->first & 0x80000000ULL) != 0;
  
  // if last job is an empty block job, we need to 
  // send a new non-empty job as quick as possible.
  if (isLastJobEmptyBlock_ && !currentGbtIsEmpty) {
    needUpdateEmptyBlockJob = true;
    LOG(INFO) << "--------update last empty block job--------";
  }

  if (!needUpdateEmptyBlockJob && bestKey == lastSendBestKey) {
    LOG(WARNING) << "bestKey is the same as last one: " << lastSendBestKey;
    return;
  }

  // get bestHeight without empty block flag
  const uint32_t bestHeight = (uint32_t)((bestKey >> 32) & 0x000000007FFFFFFFULL);

  if (bestHeight != currBestHeight_) {
    LOG(INFO) << ">>>> found new best height: " << bestHeight
    << ", curr: " << currBestHeight_ << " <<<<";
    isFindNewHeight = true;
    currBestHeight_ = bestHeight;
  }

  if (isFindNewHeight || needUpdateEmptyBlockJob || isReachTimeout()) {
    lastSendBestKey     = bestKey;
    isLastJobNewHeight_ = isFindNewHeight;

    sendStratumJob(gbtByHeight.rbegin()->second);
  }
}


