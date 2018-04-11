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
#include "Stratum.h"

#include <uint256.h>
#include <base58.h>

#include "rsk/RskWork.h"

#include <map>
#include <deque>

class JobMakerHandler;
struct JobMakerDefinition
{
  string payoutAddr;
  string fileLastJobTime;
  string consumerTopic;
  string producerTopic;
  uint32 stratumJobInterval;
  uint32 maxJobDelay;
  shared_ptr<JobMakerHandler> handler;
  bool enabled;
};

class JobMakerHandler
{
public:
  //return true if need to produce stratum job
  virtual void init(const JobMakerDefinition &def) {def_ = def;}
  virtual bool processMsg(const string &msg) = 0;
  virtual string buildStratumJobMsg() = 0;
  virtual ~JobMakerHandler() {}

protected:
  JobMakerDefinition def_;
};

class JobMakerHandlerEth : public JobMakerHandler
{
public:
  virtual bool processMsg(const string &msg);
  virtual string buildStratumJobMsg();
private:
  void clearTimeoutMsg();
  shared_ptr<RskWork> previousRskWork_;
  shared_ptr<RskWork> currentRskWork_;
};

class JobMakerHandlerSia : public JobMakerHandler
{
  string target_;
  string header_;
  uint32 time_;
  bool validate(JsonNode &work);
public:
  JobMakerHandlerSia();
  virtual bool processMsg(const string &msg);
  virtual string buildStratumJobMsg();
};

static vector<JobMakerDefinition> gJobMakerDefinitions; 

class JobMaker {
protected:
  JobMakerDefinition def_;
  atomic<bool> running_;

  string kafkaBrokers_;
  KafkaProducer kafkaProducer_;

  // merged mining for RSK
  KafkaConsumer kafkaRawGwConsumer_;  
  mutex rskWorkAccessLock_;
  RskWork *previousRskWork_;
  RskWork *currentRskWork_;

  
protected:
  void consumeRawGwMsg(rd_kafka_message_t *rkmessage);

  void clearTimeoutGw();

public:
  void runThreadConsumeRawGw();

public:
  JobMaker(const JobMakerDefinition def, const string& brokers);
  virtual ~JobMaker();

  bool init();
  virtual bool initConsumer();
  void stop();
  virtual void run();
};

#endif
