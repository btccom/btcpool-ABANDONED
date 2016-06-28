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

#include "bitcoin/uint256.h"
#include "bitcoin/base58.h"

#include <map>


//
// Stratum Job
//
// https://slushpool.com/help/#!/manual/stratum-protocol
//
// "mining.notify"
//
// job_id   - ID of the job. Use this ID while submitting share generated
//             from this job.
// prevhash - Hash of previous block.
// coinb1   - Initial part of coinbase transaction.
// coinb2   - Final part of coinbase transaction.
// merkle_branch - List of hashes, will be used for calculation of merkle root.
//                 This is not a list of all transactions, it only contains
//                 prepared hashes of steps of merkle tree algorithm.
// version    - Bitcoin block version.
// nbits      - Encoded current network difficulty
// ntime      - Current ntime/
// clean_jobs - When true, server indicates that submitting shares from previous
//              jobs don't have a sense and such shares will be rejected. When
//              this flag is set, miner should also drop all previous jobs,
//              so job_ids can be eventually rotated.
//
//
class StratumJobMsg {
public:
  // jobID: timestamp + gbtHash, hex string, we need to make sure jobID is
  // unique in a some time, jobID can convert to uint64_t
  string jobID_;
  string gbtHash_;        // gbt hash id
  string prevHashBeStr_;  // little-endian hex, memory's order
  int32_t height_;
  string coinbase1_;
  string coinbase2_;
  string merkleBranch_;

  int32_t nVersion_;
  uint32_t nBits_;
  uint32_t nTime_;
  uint32_t minTime_;
  int64_t  coinbaseValue_;

public:
  StratumJobMsg();
  string toString() const;
  bool parse(const char *s);
  bool initFromGbt(const char *gbt, const string &poolCoinbaseInfo,
                   const CBitcoinAddress &poolPayoutAddr);
};


class JobMaker {
  atomic<bool> running_;
  mutex lock_;

  string kafkaBrokers_;
  KafkaConsumer kafkaConsumer_;
  KafkaProducer kafkaProducer_;

  uint32_t currBestHeight_;
  uint32_t lastJobSendTime_;
  uint32_t stratumJobInterval_;

  string poolCoinbaseInfo_;
  CBitcoinAddress poolPayoutAddr_;

  uint32_t kGbtLifeTime_;
  std::map<uint64_t/* timestamp + height */, string> rawgbtMap_;  // sorted gbt by timestamp

  void consumeRawGbtMsg(rd_kafka_message_t *rkmessage, bool needToSend);
  void addRawgbt(const char *str, size_t len);

  void clearTimeoutGbt();
  void findNewBestHeight(std::map<uint64_t/* height + ts */, const char *> *gbtByHeight);
  bool isReachTimeout();
  void sendStratumJob(const char *gbt);

  void checkAndSendStratumJob();

public:
  JobMaker(const string &kafkaBrokers, uint32_t stratumJobInterval,
           const string &payoutAddr, uint32_t gbtLifeTime);
  ~JobMaker();

  bool init();
  void stop();
  void run();
};

#endif
