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
  uint32 consumerInterval;
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
  mutex lock_;

  string kafkaBrokers_;
  KafkaProducer kafkaProducer_;
  //KafkaConsumer kafkaRawGbtConsumer_;

  //KafkaConsumer kafkaNmcAuxConsumer_;  // merged mining for namecoin
  mutex auxJsonlock_;
  string latestNmcAuxBlockJson_;

  // merged mining for RSK
  KafkaConsumer kafkaRawGwConsumer_;  
  mutex rskWorkAccessLock_;
  RskWork *previousRskWork_;
  RskWork *currentRskWork_;
  uint32_t rskNotifyPolicy_;

  uint32_t currBestHeight_;
  uint32_t lastJobSendTime_;
  bool isLastJobEmptyBlock_;
  uint32_t stratumJobInterval_;

  string poolCoinbaseInfo_;
  string poolPayoutAddrStr_;
  CTxDestination poolPayoutAddr_;

  uint32_t kGbtLifeTime_;
  uint32_t kEmptyGbtLifeTime_;
  string fileLastJobTime_;

  std::map<uint64_t/* @see makeGbtKey() */, string> rawgbtMap_;  // sorted gbt by timestamp

  deque<uint256> lastestGbtHash_;
  uint32_t blockVersion_;

  thread threadConsumeNmcAuxBlock_;
  thread threadConsumeRskRawGw_;
  
protected:
  void consumeNmcAuxBlockMsg(rd_kafka_message_t *rkmessage);
  void consumeRawGwMsg(rd_kafka_message_t *rkmessage);
  void consumeRawGbtMsg(rd_kafka_message_t *rkmessage, bool needToSend);
  void addRawgbt(const char *str, size_t len);

  void clearTimeoutGbt();
  bool isReachTimeout();
  void sendStratumJob(const char *gbt);

  void clearTimeoutGw();
  virtual bool triggerRskUpdate();
  virtual void checkAndSendStratumJob(bool isRskUpdate);
  void runThreadConsumeNmcAuxBlock();

public:
  void runThreadConsumeRawGw();

private:
  inline uint64_t makeGbtKey(uint32_t gbtTime, bool isEmptyBlock, uint32_t height);
  inline uint32_t gbtKeyGetTime(uint64_t gbtKey);
  inline uint32_t gbtKeyGetHeight(uint64_t gbtKey);
  inline bool gbtKeyIsEmptyBlock(uint64_t gbtKey);
  virtual RskWork* createWork();
public:
  // JobMaker(const string &kafkaBrokers, uint32_t stratumJobInterval,
  //          const string &payoutAddr, uint32_t gbtLifeTime,
  //          uint32_t emptyGbtLifeTime, const string &fileLastJobTime,
  //          uint32_t rskNotifyPolicy, uint32_t blockVersion,
	// 				const string &poolCoinbaseInfo);

  JobMaker(const JobMakerDefinition def, const string& brokers);
  virtual ~JobMaker();

  bool init();
  virtual bool initConsumer();
  void stop();
  virtual void run();
};

// number: QUANTITY - the block number. null when its pending block.
// hash: DATA, 32 Bytes - hash of the block. null when its pending block.
// parentHash: DATA, 32 Bytes - hash of the parent block.
// nonce: DATA, 8 Bytes - hash of the generated proof-of-work. null when its pending block.
// sha3Uncles: DATA, 32 Bytes - SHA3 of the uncles data in the block.
// logsBloom: DATA, 256 Bytes - the bloom filter for the logs of the block. null when its pending block.
// transactionsRoot: DATA, 32 Bytes - the root of the transaction trie of the block.
// stateRoot: DATA, 32 Bytes - the root of the final state trie of the block.
// receiptsRoot: DATA, 32 Bytes - the root of the receipts trie of the block.
// miner: DATA, 20 Bytes - the address of the beneficiary to whom the mining rewards were given.
// difficulty: QUANTITY - integer of the difficulty for this block.
// totalDifficulty: QUANTITY - integer of the total difficulty of the chain until this block.
// extraData: DATA - the "extra data" field of this block.
// size: QUANTITY - integer the size of this block in bytes.
// gasLimit: QUANTITY - the maximum gas allowed in this block.
// gasUsed: QUANTITY - the total used gas by all transactions in this block.
// timestamp: QUANTITY - the unix timestamp for when the block was collated.
// transactions: Array - Array of transaction objects, or 32 Bytes transaction hashes depending on the last given parameter.
// uncles: Array - Array of uncle hashes.

// class BlockHeaderEth
// {
//   public:
//     uint64_t number_;
//     uint64_t difficulty_;
//     uint64_t totalDifficulty_;
//     uint32_t gasLimit_;
//     uint32_t gasUsed_;
//     uint32_t timestamp_;
//     uint32_t size_;
//     string extraData_;
//     string hash_;
//     string logsBloom_;
//     string miner_;
//     string mixHash_;
//     string nonce_;
//     string parentHash_;
//     string receiptsRoot_;
//     string sha3Uncles_;
//     string stateRoot_;
//     string transactionsRoot_;
// };

// class JobMakerEth : public JobMaker
// {
//   virtual RskWork* createWork();
//   virtual bool triggerRskUpdate();
//   virtual void checkAndSendStratumJob(bool isRskUpdate);
//   void sendGwStratumJob();
// public:
//   JobMakerEth(const string &kafkaBrokers, uint32_t stratumJobInterval,
//               const string &payoutAddr, const string &fileLastJobTime,
//               uint32_t rskNotifyPolicy, uint32_t blockVersion,
//               const string &poolCoinbaseInfo);
//   virtual bool initConsumer();
//   virtual void run();            
// };
#endif
