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
#ifndef BLOCK_MAKER_H_
#define BLOCK_MAKER_H_

#include "Common.h"
#include "Kafka.h"
#include "MySQLConnection.h"
#include "Stratum.h"

#include <vector>

struct NodeDefinition {
  string rpcAddr_;
  string rpcUserPwd_;
  mutable bool parity_;
};

struct BlockMakerDefinition {
  string chainType_;
  bool enabled_;
  vector<NodeDefinition> nodes;
  string solvedShareTopic_;
  string foundAuxBlockTable_;
  string auxChainName_;

  virtual ~BlockMakerDefinition() {}
};

struct BlockMakerDefinitionBitcoin : public BlockMakerDefinition {
  string rawGbtTopic_;
  string stratumJobTopic_;
  string auxPowSolvedShareTopic_; // merged mining solved share topic
  string rskSolvedShareTopic_;

  virtual ~BlockMakerDefinitionBitcoin() {}
};

////////////////////////////////// BlockMaker //////////////////////////////////
class BlockMaker {
protected:
  shared_ptr<BlockMakerDefinition> def_;
  atomic<bool> running_;

  KafkaSimpleConsumer kafkaConsumerSolvedShare_;

  MysqlConnectInfo poolDB_; // save blocks to table.found_blocks

  void runThreadConsumeSolvedShare();
  void consumeSolvedShare(rd_kafka_message_t *rkmessage);
  virtual void processSolvedShare(rd_kafka_message_t *rkmessage) = 0;

public:
  BlockMaker(
      shared_ptr<BlockMakerDefinition> def,
      const char *kafkaBrokers,
      const MysqlConnectInfo &poolDB);
  virtual ~BlockMaker();

  // read-only definition
  inline shared_ptr<const BlockMakerDefinition> def() { return def_; }

  virtual bool init();
  virtual void stop();
  virtual void run();
};

#endif
