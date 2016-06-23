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
#ifndef GBT_MAKER_H_
#define GBT_MAKER_H_

#include "Common.h"
#include "Kafka.h"

#include "zmq.hpp"


class GbtMaker {
  atomic<bool> running_;
  mutex lock_;

  zmq::context_t zmqContext_;
  string zmqBitcoindAddr_;

  string bitcoindRpcAddr_;
  string bitcoindRpcUserpass_;

  atomic<uint32_t> lastGbtMakeTime_;
  uint32_t kRpcCallInterval_;

  rd_kafka_conf_t *kafkaConf_;
  rd_kafka_t *kafkaProducer_;
  rd_kafka_topic_t *kafkaTopicRawgbt_;
  string kafkaBrokers_;

  bool bitcoindRpcGBT(string &resp);
  string makeRawGbtMsg();

  void submitRawGbtMsg(bool checkTime);
  void threadListenBitcoind();

  bool setupKafka();
  void kafkaProduceMsg(const void *payload, size_t len);

public:
  GbtMaker(const string &zmqBitcoindAddr,
           const string &bitcoindRpcAddr, const string &bitcoindRpcUserpass,
           const string &kafkaBrokers, uint32_t kRpcCallInterval);
  ~GbtMaker();

  bool init();
  void stop();
  void run();
};

#endif
