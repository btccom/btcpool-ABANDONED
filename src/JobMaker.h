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
#include "stratum/Stratum.h"
#include "EthConsensus.h"

#include <uint256.h>
#include <base58.h>
#include <pubkey.h>

#include "rsk/RskWork.h"
#include "Zookeeper.h"

#include <map>
#include <deque>
#include <vector>
#include <memory>
#include <functional>

using std::vector;
using std::shared_ptr;

// Consume a kafka message and decide whether to generate a new job.
// Params:
//     msg: kafka message.
// Return:
//     if true, JobMaker will try generate a new job.
using JobMakerMessageProcessor = std::function<bool(const string &msg)>;

struct JobMakerConsumerHandler {
  string kafkaTopic_;
  shared_ptr<KafkaConsumer> kafkaConsumer_;
  JobMakerMessageProcessor messageProcessor_;
};

struct JobMakerDefinition
{
  virtual ~JobMakerDefinition() {}

  string chainType_;
  bool enabled_;

  string jobTopic_;
  uint32 jobInterval_;

  string zookeeperLockPath_;
  string fileLastJobTime_;
};

struct GwJobMakerDefinition : public JobMakerDefinition
{
  virtual ~GwJobMakerDefinition() {}

  string rawGwTopic_;
  uint32 maxJobDelay_;
  uint32 workLifeTime_;
};

struct JobMakerDefinitionEth : public GwJobMakerDefinition {
  virtual ~JobMakerDefinitionEth() {}

  EthConsensus::Chain chain_;
};

struct GbtJobMakerDefinition : public JobMakerDefinition
{
  virtual ~GbtJobMakerDefinition() {}

  bool testnet_;
  
  string payoutAddr_;
  string coinbaseInfo_;
  uint32 blockVersion_;
  
  string rawGbtTopic_;
  string auxPowTopic_;
  string rskRawGwTopic_;

  uint32 maxJobDelay_;
  uint32 gbtLifeTime_;
  uint32 emptyGbtLifeTime_;

  uint32 rskNotifyPolicy_;
};

class JobMakerHandler
{
public:
  virtual ~JobMakerHandler() {}

  virtual bool init(shared_ptr<JobMakerDefinition> def) { def_ = def; return true; }

  virtual bool initConsumerHandlers(const string &kafkaBrokers, vector<JobMakerConsumerHandler> &handlers) = 0;
  virtual string makeStratumJobMsg() = 0;

  // read-only definition
  inline shared_ptr<const JobMakerDefinition> def() { return def_; }

protected:
  shared_ptr<JobMakerDefinition> def_;
};

class GwJobMakerHandler : public JobMakerHandler
{
public:
  virtual ~GwJobMakerHandler() {}

  virtual bool initConsumerHandlers(const string &kafkaBrokers, vector<JobMakerConsumerHandler> &handlers) override;

  //return true if need to produce stratum job
  virtual bool processMsg(const string &msg) = 0;

  // read-only definition
  inline shared_ptr<const GwJobMakerDefinition> def() { return std::dynamic_pointer_cast<const GwJobMakerDefinition>(def_); }
};

class JobMakerHandlerEth : public GwJobMakerHandler
{
public:
  virtual ~JobMakerHandlerEth() {}
  bool processMsg(const string &msg) override;
  string makeStratumJobMsg() override;

  // read-only definition
  inline shared_ptr<const JobMakerDefinitionEth> def() { return std::dynamic_pointer_cast<const JobMakerDefinitionEth>(def_); }

private:
  void clearTimeoutMsg();
  inline uint64_t makeWorkKey(const RskWorkEth &work);

  std::map<uint64_t/* @see makeWorkKey() */, shared_ptr<RskWorkEth>> workMap_;  // sorting works by height + time + hash
};

class JobMakerHandlerSia : public GwJobMakerHandler
{
public:
  JobMakerHandlerSia();
  virtual ~JobMakerHandlerSia() {}
  bool processMsg(const string &msg) override;
  string makeStratumJobMsg() override;
  virtual bool processMsg(JsonNode &j);

protected:
  string target_;
  string header_;
  uint32 time_;
  virtual bool validate(JsonNode &j);

};

class JobMakerHandlerBytom : public JobMakerHandlerSia
{
public:
  virtual ~JobMakerHandlerBytom() {}
  bool processMsg(JsonNode &j) override;
  string makeStratumJobMsg() override;

protected:
  string seed_;
  bool validate(JsonNode &j) override;
};

class JobMakerHandlerBitcoin : public JobMakerHandler
{
  // mining bitcoin blocks
  shared_ptr<KafkaConsumer> kafkaRawGbtConsumer_;
  CTxDestination poolPayoutAddr_;
  uint32_t currBestHeight_;
  uint32_t lastJobSendTime_;
  bool isLastJobEmptyBlock_;
  mutex lock_; // lock when update rawgbtMap_
  std::map<uint64_t/* @see makeGbtKey() */, string> rawgbtMap_;  // sorted gbt by timestamp
  deque<uint256> lastestGbtHash_;

  // merged mining for AuxPow blocks (example: Namecoin, ElastOS)
  shared_ptr<KafkaConsumer> kafkaAuxPowConsumer_;
  mutex auxJsonLock_;
  string latestAuxPowJson_;

  // merged mining for RSK
  shared_ptr<KafkaConsumer> kafkaRskGwConsumer_;
  mutex rskWorkAccessLock_;
  RskWork *previousRskWork_;
  RskWork *currentRskWork_;
  bool isRskUpdate_; // a flag to mark RSK has an update

  bool addRawGbt(const string &msg);
  void clearTimeoutGbt();
  bool isReachTimeout();

  void clearTimeoutRskGw();
  bool triggerRskUpdate();

  // return false if there is no best rawGbt or
  // doesn't need to send a stratum job at current.
  bool findBestRawGbt(bool isRskUpdate, string &bestRawGbt);
  string makeStratumJob(const string &gbt);

  inline uint64_t makeGbtKey(uint32_t gbtTime, bool isEmptyBlock, uint32_t height);
  inline uint32_t gbtKeyGetTime     (uint64_t gbtKey);
  inline uint32_t gbtKeyGetHeight   (uint64_t gbtKey);
  inline bool     gbtKeyIsEmptyBlock(uint64_t gbtKey);

public:
  JobMakerHandlerBitcoin();
  virtual ~JobMakerHandlerBitcoin() {}

  bool init(shared_ptr<JobMakerDefinition> def) override;
  virtual bool initConsumerHandlers(const string &kafkaBrokers, vector<JobMakerConsumerHandler> &handlers) override;
  
  bool processRawGbtMsg(const string &msg);
  bool processAuxPowMsg(const string &msg);
  bool processRskGwMsg(const string &msg);

  virtual string makeStratumJobMsg() override;

  // read-only definition
  inline shared_ptr<const GbtJobMakerDefinition> def() { return std::dynamic_pointer_cast<const GbtJobMakerDefinition>(def_); }
};


class JobMaker {
protected:
  shared_ptr<JobMakerHandler> handler_;
  atomic<bool> running_;

  // coordinate two or more jobmaker (automatic disaster
  // preparedness and recovery) with the zookeeper locker.
  Zookeeper zkLocker_;

  string kafkaBrokers_;
  KafkaProducer kafkaProducer_;
  
  vector<JobMakerConsumerHandler> kafkaConsumerHandlers_;
  vector<shared_ptr<thread>> kafkaConsumerWorkers_;

  time_t lastJobTime_;
  
protected:
  void consumeKafkaMsg(rd_kafka_message_t *rkmessage, JobMakerConsumerHandler &consumerHandler);

public:
  void produceStratumJob();
  void runThreadKafkaConsume(JobMakerConsumerHandler &consumerHandler);

public:
  JobMaker(shared_ptr<JobMakerHandler> handle, const string& kafkaBrokers, const string& zookeeperBrokers);
  virtual ~JobMaker();

  bool init();
  void stop();
  void run();
};

#endif
