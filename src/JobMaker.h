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
#ifndef JOB_MAKER_H_
#define JOB_MAKER_H_

#include "Common.h"
#include "Kafka.h"

#include "Zookeeper.h"
#include "Utils.h"

#include <deque>
#include <vector>
#include <memory>

using std::vector;
using std::shared_ptr;

class SubPoolInfo;
class JobMaker;

// Consume a kafka message and decide whether to generate a new job.
// Params:
//     msg: kafka message.
// Return:
//     if true, JobMaker will try generate a new job.
using JobMakerMessageProcessor =
    std::function<bool(const string &, const string &)>;

struct JobMakerConsumerHandler {
  shared_ptr<KafkaConsumer> kafkaConsumer_;
  JobMakerMessageProcessor messageProcessor_;
};

struct JobMakerDefinition {
  virtual ~JobMakerDefinition() {}

  string chainType_;
  bool enabled_;

  string jobTopic_;
  uint32_t jobInterval_;
  uint32_t serverId_;

  string zookeeperLockPath_;
  string fileLastJobTime_;
};

struct GwJobMakerDefinition : public JobMakerDefinition {
  virtual ~GwJobMakerDefinition() {}

  string rawGwTopic_;
  uint32_t maxJobDelay_;
  uint32_t workLifeTime_;
};

class JobMakerHandler {
public:
  virtual ~JobMakerHandler() {}
  void setParent(shared_ptr<JobMaker> jobmaker);

  virtual bool init(shared_ptr<JobMakerDefinition> def) {
    def_ = def;
    gen_ = std::make_unique<IdGenerator>(def->serverId_);
    return true;
  }

  virtual bool initConsumerHandlers(
      const string &kafkaBrokers,
      vector<JobMakerConsumerHandler> &handlers) = 0;
  virtual string makeStratumJobMsg() = 0;

  // read-only definition
  inline shared_ptr<const JobMakerDefinition> def() { return def_; }
  JobMakerConsumerHandler createConsumerHandler(
      const string &kafkaBrokers,
      const string &topic,
      int64_t offset,
      vector<pair<string, string>> consumerOptions,
      JobMakerMessageProcessor messageProcessor);

  void setServerId(uint8_t id);

protected:
  shared_ptr<JobMaker> jobmaker_;
  shared_ptr<JobMakerDefinition> def_;
  unique_ptr<IdGenerator> gen_;
};

class GwJobMakerHandler : public JobMakerHandler {
public:
  virtual ~GwJobMakerHandler() {}

  virtual bool initConsumerHandlers(
      const string &kafkaBrokers,
      vector<JobMakerConsumerHandler> &handlers) override;

  // return true if need to produce stratum job
  virtual bool processMsg(const string &msg) = 0;

  // read-only definition
  inline shared_ptr<const GwJobMakerDefinition> def() {
    return std::dynamic_pointer_cast<const GwJobMakerDefinition>(def_);
  }
};

class JobMaker {
protected:
  shared_ptr<JobMakerHandler> handler_;
  atomic<bool> running_;

  // assign server id from zookeeper
  shared_ptr<Zookeeper> zk_;
  string zkBrokers_;

  string kafkaBrokers_;
  KafkaProducer kafkaProducer_;

  vector<JobMakerConsumerHandler> kafkaConsumerHandlers_;
  vector<shared_ptr<thread>> kafkaConsumerWorkers_;

  time_t lastJobTime_;

protected:
  bool consumeKafkaMsg(
      rd_kafka_message_t *rkmessage, JobMakerConsumerHandler &consumerHandler);

public:
  void produceStratumJob();
  void runThreadKafkaConsume(JobMakerConsumerHandler &consumerHandler);

public:
  JobMaker(
      shared_ptr<JobMakerHandler> handle,
      const string &kafkaBrokers,
      const string &zookeeperBrokers);
  virtual ~JobMaker();

  void initZk();
  bool init();
  void stop();
  void run();

  inline bool running() { return running_; }
  inline shared_ptr<Zookeeper> zk() { return zk_; }

private:
  bool setupKafkaProducer();
};

#endif
