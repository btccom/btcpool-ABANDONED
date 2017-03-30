/*
 The MIT License (MIT)

 Copyright (c) [2017] [RSK.CO]

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
#include "GwMaker.h"

#include <glog/logging.h>

#include "bitcoin/util.h"
#include "bitcoin/utilstrencodings.h"

#include "Utils.h"
#include "utilities_js.hpp"

///////////////////////////////////  GwMaker  /////////////////////////////////
GwMaker::GwMaker(const string &rskdRpcAddr, const string &rskdRpcUserpass,
                   const string &kafkaBrokers, uint32_t kRpcCallInterval)
: running_(true), rskdRpcAddr_(rskdRpcAddr),
rskdRpcUserpass_(rskdRpcUserpass),lastGwMakeTime_(0), kRpcCallInterval_(kRpcCallInterval),
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

  // check rskd
//  {
//    string response;
//    string request = "{\"jsonrpc\":\"1.0\",\"id\":\"1\",\"method\":\"getinfo\",\"params\":[]}";
//    bool res = rskdRpcCall(rskdRpcAddr_.c_str(), rskdRpcUserpass_.c_str(),
//                               request.c_str(), response);
//    if (!res) {
//      LOG(ERROR) << "bitcoind rpc call failure";
//      return false;
//    }
//    LOG(INFO) << "bitcoind getinfo: " << response;
//
//    JsonNode r;
//    if (!JsonNode::parse(response.c_str(),
//                         response.c_str() + response.length(), r)) {
//      LOG(ERROR) << "decode gbt failure";
//      return false;
//    }
//
//    // check fields
//    if (r["result"].type() != Utilities::JS::type::Obj ||
//        r["result"]["connections"].type() != Utilities::JS::type::Int ||
//        r["result"]["blocks"].type()      != Utilities::JS::type::Int) {
//      LOG(ERROR) << "getinfo missing some fields";
//      return false;
//    }
//    if (r["result"]["connections"].int32() <= 0) {
//      LOG(ERROR) << "bitcoind connections is zero";
//      return false;
//    }
//  }

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
  bool res = rskdRpcCall(rskdRpcAddr_.c_str(), rskdRpcUserpass_.c_str(),
                             request.c_str(), response);
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
  << ", gbthash: " << gwHash.ToString();

  return Strings::Format("{\"created_at_ts\":%u,"
                         "\"get_work_base64\":\"%s\","
                         "\"gwhash\":\"%s\"}",
                         (uint32_t)time(nullptr), EncodeBase64(gw).c_str(),
                         gwHash.ToString().c_str());
}

void GwMaker::submitRawGbtMsg(bool checkTime) {
  ScopeLock sl(lock_);

  if (checkTime &&
      lastGwMakeTime_ + kRpcCallInterval_ > time(nullptr)) {
    return;
  }

  const string rawGwMsg = makeRawGwMsg();
  if (rawGwMsg.length() == 0) {
    LOG(ERROR) << "get rawGw failure";
    return;
  }
  lastGwMakeTime_ = (uint32_t)time(nullptr);

  // submit to Kafka
  LOG(INFO) << "submit to Kafka, msg len: " << rawGwMsg.length();
  kafkaProduceMsg(rawGwMsg.c_str(), rawGwMsg.length());
}

void GwMaker::run() {

  while (running_) {
    sleep(1);
    submitRawGbtMsg(true);
  }
}