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
#include "Utils.h"

///////////////////////////////////  JobMakerHandler
////////////////////////////////////
void JobMakerHandler::setParent(shared_ptr<JobMaker> jobmaker) {
  jobmaker_ = jobmaker;
}

///////////////////////////////////  JobMaker  /////////////////////////////////
JobMaker::JobMaker(
    shared_ptr<JobMakerHandler> handler,
    const string &kafkaBrokers,
    const string &zookeeperBrokers)
  : handler_(handler)
  , running_(true)
  , zkBrokers_(zookeeperBrokers)
  , kafkaBrokers_(kafkaBrokers)
  , kafkaProducer_(
        kafkaBrokers.c_str(),
        handler->def()->jobTopic_.c_str(),
        RD_KAFKA_PARTITION_UA) {
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

bool JobMaker::setupKafkaProducer() {
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
  return true;
}

void JobMaker::initZk() {
  if (zk_ == nullptr) {
    zk_ = std::make_shared<Zookeeper>(zkBrokers_);
  }
}

bool JobMaker::init() {
  if (handler_->def()->serverId_ == 0) {
    // assign id from zookeeper
    try {
      if (handler_->def()->zookeeperLockPath_.empty()) {
        LOG(ERROR) << "zookeeper lock path is empty!";
        return false;
      }
      initZk();
      handler_->setServerId(
          zk_->getUniqIdUint8(handler_->def()->zookeeperLockPath_));

    } catch (const ZookeeperException &zooex) {
      LOG(ERROR) << zooex.what();
      return false;
    }
  }

  if (!setupKafkaProducer())
    return false;

  /* setup kafka consumers */
  if (!handler_->initConsumerHandlers(kafkaBrokers_, kafkaConsumerHandlers_)) {
    return false;
  }

  return true;
}

bool JobMaker::consumeKafkaMsg(
    rd_kafka_message_t *rkmessage, JobMakerConsumerHandler &consumerHandler) {
  // check error
  string topic = rd_kafka_topic_name(rkmessage->rkt);
  if (rkmessage->err) {
    if (rkmessage->err == RD_KAFKA_RESP_ERR__PARTITION_EOF) {
      // Reached the end of the topic+partition queue on the broker.
      return false;
    }

    LOG(ERROR) << "consume error for topic " << topic.c_str() << "["
               << rkmessage->partition << "] offset " << rkmessage->offset
               << ": " << rd_kafka_message_errstr(rkmessage);

    if (rkmessage->err == RD_KAFKA_RESP_ERR__UNKNOWN_PARTITION ||
        rkmessage->err == RD_KAFKA_RESP_ERR__UNKNOWN_TOPIC) {
      LOG(FATAL) << "consume fatal";
      stop();
    }
    return false;
  }

  // set json string
  LOG(INFO) << "received " << topic.c_str()
            << " message len: " << rkmessage->len;

  string msg((const char *)rkmessage->payload, rkmessage->len);

  if (consumerHandler.messageProcessor_(msg, topic)) {
    LOG(INFO) << "handleMsg returns true, new stratum job";
    produceStratumJob();
    return true;
  } else {
    return false;
  }
}

void JobMaker::produceStratumJob() {
  const string jobMsg = handler_->makeStratumJobMsg();

  if (!jobMsg.empty()) {
    LOG(INFO) << "new " << handler_->def()->jobTopic_ << " job: " << jobMsg;
    kafkaProducer_.produce(jobMsg.data(), jobMsg.size());
  }

  lastJobTime_ = time(nullptr);

  // save send timestamp to file, for monitor system
  if (!handler_->def()->fileLastJobTime_.empty()) {
    // TODO: fix Y2K38 issue
    writeTime2File(
        handler_->def()->fileLastJobTime_.c_str(), (uint32_t)lastJobTime_);
  }
}

void JobMaker::runThreadKafkaConsume(JobMakerConsumerHandler &consumerHandler) {
  int32_t timeoutMs = handler_->def()->jobInterval_ * 1000;
  bool jobUpdated = false;

  while (running_) {
    rd_kafka_message_t *rkmessage =
        consumerHandler.kafkaConsumer_->consumer(timeoutMs);
    if (rkmessage) {
      jobUpdated = consumeKafkaMsg(rkmessage, consumerHandler);

      /* Return message to rdkafka */
      rd_kafka_message_destroy(rkmessage);

      // Don't add any sleep() here.
      // Kafka will not skip any message during your sleep(), you will received
      // all messages from your beginning offset to the latest in any case.
      // So sleep() will cause unexpected delay before consumer a new message.
      // If the producer's speed is faster than the sleep() here, the
      // consumption will be delayed permanently and the latest message will
      // never be received.

      // At the same time, there is not a busy waiting.
      // KafkaConsumer::consumer(timeoutMs) will return after `timeoutMs`
      // millisecond if no new messages. You can increase `timeoutMs` if you
      // want.
    }

    uint32_t timeDiff;
    if (rkmessage == nullptr ||
        (!jobUpdated &&
         (timeDiff = time(nullptr) - lastJobTime_) >
             handler_->def()->jobInterval_)) {
      produceStratumJob();
      jobUpdated = true;
    }

    timeoutMs =
        (handler_->def()->jobInterval_ - (jobUpdated ? 0 : timeDiff)) * 1000;
  }
}

void JobMaker::run() {

  // running consumer threads
  for (JobMakerConsumerHandler &consumerhandler : kafkaConsumerHandlers_) {
    kafkaConsumerWorkers_.push_back(std::make_shared<thread>(
        std::bind(&JobMaker::runThreadKafkaConsume, this, consumerhandler)));
  }

  // wait consumer threads exit
  for (auto pWorker : kafkaConsumerWorkers_) {
    if (pWorker->joinable()) {
      LOG(INFO) << "wait for worker " << pWorker->get_id() << " exiting";
      pWorker->join();
      LOG(INFO) << "worker exited";
    }
  }
}

JobMakerConsumerHandler JobMakerHandler::createConsumerHandler(
    const string &kafkaBrokers,
    const string &topic,
    int64_t offset,
    vector<pair<string, string>> consumerOptions,
    JobMakerMessageProcessor messageProcessor) {
  std::map<string, string> usedConsumerOptions;
  //  default
  usedConsumerOptions["fetch.wait.max.ms"] = "5";
  //  passed settings
  for (auto &option : consumerOptions) {
    usedConsumerOptions[option.first] = option.second;
  }

  JobMakerConsumerHandler result;
  auto consumer = std::make_shared<KafkaSimpleConsumer>(
      kafkaBrokers.c_str(), topic.c_str(), 0);
  if (!consumer->setup(RD_KAFKA_OFFSET_TAIL(offset), &usedConsumerOptions)) {
    LOG(ERROR) << "kafka consumer " << topic << " setup failure";
  } else if (!consumer->checkAlive()) {
    LOG(FATAL) << "kafka consumer " << topic << " is NOT alive";
  } else {
    result.kafkaConsumer_ = consumer;
    result.messageProcessor_ = messageProcessor;
  }

  return result;
}

void JobMakerHandler::setServerId(uint8_t id) {
  def_->serverId_ = id;
  gen_ = std::make_unique<IdGenerator>(id);
}

////////////////////////////////GwJobMakerHandler//////////////////////////////////
bool GwJobMakerHandler::initConsumerHandlers(
    const string &kafkaBrokers, vector<JobMakerConsumerHandler> &handlers) {
  {
    auto messageProcessor =
        std::bind(&GwJobMakerHandler::processMsg, this, std::placeholders::_1);
    auto handler = createConsumerHandler(
        kafkaBrokers, def()->rawGwTopic_, 1, {}, messageProcessor);
    if (handler.kafkaConsumer_ == nullptr)
      return false;
    handlers.push_back(handler);
  }
  return true;
}
