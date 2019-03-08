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
#include "BlockMaker.h"

////////////////////////////////// BlockMaker //////////////////////////////////
BlockMaker::BlockMaker(
    shared_ptr<BlockMakerDefinition> def,
    const char *kafkaBrokers,
    const MysqlConnectInfo &poolDB)
  : def_(def)
  , running_(true)
  , kafkaConsumerSolvedShare_(
        kafkaBrokers, def_->solvedShareTopic_.c_str(), 0 /* patition */)
  , poolDB_(poolDB) {
}

BlockMaker::~BlockMaker() {
}

void BlockMaker::stop() {
  if (!running_)
    return;

  running_ = false;
  LOG(INFO) << "stop block maker";
}

bool BlockMaker::init() {

  //
  // Sloved Share
  //
  // we need to consume the latest 2 messages, just in case
  if (kafkaConsumerSolvedShare_.setup(RD_KAFKA_OFFSET_TAIL(2)) == false) {
    LOG(INFO) << "setup kafkaConsumerSolvedShare_ fail";
    return false;
  }
  if (!kafkaConsumerSolvedShare_.checkAlive()) {
    LOG(ERROR) << "kafka brokers is not alive: kafkaConsumerSolvedShare_";
    return false;
  }

  return true;
}

void BlockMaker::consumeSolvedShare(rd_kafka_message_t *rkmessage) {
  // check error
  if (rkmessage->err) {
    if (rkmessage->err == RD_KAFKA_RESP_ERR__PARTITION_EOF) {
      // Reached the end of the topic+partition queue on the broker.
      // Not really an error.
      //      LOG(INFO) << "consumer reached end of " <<
      //      rd_kafka_topic_name(rkmessage->rkt)
      //      << "[" << rkmessage->partition << "] "
      //      << " message queue at offset " << rkmessage->offset;
      // acturlly
      return;
    }

    LOG(ERROR) << "consume error for topic "
               << rd_kafka_topic_name(rkmessage->rkt) << "["
               << rkmessage->partition << "] offset " << rkmessage->offset
               << ": " << rd_kafka_message_errstr(rkmessage);

    if (rkmessage->err == RD_KAFKA_RESP_ERR__UNKNOWN_PARTITION ||
        rkmessage->err == RD_KAFKA_RESP_ERR__UNKNOWN_TOPIC) {
      LOG(FATAL) << "consume fatal";
      stop();
    }
    return;
  }

  LOG(INFO) << "received SolvedShare message, len: " << rkmessage->len;
  processSolvedShare(rkmessage);
}

void BlockMaker::runThreadConsumeSolvedShare() {
  const int32_t timeoutMs = 1000;

  while (running_) {
    rd_kafka_message_t *rkmessage;
    rkmessage = kafkaConsumerSolvedShare_.consumer(timeoutMs);
    if (rkmessage == nullptr) /* timeout */
      continue;

    consumeSolvedShare(rkmessage);

    /* Return message to rdkafka */
    rd_kafka_message_destroy(rkmessage);
  }
}

void BlockMaker::run() {
  std::this_thread::sleep_for(3s);

  runThreadConsumeSolvedShare();
}
