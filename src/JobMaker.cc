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

#include <stdlib.h>

#include <glog/logging.h>
#include <librdkafka/rdkafka.h>

#include "bitcoin/util.h"
#include "utilities_js.hpp"
#include "Utils.h"


///////////////////////////////////  JobMaker  /////////////////////////////////
JobMaker::JobMaker(const string &kafkaBrokers,  uint32_t stratumJobIntveral):
running_(true),
kafkaBrokers_(kafkaBrokers),
kafkaProducer_(kafkaBrokers_.c_str(), KAFKA_TOPIC_STRATUM_JOB, 1/* partition */),
kafkaConsumer_(kafkaBrokers_.c_str(), KAFKA_TOPIC_RAWGBT,      1/* partition */),
currBestHeight_(0), kGbtLifeTime_(120U), stratumJobInterval_(stratumJobIntveral)
{
}

JobMaker::~JobMaker() {}

void JobMaker::stop() {
  if (!running_) {
    return;
  }
  running_ = false;
  LOG(INFO) << "stop jobmaker";
}

bool JobMaker::init() {
  /* setup kafka */
  if (!kafkaProducer_.setup()) {
    LOG(ERROR) << "kafka producer setup failure";
    return false;
  }
  if (!kafkaProducer_.checkAlive()) {
    LOG(ERROR) << "kafka producer is NOT alive";
    return false;
  }
  // consumer offset: latest N messages
  if (!kafkaConsumer_.setup(RD_KAFKA_OFFSET_TAIL(-20))) {
    LOG(ERROR) << "kafka consumer setup failure";
    return false;
  }
  if (!kafkaConsumer_.checkAlive()) {
    LOG(ERROR) << "kafka consumer is NOT alive";
    return false;
  }
  return true;
}

void JobMaker::addRawgbt(const char *str, size_t len) {
  JsonNode r;
  if (!JsonNode::parse(str, str + len, r)) {
    LOG(ERROR) << "parse rawgbt message to json fail";
    return;
  }
  if (r["created_at_ts"].type() != Utilities::JS::type::Int ||
      r["block_template_base64"].type() != Utilities::JS::type::Str) {
    LOG(ERROR) << "invalid rawgbt: missing fields";
    return;
  }

  const uint32_t gbtTime = r["created_at_ts"].uint32();
  const int64_t timeDiff = (int64_t)time(nullptr) - (int64_t)gbtTime;
  if (labs(timeDiff) >= 60) {
    LOG(ERROR) << "rawgbt diff time is more than 60";
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

  {
    ScopeLock sl(lock_);
    const uint64_t key = ((uint64_t)gbtTime << 32) + (uint64_t)height;
    rawgbtMap_.insert(std::make_pair(key, gbt));
  }
}

void JobMaker::clearTimeoutGbt() {
  // Maps (and sets) are sorted, so the first element is the smallest,
  // and the last element is the largest.
  while (rawgbtMap_.size()) {
    auto itr = rawgbtMap_.begin();
    const uint32_t ts = (uint32_t)(itr->first >> 32);
    if (ts + kGbtLifeTime_ > time(nullptr)) {
      break;
    }
    // remove the oldest gbt
    LOG(INFO) << "remove oldest rawgbt: " << date("%F %T", ts) << "|" << ts;
    rawgbtMap_.erase(itr);
  }
}

void JobMaker::sendStratumJob(const char *gbt) {
  // decode & make stratum job
  
}

bool JobMaker::isFindNewBestHeight(std::map<uint64_t/* height + ts */, const char *> *gbtByHeight) {
  for (const auto &itr : rawgbtMap_) {
    const uint32_t timestamp = (uint32_t)((itr.first >> 32) & 0x00000000FFFFFFFFULL);
    const uint32_t height    = (uint32_t)(itr.first         & 0x00000000FFFFFFFFULL);

    const uint64_t key = ((uint64_t)height << 32) + (uint64_t)timestamp;
    gbtByHeight->insert(std::make_pair(key, itr.second.c_str()));
  }
  const uint64_t bestKey = gbtByHeight->rbegin()->first;
  uint32_t bestHeight = (uint32_t)((bestKey >> 32) & 0x00000000FFFFFFFFULL);

  if (bestHeight != currBestHeight_) {
    LOG(INFO) << "found new best height: " << bestHeight << ", curr: " << currBestHeight_;
    return true;
  }
  return false;
}

bool JobMaker::isReachTimeout() {
  if (lastJobSendTime_ + stratumJobInterval_ <= time(nullptr)) {
    return true;
  }
  return false;
}

void JobMaker::checkAndSendStratumJob() {
  ScopeLock sl(lock_);
  if (rawgbtMap_.size() == 0) {
    return;
  }

  std::map<uint64_t/* height + ts */, const char *> gbtByHeight;
  clearTimeoutGbt();
  // we need to build 'gbtByHeight' first
  bool isFindNewHeight = isFindNewBestHeight(&gbtByHeight);

  if (isFindNewHeight || isReachTimeout()) {
    sendStratumJob(gbtByHeight.rbegin()->second);
  }
}

void JobMaker::run() {

}

