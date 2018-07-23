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


///////////////////////////////////  JobMaker  /////////////////////////////////
JobMaker::JobMaker(shared_ptr<JobMakerHandler> handler,
                   const string &kafkaBrokers,
                   const string& zookeeperBrokers) : handler_(handler),
                                            running_(true),
                                            zkLocker_(zookeeperBrokers.c_str()),
                                            kafkaBrokers_(kafkaBrokers),
                                            kafkaProducer_(kafkaBrokers.c_str(), handler->def()->jobTopic_.c_str(), RD_KAFKA_PARTITION_UA)
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
    if (handler_->def()->zookeeperLockPath_.empty()) {
      LOG(ERROR) << "zookeeper lock path is empty!";
      return false;
    }
    zkLocker_.getLock(handler_->def()->zookeeperLockPath_.c_str());
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
    LOG(INFO) << "new " << handler_->def()->jobTopic_ << " job: " << jobMsg;
    kafkaProducer_.produce(jobMsg.data(), jobMsg.size());
  }

  lastJobTime_ = time(nullptr);
  
  // save send timestamp to file, for monitor system
  if (!handler_->def()->fileLastJobTime_.empty()) {
    // TODO: fix Y2K38 issue
  	writeTime2File(handler_->def()->fileLastJobTime_.c_str(), (uint32_t)lastJobTime_);
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

    if (time(nullptr) - lastJobTime_ > handler_->def()->jobInterval_) {
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
    /* kafkaTopic_ = */       def()->rawGwTopic_,
    /* kafkaConsumer_ = */    std::make_shared<KafkaConsumer>(kafkaBrokers.c_str(), def()->rawGwTopic_.c_str(), 0/* partition */),
    /* messageProcessor_ = */ std::bind(&GwJobMakerHandler::processMsg, this, std::placeholders::_1)
  };

  // init kafka consumer
  {
    map<string, string> consumerOptions;
    consumerOptions["fetch.wait.max.ms"] = "5";
    if (!handler.kafkaConsumer_->setup(RD_KAFKA_OFFSET_TAIL(1), &consumerOptions)) {
      LOG(ERROR) << "kafka consumer " << def()->rawGwTopic_ << " setup failure";
      return false;
    }
    if (!handler.kafkaConsumer_->checkAlive()) {
      LOG(FATAL) << "kafka consumer " << def()->rawGwTopic_ << " is NOT alive";
      return false;
    }
  }

  handlers.push_back(handler);
  return true;
}