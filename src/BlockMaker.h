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
#include "Stratum.h"

#include <deque>
#include <vector>
#include <unordered_map>

#include "bitcoin/uint256.h"
#include "bitcoin/base58.h"


class BlockMaker {
  atomic<bool> running_;
  mutex lock_;

  size_t kMaxRawGbtNum_;  // how many rawgbt should we keep
  // key: gbthash
  std::deque<string> rawGbtQ_;
  // key: gbthash, value: block template json
  std::unordered_map<string, string> rawGbtMap_;

  KafkaConsumer kafkaConsumerRawGbt_;
  KafkaConsumer kafkaConsumerSovledShare_;

  // submit new block to bitcoind
  // pair: <RpcAddress, RpcUserpass>
  std::vector<std::pair<string, string>> bitcoindRpcUri_;

  thread threadConsumeRawGbt_;
  thread threadConsumeSovledShare_;

  void runThreadConsumeRawGbt();
  void runThreadConsumeSovledShare();

  void consumeRawGbt     (rd_kafka_message_t *rkmessage);
  void consumeSovledShare(rd_kafka_message_t *rkmessage);

public:
  BlockMaker(const char *kafkaBrokers);
  ~BlockMaker();

  void addBitcoind(const string &rpcAddress, const string &rpcUserpass);

  bool init();
  void stop();
  void run();
};

#endif
