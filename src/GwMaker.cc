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
  File: GwMaker.cc
  Purpose: Poll RSK node to get new work and send it to Kafka "RawGw" topic

  @author Martin Medina
  @copyright RSK Labs Ltd.
  @version 1.0 30/03/17 
*/

#include "GwMaker.h"

#include <glog/logging.h>

#include <util.h>
#include <utilstrencodings.h>

#include "Utils.h"
#include "utilities_js.hpp"
#include "hash.h"

GwMaker::GwMaker(const string &rskdRpcAddr, const string &rskdRpcUserpass,
                   const string &kafkaBrokers, uint32_t kRpcCallInterval)
: running_(true), rskdRpcAddr_(rskdRpcAddr),
rskdRpcUserpass_(rskdRpcUserpass), kRpcCallInterval_(kRpcCallInterval),
kafkaBrokers_(kafkaBrokers),
kafkaProducer_(kafkaBrokers_.c_str(), KAFKA_TOPIC_RAWGW, 0/* partition */)
{
}

GwMaker::~GwMaker() {}

bool GwMaker::init() {
  map<string, string> options;
  // set to 1 (0 is an illegal value here), deliver msg as soon as possible.
  options["queue.buffering.max.ms"] = "1";
  if (!kafkaProducer_.setup(&options)) {
    LOG(ERROR) << "kafka producer setup failure";
    return false;
  }

  // setup kafka and check if it's alive
  if (!kafkaProducer_.checkAlive()) {
    LOG(ERROR) << "kafka is NOT alive";
    return false;
  }

  // TODO: check rskd is alive in a similar way as done for btcd

  return true;
}

void GwMaker::stop() {
  if (!running_) {
    return;
  }
  running_ = false;
  LOG(INFO) << "stop GwMaker";
}

void GwMaker::kafkaProduceMsg(const void *payload, size_t len) {
  kafkaProducer_.produce(payload, len);
}

bool GwMaker::rskdRpcGw(string &response) {
  string request = "{\"jsonrpc\": \"2.0\", \"method\": \"mnr_getWork\", \"params\": [], \"id\": 1}";

  bool res = bitcoindRpcCall(rskdRpcAddr_.c_str(), rskdRpcUserpass_.c_str(), request.c_str(), response);

  if (!res) {
    LOG(ERROR) << "rskd RPC failure";
    return false;
  }
  return true;
}

string GwMaker::makeRawGwMsg() {
  string gw;
  if (!rskdRpcGw(gw)) {
    return "";
  }

  JsonNode r;
  if (!JsonNode::parse(gw.c_str(), gw.c_str() + gw.length(), r)) {
    LOG(ERROR) << "decode gw failure: " << gw;
    return "";
  }

  // check fields
  if (r["result"].type()                                != Utilities::JS::type::Obj ||
      r["result"]["parentBlockHash"].type()             != Utilities::JS::type::Str ||
      r["result"]["blockHashForMergedMining"].type()    != Utilities::JS::type::Str ||
      r["result"]["target"].type()                      != Utilities::JS::type::Str ||
      r["result"]["feesPaidToMiner"].type()             != Utilities::JS::type::Str ||
      r["result"]["notify"].type()                      != Utilities::JS::type::Bool) {
    LOG(ERROR) << "gw check fields failure";
    return "";
  }
  const uint256 gwHash = Hash(gw.begin(), gw.end());

  LOG(INFO) << ", parent block hash: "    << r["result"]["parentBlockHash"].str()
  << ", block hash for merge mining: " << r["result"]["blockHashForMergedMining"].str()
  << ", target: "               << r["result"]["target"].str()
  << ", fees paid to miner: "   << r["result"]["feesPaidToMiner"].str()
  << ", notify: " << r["result"]["notify"].boolean()
  << ", gwhash: " << gwHash.ToString();

  return Strings::Format("{\"created_at_ts\":%u,"
                         "\"rskdRpcAddress\":\"%s\","
                         "\"rskdRpcUserPwd\":\"%s\","
                         "\"target\":\"%s\","
                         "\"parentBlockHash\":\"%s\","
                         "\"blockHashForMergedMining\":\"%s\","
                         "\"feesPaidToMiner\":\"%s\","
                         "\"notify\":\"%s\"}",
                         (uint32_t)time(nullptr), 
                         rskdRpcAddr_.c_str(), 
                         rskdRpcUserpass_.c_str(),
                         r["result"]["target"].str().c_str(), 
                         r["result"]["parentBlockHash"].str().c_str(),
                         r["result"]["blockHashForMergedMining"].str().c_str(),
                         r["result"]["feesPaidToMiner"].str().c_str(),
                         r["result"]["notify"].boolean() ? "true" : "false");
}

void GwMaker::submitRawGwMsg() {

  const string rawGwMsg = makeRawGwMsg();
  if (rawGwMsg.length() == 0) {
    LOG(ERROR) << "get rawGw failure";
    return;
  }

  // submit to Kafka
  LOG(INFO) << "submit to Kafka, msg len: " << rawGwMsg.length();
  kafkaProduceMsg(rawGwMsg.c_str(), rawGwMsg.length());
}

void GwMaker::run() {

  while (running_) {
    sleep(kRpcCallInterval_);
    submitRawGwMsg();
  }
}