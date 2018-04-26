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

#ifdef INCLUDE_BTC_KEY_IO_H //  
#include <key_io.h> //  IsValidDestinationString for bch is not in this file.
#endif

#include "utilities_js.hpp"
#include "Utils.h"
#include "BitcoinUtils.h"

///////////////////////////////////  JobMaker  /////////////////////////////////
JobMaker::JobMaker(shared_ptr<JobMakerHandler> handler,
                   const string &kafkaBrokers,
                   const string& zookeeperBrokers) : handler_(handler),
                                            running_(true),
                                            zkLocker_(zookeeperBrokers.c_str()),
                                            kafkaBrokers_(kafkaBrokers),
                                            kafkaProducer_(kafkaBrokers.c_str(), handler->def().jobTopic_.c_str(), RD_KAFKA_PARTITION_UA)
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

  /* get lock from zookeeper */
  try {
    if (handler_->def().zookeeperLockPath_.empty()) {
      LOG(ERROR) << "zookeeper lock path is empty!";
      return false;
    }
    zkLocker_.getLock(handler_->def().zookeeperLockPath_.c_str());
  } catch(const ZookeeperException &zooex) {
    LOG(ERROR) << zooex.what();
    return false;
  }

  /* setup kafka producer */
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

  /* setup kafka consumers */
  if (!handler_->initConsumerHandlers(kafkaBrokers_, kafkaConsumerHandlers_)) {
    return false;
  }

  return true;
}

void JobMaker::consumeKafkaMsg(rd_kafka_message_t *rkmessage, JobMakerConsumerHandler &consumerHandler)
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
  LOG(INFO) << "received " << consumerHandler.kafkaTopic_ << " message len: " << rkmessage->len;

  string msg((const char *)rkmessage->payload, rkmessage->len);

  if (consumerHandler.messageProcessor_(msg)) {
    LOG(INFO) << "handleMsg returns true, new stratum job";
    produceStratumJob();
  }

}

void JobMaker::produceStratumJob() {
  const string jobMsg = handler_->makeStratumJobMsg();

  if (!jobMsg.empty()) {
    LOG(INFO) << "new " << handler_->def().jobTopic_ << " job: " << jobMsg;
    kafkaProducer_.produce(jobMsg.data(), jobMsg.size());
  }

  lastJobTime_ = time(nullptr);
  
  // save send timestamp to file, for monitor system
  if (!handler_->def().fileLastJobTime_.empty()) {
    // TODO: fix Y2K38 issue
  	writeTime2File(handler_->def().fileLastJobTime_.c_str(), (uint32_t)lastJobTime_);
  }
}

void JobMaker::runThreadKafkaConsume(JobMakerConsumerHandler &consumerHandler) {
  const int32_t timeoutMs = 1000;

  while (running_) {
    rd_kafka_message_t *rkmessage;
    rkmessage = consumerHandler.kafkaConsumer_->consumer(timeoutMs);
    if (rkmessage == nullptr) /* timeout */ {
      continue;
    }

    consumeKafkaMsg(rkmessage, consumerHandler);

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

void JobMaker::run() {

  // running consumer threads
  for (JobMakerConsumerHandler &consumerhandler : kafkaConsumerHandlers_)
  {
    kafkaConsumerWorkers_.push_back(std::make_shared<thread>(std::bind(&JobMaker::runThreadKafkaConsume, this, consumerhandler)));
  }

  // produce stratum job regularly
  // the stratum job producing will also be triggered by consumer threads
  while (running_) {
    sleep(1);

    if (time(nullptr) - lastJobTime_ > handler_->def().jobInterval_) {
      produceStratumJob();
    }
  }

  // wait consumer threads exit
  for (auto pWorker : kafkaConsumerWorkers_) {
    if (pWorker->joinable()) {
      LOG(INFO) << "wait for worker " << pWorker->get_id() << "exiting";
      pWorker->join();
      LOG(INFO) << "worker exited";
    }
  }
}


////////////////////////////////GwJobMakerHandler//////////////////////////////////
bool GwJobMakerHandler::initConsumerHandlers(const string &kafkaBrokers, vector<JobMakerConsumerHandler> &handlers)
{
  JobMakerConsumerHandler handler = {
    /* kafkaTopic_ = */       def_.rawGwTopic_,
    /* kafkaConsumer_ = */    std::make_shared<KafkaConsumer>(kafkaBrokers.c_str(), def_.rawGwTopic_.c_str(), 0/* partition */),
    /* messageProcessor_ = */ std::bind(&GwJobMakerHandler::processMsg, this, std::placeholders::_1)
  };

  // init kafka consumer
  {
    map<string, string> consumerOptions;
    consumerOptions["fetch.wait.max.ms"] = "5";
    if (!handler.kafkaConsumer_->setup(RD_KAFKA_OFFSET_TAIL(1), &consumerOptions)) {
      LOG(ERROR) << "kafka consumer " << def_.rawGwTopic_ << " setup failure";
      return false;
    }
    if (!handler.kafkaConsumer_->checkAlive()) {
      LOG(FATAL) << "kafka consumer " << def_.rawGwTopic_ << " is NOT alive";
      return false;
    }
  }

  handlers.push_back(handler);
  return true;
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
  else {
    LOG(ERROR) << "eth initFromGw failed " << msg;
    return false;
  }

  clearTimeoutMsg();

  if (nullptr == previousRskWork_ && nullptr == currentRskWork_) {
    LOG(ERROR) << "no current work" << msg;
    return false;
  }

  //first job 
  if (nullptr == previousRskWork_ && currentRskWork_ != nullptr)
    return true;

  // Check if header changes so the new workpackage is really new
  return currentRskWork_->getBlockHash() != previousRskWork_->getBlockHash(); 
}

void JobMakerHandlerEth::clearTimeoutMsg() {
  const uint32_t now = time(nullptr);
  if(currentRskWork_ != nullptr && currentRskWork_->getCreatedAt() + def_.maxJobDelay_ < now) 
      currentRskWork_ = nullptr;

  if(previousRskWork_ != nullptr && previousRskWork_->getCreatedAt() + def_.maxJobDelay_ < now) 
      previousRskWork_ = nullptr;
}

string JobMakerHandlerEth::makeStratumJobMsg()
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

bool JobMakerHandlerSia::processMsg(const string &msg)
{
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
  time_ = j["created_at_ts"].uint32();

  return processMsg(j);
}

bool JobMakerHandlerSia::processMsg(JsonNode &j)
{
  target_ = j["target"].str();
  return true;
}

bool JobMakerHandlerSia::validate(JsonNode &j)
{
  // check fields are valid
  if (j.type() != Utilities::JS::type::Obj ||
    j["created_at_ts"].type() != Utilities::JS::type::Int ||
    j["rpcAddress"].type() != Utilities::JS::type::Str ||
    j["rpcUserPwd"].type() != Utilities::JS::type::Str ||
    j["target"].type() != Utilities::JS::type::Str ||
    j["hHash"].type() != Utilities::JS::type::Str) {
      LOG(ERROR) << "work format not expected";
    return false;
    }

  // check timestamp
  if (j["created_at_ts"].uint32() + def_.maxJobDelay_ < time(nullptr))
  {
    LOG(ERROR) << "too old sia work: " << date("%F %T", j["created_at_ts"].uint32());
    return false;
  }

  return true;
}

string JobMakerHandlerSia::makeStratumJobMsg()
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

///////////////////////////////////JobMakerHandlerBytom//////////////////////////////////
bool JobMakerHandlerBytom::processMsg(JsonNode &j)
{
  seed_ = j["sHash"].str();
  return true;
}

bool JobMakerHandlerBytom::validate(JsonNode &j)
{
  // check fields are valid
  if (j.type() != Utilities::JS::type::Obj ||
    j["created_at_ts"].type() != Utilities::JS::type::Int ||
    j["rpcAddress"].type() != Utilities::JS::type::Str ||
    j["rpcUserPwd"].type() != Utilities::JS::type::Str ||
    j["hHash"].type() != Utilities::JS::type::Str) {
      LOG(ERROR) << "work format not expected";
    return false;
    }

  // check timestamp
  if (j["created_at_ts"].uint32() + def_.maxJobDelay_ < time(nullptr))
  {
    LOG(ERROR) << "too old bytom work: " << date("%F %T", j["created_at_ts"].uint32());
    return false;
  }

  return true;
}

string JobMakerHandlerBytom::makeStratumJobMsg()
{
  if (0 == header_.size() ||
      0 == seed_.size())
    return "";

  const string jobIdStr = Strings::Format("%08x%08x", (uint32_t)time(nullptr), djb2(header_.c_str()));
  assert(jobIdStr.length() == 16);
  size_t pos;
  uint64 jobId = stoull(jobIdStr, &pos, 16);

  return Strings::Format("{\"created_at_ts\":%u"
                         ",\"jobId\":%" PRIu64 ""
                         ",\"sHash\":\"%s\""
                         ",\"hHash\":\"%s\""
                         "}",
                         time_,
                         jobId,
                         seed_.c_str(),
                         header_.c_str());
}

////////////////////////////////JobMakerHandlerBitcoin//////////////////////////////////
JobMakerHandlerBitcoin::JobMakerHandlerBitcoin() :
currBestHeight_(0), lastJobSendTime_(0),
isLastJobEmptyBlock_(false),
previousRskWork_(nullptr), currentRskWork_(nullptr),
isRskUpdate_(false)
{
}

bool JobMakerHandlerBitcoin::init(const GbtJobMakerDefinition &def) {
  def_ = def;

  // select chain
  if (def_.testnet_) {
    SelectParams(CBaseChainParams::TESTNET);
    LOG(WARNING) << "using bitcoin testnet3";
  } else {
    SelectParams(CBaseChainParams::MAIN);
  }

  LOG(INFO) << "Block Version: " << std::hex << def_.blockVersion_;
	LOG(INFO) << "Coinbase Info: " << def_.coinbaseInfo_;
  LOG(INFO) << "Payout Address: " << def_.payoutAddr_;

  // check pool payout address
  if (!IsValidDestinationString(def_.payoutAddr_)) {
    LOG(ERROR) << "invalid pool payout address";
    return false;
  }
  poolPayoutAddr_ = DecodeDestination(def_.payoutAddr_);

  // notify policy for RSK
  RskWork::setIsCleanJob(def_.rskNotifyPolicy_ != 0);

  return true;
}

bool JobMakerHandlerBitcoin::initConsumerHandlers(const string &kafkaBrokers, vector<JobMakerConsumerHandler> &handlers) {
  const int32_t consumeLatestN = 20;

  //
  // consumer for RawGbt, offset: latest N messages
  //
  {
    kafkaRawGbtConsumer_ = std::make_shared<KafkaConsumer>(kafkaBrokers.c_str(), def_.rawGbtTopic_.c_str(), 0/* partition */);

    map<string, string> consumerOptions;
    consumerOptions["fetch.wait.max.ms"] = "5";

    if (!kafkaRawGbtConsumer_->setup(RD_KAFKA_OFFSET_TAIL(consumeLatestN), &consumerOptions)) {
      LOG(ERROR) << "kafka consumer rawgbt setup failure";
      return false;
    }
    if (!kafkaRawGbtConsumer_->checkAlive()) {
      LOG(ERROR) << "kafka consumer rawgbt is NOT alive";
      return false;
    }

    handlers.push_back({
      def_.rawGbtTopic_,
      kafkaRawGbtConsumer_,
      std::bind(&JobMakerHandlerBitcoin::processRawGbtMsg, this, std::placeholders::_1)
    });
  }

  //
  // consumer for aux block
  //
  {
    kafkaAuxPowConsumer_ = std::make_shared<KafkaConsumer>(kafkaBrokers.c_str(), def_.auxPowTopic_.c_str(), 0/* partition */);

    map<string, string> consumerOptions;
    consumerOptions["fetch.wait.max.ms"] = "5";
    
    if (!kafkaAuxPowConsumer_->setup(RD_KAFKA_OFFSET_TAIL(1), &consumerOptions)) {
      LOG(ERROR) << "kafka consumer aux pow setup failure";
      return false;
    }
    if (!kafkaAuxPowConsumer_->checkAlive()) {
      LOG(ERROR) << "kafka consumer aux pow is NOT alive";
      return false;
    }

    handlers.push_back({
      def_.auxPowTopic_,
      kafkaAuxPowConsumer_,
      std::bind(&JobMakerHandlerBitcoin::processAuxPowMsg, this, std::placeholders::_1)
    });
  }

  //
  // consumer for RSK messages
  //
  {
    kafkaRskGwConsumer_ = std::make_shared<KafkaConsumer>(kafkaBrokers.c_str(), def_.rskRawGwTopic_.c_str(), 0/* partition */);

    map<string, string> consumerOptions;
    consumerOptions["fetch.wait.max.ms"] = "5";
    if (!kafkaRskGwConsumer_->setup(RD_KAFKA_OFFSET_TAIL(1), &consumerOptions)) {
      LOG(ERROR) << "kafka consumer rawgw block setup failure";
      return false;
    }
    if (!kafkaRskGwConsumer_->checkAlive()) {
      LOG(ERROR) << "kafka consumer rawgw block is NOT alive";
      return false;
    }

    handlers.push_back({
      def_.rskRawGwTopic_,
      kafkaRskGwConsumer_,
      std::bind(&JobMakerHandlerBitcoin::processRskGwMsg, this, std::placeholders::_1)
    });
  }

  // sleep 3 seconds, wait for the latest N messages transfer from broker to client
  sleep(3);

  /* pre-consume some messages for initialization */

  //
  // consume the latest AuxPow message
  //
  {
    rd_kafka_message_t *rkmessage;
    rkmessage = kafkaAuxPowConsumer_->consumer(1000/* timeout ms */);
    if (rkmessage != nullptr && !rkmessage->err) {
      string msg((const char *)rkmessage->payload, rkmessage->len);
      processAuxPowMsg(msg);
      rd_kafka_message_destroy(rkmessage);
    }
  }

  //
  // consume the latest RSK getwork message
  //
  {
    rd_kafka_message_t *rkmessage;
    rkmessage = kafkaRskGwConsumer_->consumer(1000/* timeout ms */);
    if (rkmessage != nullptr && !rkmessage->err) {
      string msg((const char *)rkmessage->payload, rkmessage->len);
      processRskGwMsg(msg);
      rd_kafka_message_destroy(rkmessage);
    }
  }

  //
  // consume the latest N RawGbt messages
  //
  LOG(INFO) << "consume latest rawgbt messages from kafka...";
  for (int32_t i = 0; i < consumeLatestN; i++) {
    rd_kafka_message_t *rkmessage;
    rkmessage = kafkaRawGbtConsumer_->consumer(5000/* timeout ms */);
    if (rkmessage == nullptr || rkmessage->err) {
      break;
    }
    string msg((const char *)rkmessage->payload, rkmessage->len);
    processRawGbtMsg(msg);
    rd_kafka_message_destroy(rkmessage);
  }
  LOG(INFO) << "consume latest rawgbt messages done";

  return true;
}

bool JobMakerHandlerBitcoin::addRawGbt(const string &msg) {
  JsonNode r;
  if (!JsonNode::parse(msg.c_str(), msg.c_str() + msg.size(), r)) {
    LOG(ERROR) << "parse rawgbt message to json fail";
    return false;
  }

  if (r["created_at_ts"].type()         != Utilities::JS::type::Int ||
      r["block_template_base64"].type() != Utilities::JS::type::Str ||
      r["gbthash"].type()               != Utilities::JS::type::Str) {
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
    return false;  // time diff too large, there must be some problems, so ignore it
  }
  if (labs(timeDiff) >= 3) {
    LOG(WARNING) << "rawgbt diff time is too large: " << timeDiff << " seconds";
  }

  const string gbt = DecodeBase64(r["block_template_base64"].str());
  assert(gbt.length() > 64);  // valid gbt string's len at least 64 bytes

  JsonNode nodeGbt;
  if (!JsonNode::parse(gbt.c_str(), gbt.c_str() + gbt.length(), nodeGbt)) {
    LOG(ERROR) << "parse gbt message to json fail";
    return false;
  }
  assert(nodeGbt["result"]["height"].type() == Utilities::JS::type::Int);
  const uint32_t height = nodeGbt["result"]["height"].uint32();

  assert(nodeGbt["result"]["transactions"].type() == Utilities::JS::type::Array);
  const bool isEmptyBlock = nodeGbt["result"]["transactions"].array().size() == 0;

  {
    ScopeLock sl(lock_);

    if (!rawgbtMap_.empty()) {
      const uint64_t bestKey = rawgbtMap_.rbegin()->first;
      const uint32_t bestTime = gbtKeyGetTime(bestKey);
      const uint32_t bestHeight = gbtKeyGetHeight(bestKey);
      const bool     bestIsEmpty = gbtKeyIsEmptyBlock(bestKey);

      // To prevent the job's block height ups and downs
      // when the block height of two bitcoind is not synchronized.
      // The block height downs must past twice the time of stratumJobInterval_
      // without the higher height GBT received.
      if (height < bestHeight && !bestIsEmpty && 
          gbtTime - bestTime < 2 * def_.jobInterval_) {
        LOG(WARNING) << "skip low height GBT. height: " << height
                     << ", best height: " << bestHeight
                     << ", elapsed time after best GBT: " << (gbtTime - bestTime) << "s";
        return false;
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

  return true;
}

bool JobMakerHandlerBitcoin::findBestRawGbt(bool isRskUpdate, string &bestRawGbt) {
  static uint64_t lastSendBestKey = 0;

  ScopeLock sl(lock_);

  // clean expired gbt first
  clearTimeoutGbt();
  clearTimeoutRskGw();

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
  
  // if last job is an empty block job, we need to 
  // send a new non-empty job as quick as possible.
  if (bestHeight == currBestHeight_ && isLastJobEmptyBlock_ && !currentGbtIsEmpty) {
    needUpdateEmptyBlockJob = true;
    LOG(INFO) << "--------update last empty block job--------";
  }

  if (!needUpdateEmptyBlockJob && !isRskUpdate && bestKey == lastSendBestKey) {
    LOG(WARNING) << "bestKey is the same as last one: " << lastSendBestKey;
    return false;
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

    bestRawGbt = rawgbtMap_.rbegin()->second.c_str();
    return true;
  }

  return false;
}

bool JobMakerHandlerBitcoin::isReachTimeout() {
  uint32_t intervalSeconds = def_.jobInterval_;

  if (lastJobSendTime_ + intervalSeconds <= time(nullptr)) {
    return true;
  }
  return false;
}

void JobMakerHandlerBitcoin::clearTimeoutGbt() {
  // Maps (and sets) are sorted, so the first element is the smallest,
  // and the last element is the largest.

  const uint32_t ts_now = time(nullptr);

  for (auto itr = rawgbtMap_.begin(); itr != rawgbtMap_.end(); ) {
    const uint32_t ts  = gbtKeyGetTime(itr->first);
    const bool isEmpty = gbtKeyIsEmptyBlock(itr->first);
    const uint32_t height = gbtKeyGetHeight(itr->first);

    // gbt expired time
    const uint32_t expiredTime = ts + (isEmpty ? def_.emptyGbtLifeTime_ : def_.gbtLifeTime_);

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

void JobMakerHandlerBitcoin::clearTimeoutRskGw() {
  RskWork currentRskWork;
  RskWork previousRskWork;
  {
    ScopeLock sl(rskWorkAccessLock_);
    if (previousRskWork_ == nullptr || currentRskWork_ == nullptr) {
      return;
    }

    const uint32_t ts_now = time(nullptr);
    currentRskWork = *currentRskWork_;
    if(currentRskWork.getCreatedAt() + 120u < ts_now) {
      delete currentRskWork_;
      currentRskWork_ = nullptr;
    }

    previousRskWork = *previousRskWork_;
    if(previousRskWork.getCreatedAt() + 120u < ts_now) {
      delete previousRskWork_;
      previousRskWork_ = nullptr;
    }
  }
}

bool JobMakerHandlerBitcoin::triggerRskUpdate() {
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

  bool notify_flag_update = def_.rskNotifyPolicy_ == 1 && currentRskWork.getNotifyFlag();
  bool different_block_hashUpdate = def_.rskNotifyPolicy_ == 2 && 
                                      (currentRskWork.getBlockHash() != 
                                       previousRskWork.getBlockHash());

  return notify_flag_update || different_block_hashUpdate;
}

bool JobMakerHandlerBitcoin::processRawGbtMsg(const string &msg) {
  return addRawGbt(msg);
}

bool JobMakerHandlerBitcoin::processAuxPowMsg(const string &msg) {
  // set json string
  {
    ScopeLock sl(auxJsonLock_);
    latestAuxPowJson_ = msg;
    DLOG(INFO) << "latestAuxPowJson: " << latestAuxPowJson_;
  }

  // auxpow message will nerver triggered a stratum job updating
  return false;
}

bool JobMakerHandlerBitcoin::processRskGwMsg(const string &rawGetWork) {
  // set json string
  {
    ScopeLock sl(rskWorkAccessLock_);

    RskWork *rskWork = new RskWork();
    if(rskWork->initFromGw(rawGetWork)) {

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
  }

  isRskUpdate_ = triggerRskUpdate();
  return isRskUpdate_;
}

string JobMakerHandlerBitcoin::makeStratumJob(const string &gbt) {
  string latestAuxPowJson;
  {
    ScopeLock sl(auxJsonLock_);
    latestAuxPowJson = latestAuxPowJson_;
  }

  RskWork currentRskBlockJson;
  {
    ScopeLock sl(rskWorkAccessLock_);
    if (currentRskWork_ != nullptr) {
      currentRskBlockJson = *currentRskWork_;
    }
  }

  StratumJob sjob;
  if (!sjob.initFromGbt(gbt.c_str(), def_.coinbaseInfo_,
                                     poolPayoutAddr_,
                                     def_.blockVersion_,
                                     latestAuxPowJson,
                                     currentRskBlockJson)) {
    LOG(ERROR) << "init stratum job message from gbt str fail";
    return "";
  }
  const string jobMsg = sjob.serializeToJson();

  // set last send time
  // TODO: fix Y2K38 issue
  lastJobSendTime_ = (uint32_t)time(nullptr);

  // is an empty block job
  isLastJobEmptyBlock_ = sjob.isEmptyBlock();

  LOG(INFO) << "--------producer stratum job, jobId: " << sjob.jobId_
  << ", height: " << sjob.height_ << "--------";
  LOG(INFO) << "sjob: " << jobMsg;

  return jobMsg;
}

string JobMakerHandlerBitcoin::makeStratumJobMsg() {
  string bestRawGbt;
  if (!findBestRawGbt(isRskUpdate_, bestRawGbt)) {
    return "";
  }
  isRskUpdate_ = false;

  return makeStratumJob(bestRawGbt);
}

uint64_t JobMakerHandlerBitcoin::makeGbtKey(uint32_t gbtTime, bool isEmptyBlock, uint32_t height) {
  assert(height < 0x7FFFFFFFU);

  //
  // gbtKey:
  //  -----------------------------------------------------------------------------------------
  // |               32 bits               |               31 bits              | 1 bit        |
  // | xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx | xxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx | x            |
  // |               gbtTime               |               height               | nonEmptyFlag |
  //  -----------------------------------------------------------------------------------------
  // use nonEmptyFlag (aka: !isEmptyBlock) so the key of a non-empty block
  // will large than the key of an empty block.
  //
  return (((uint64_t)gbtTime) << 32) | (((uint64_t)height) << 1) | ((uint64_t)(!isEmptyBlock));
}

uint32_t JobMakerHandlerBitcoin::gbtKeyGetTime(uint64_t gbtKey) {
  return (uint32_t)(gbtKey >> 32);
}

uint32_t JobMakerHandlerBitcoin::gbtKeyGetHeight(uint64_t gbtKey) {
  return (uint32_t)((gbtKey >> 1) & 0x7FFFFFFFULL);
}

bool JobMakerHandlerBitcoin::gbtKeyIsEmptyBlock(uint64_t gbtKey) {
  return !((bool)(gbtKey & 1ULL));
}
