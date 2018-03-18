/*
 The MIT License (MIT)

 Copyright (C) 2017 RSK Labs Ltd.

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

/**
  File: GwMaker.h
  Purpose: Poll RSK node to get new work and send it to Kafka "RawGw" topic

  @author Martin Medina
  @copyright RSK Labs Ltd.
  @version 1.0 30/03/17 
*/

#ifndef GW_MAKER_H_
#define GW_MAKER_H_

#include "Common.h"
#include "Kafka.h"
#include "utilities_js.hpp"

using GwHandler = string (*) (const string&);

struct GwDefinition {
  const string      url;
  const string      userpwd; 
  const string      reqData; 
  const string      agent;
  const string      topic;
  const string      broker;
  const uint32      pullingInterval;
  GwHandler         handler;
  bool              enable;
};

static vector<GwDefinition> gwDefiniitons_;

class GwMaker {
  atomic<bool> running_;

protected:
  string rskdRpcAddr_;
  string rskdRpcUserpass_;

private:
  uint32_t kRpcCallInterval_;

  string kafkaBrokers_;
  KafkaProducer kafkaProducer_;

  bool rskdRpcGw(string &resp);
  string makeRawGwMsg();

  void submitRawGwMsg();

  void kafkaProduceMsg(const void *payload, size_t len);
  virtual string constructRequest();
  virtual bool checkFields(JsonNode &r);
  virtual string constructRawMsg(string &gw, JsonNode &r);
public:
  GwMaker(const string &rskdRpcAddr, const string &rskdRpcUserpass,
           const string &kafkaBrokers, uint32_t kRpcCallInterval);
  virtual ~GwMaker();

  bool init();
  void stop();
  void run();
};

class GwMakerEth : public GwMaker {
  virtual string constructRequest();
  virtual bool checkFields(JsonNode &r);
  virtual string constructRawMsg(string &gw, JsonNode &r);
public:
  GwMakerEth(const string &rskdRpcAddr, const string &rskdRpcUserpass,
           const string &kafkaBrokers, uint32_t kRpcCallInterval);
};

#endif
