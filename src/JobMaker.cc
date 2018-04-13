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
JobMaker::JobMaker(const JobMakerDefinition def,
                   const string &brokers) : def_(def),
                                            running_(true),
                                            kafkaProducer_(brokers.c_str(), def.producerTopic.c_str(), RD_KAFKA_PARTITION_UA),
                                            kafkaRawGwConsumer_(brokers.c_str(), def.consumerTopic.c_str(), 0)
{
}

JobMaker::~JobMaker() {
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

  return true;
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

  string msg((const char *)rkmessage->payload, rkmessage->len);
  if (def_.handler && def_.handler->processMsg(msg)) {
    const string producerMsg = def_.handler->buildStratumJobMsg();
    if (producerMsg.size() > 0) {
      LOG(INFO) << "new " << def_.producerTopic << " job: " << producerMsg;
      kafkaProducer_.produce(producerMsg.data(), producerMsg.size());
    }
  }

}

void JobMaker::runThreadConsumeRawGw() {
  const int32_t timeoutMs = 1000;

  while (running_) {
    rd_kafka_message_t *rkmessage;
    rkmessage = kafkaRawGwConsumer_.consumer(timeoutMs);
    if (rkmessage == nullptr) /* timeout */ {
      continue;
    }

    consumeRawGwMsg(rkmessage);

    /* Return message to rdkafka */
    rd_kafka_message_destroy(rkmessage);

    // Don't add any sleep() here.
    // Kafka will not skip any message during your sleep(), you will received
    // all messages from your beginning offset to the latest in any case.
    // So sleep() will cause unexpected delay before consumer a new message.
    // If the producer's speed is faster than the sleep() here, the consumption
    // will be delayed permanently and the latest message will never be received.

    // At the same time, there is not a busy waiting.
    // KafkaConsumer::consumer(timeoutMs) will return after `timeoutMs` millisecond
    // if no new messages. You can increase `timeoutMs` if you want.
  }
}
//// End of methods added to merge mine for RSK

void JobMaker::run() {
  runThreadConsumeRawGw();
}


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

  string header = j["hHash"].str();
  if (header == header_) 
    return false;
  
  header_ = move(header);
  target_ = j["target"].str();
  time_ =  j["created_at_ts"].uint32();

  return true;
}

bool JobMakerHandlerSia::validate(JsonNode &work)
{
  // check fields are valid
  if (work.type() != Utilities::JS::type::Obj ||
    work["created_at_ts"].type() != Utilities::JS::type::Int ||
    work["rpcAddress"].type() != Utilities::JS::type::Str ||
    work["rpcUserPwd"].type() != Utilities::JS::type::Str ||
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

  const string jobIdStr = Strings::Format("%08x%08x", (uint32_t)time(nullptr), djb2(header_.c_str()));
  assert(jobIdStr.length() == 16);
  size_t pos;
  uint64 jobId = stoull(jobIdStr, &pos, 16);

  return Strings::Format("{\"created_at_ts\":%u"
                         ",\"jobId\":%" PRIu64 ""
                         ",\"target\":\"%s\""
                         ",\"hHash\":\"%s\""
                         "}",
                         time_,
                         jobId,
                         target_.c_str(),
                         header_.c_str());
}
