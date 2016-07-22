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
#include "GbtMaker.h"

#include <glog/logging.h>

#include "bitcoin/util.h"

#include "Utils.h"
#include "utilities_js.hpp"

//
// bitcoind zmq pub msg type: "hashblock", "hashtx", "rawblock", "rawtx"
//
#define BITCOIND_ZMQ_HASHBLOCK      "hashblock"
#define BITCOIND_ZMQ_HASHTX         "hashtx"

///////////////////////////////////  GbtMaker  /////////////////////////////////
GbtMaker::GbtMaker(const string &zmqBitcoindAddr,
                   const string &bitcoindRpcAddr, const string &bitcoindRpcUserpass,
                   const string &kafkaBrokers, uint32_t kRpcCallInterval,
                   bool isCheckZmq)
: running_(true), zmqContext_(1/*i/o threads*/),
zmqBitcoindAddr_(zmqBitcoindAddr), bitcoindRpcAddr_(bitcoindRpcAddr),
bitcoindRpcUserpass_(bitcoindRpcUserpass), lastGbtMakeTime_(0), kRpcCallInterval_(kRpcCallInterval),
kafkaBrokers_(kafkaBrokers),
kafkaProducer_(kafkaBrokers_.c_str(), KAFKA_TOPIC_RAWGBT, 0/* partition */),
isCheckZmq_(isCheckZmq)
{
}

GbtMaker::~GbtMaker() {}

bool GbtMaker::init() {
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

  // check bitcoind
  {
    string response;
    string request = "{\"jsonrpc\":\"1.0\",\"id\":\"1\",\"method\":\"getinfo\",\"params\":[]}";
    bool res = bitcoindRpcCall(bitcoindRpcAddr_.c_str(), bitcoindRpcUserpass_.c_str(),
                               request.c_str(), response);
    if (!res) {
      LOG(ERROR) << "bitcoind rpc call failure";
      return false;
    }
    LOG(INFO) << "bitcoind getinfo: " << response;

    JsonNode r;
    if (!JsonNode::parse(response.c_str(),
                         response.c_str() + response.length(), r)) {
      LOG(ERROR) << "decode gbt failure";
      return false;
    }

    // check fields
    if (r["result"].type() != Utilities::JS::type::Obj ||
        r["result"]["connections"].type() != Utilities::JS::type::Int ||
        r["result"]["blocks"].type()      != Utilities::JS::type::Int) {
      LOG(ERROR) << "getinfo missing some fields";
      return false;
    }
    if (r["result"]["connections"].int32() <= 0) {
      LOG(ERROR) << "bitcoind connections is zero";
      return false;
    }
  }

  if (isCheckZmq_ && !checkBitcoindZMQ())
    return false;

  return true;
}

bool GbtMaker::checkBitcoindZMQ() {
  //
  // bitcoind MUST with option: -zmqpubhashtx
  //
  zmq::socket_t subscriber(zmqContext_, ZMQ_SUB);
  subscriber.connect(zmqBitcoindAddr_);
  subscriber.setsockopt(ZMQ_SUBSCRIBE,
                        BITCOIND_ZMQ_HASHTX, strlen(BITCOIND_ZMQ_HASHTX));
  zmq::message_t ztype, zcontent;

  LOG(INFO) << "check bitcoind zmq, waiting for zmq message 'hashtx'...";
  try {
    subscriber.recv(&ztype);
    subscriber.recv(&zcontent);
  } catch (std::exception & e) {
    LOG(ERROR) << "bitcoind zmq recv exception: " << e.what();
    return false;
  }
  const string type    = std::string(static_cast<char*>(ztype.data()),    ztype.size());
  const string content = std::string(static_cast<char*>(zcontent.data()), zcontent.size());

  if (type == BITCOIND_ZMQ_HASHTX) {
    string hashHex;
    Bin2Hex((const uint8 *)content.data(), content.size(), hashHex);
    LOG(INFO) << "bitcoind zmq recv hashtx: " << hashHex;
    return true;
  }

  LOG(ERROR) << "unknown zmq message type from bitcoind: " << type;
  return false;
}

void GbtMaker::stop() {
  if (!running_) {
    return;
  }
  running_ = false;
  LOG(INFO) << "stop gbtmaker";
}

void GbtMaker::kafkaProduceMsg(const void *payload, size_t len) {
  kafkaProducer_.produce(payload, len);
}

bool GbtMaker::bitcoindRpcGBT(string &response) {
  string request = "{\"jsonrpc\":\"1.0\",\"id\":\"1\",\"method\":\"getblocktemplate\",\"params\":[]}";
  bool res = bitcoindRpcCall(bitcoindRpcAddr_.c_str(), bitcoindRpcUserpass_.c_str(),
                             request.c_str(), response);
  if (!res) {
    LOG(ERROR) << "bitcoind rpc failure";
    return false;
  }
  return true;
}

string GbtMaker::makeRawGbtMsg() {
  string gbt;
  if (!bitcoindRpcGBT(gbt)) {
    return "";
  }

  JsonNode r;
  if (!JsonNode::parse(gbt.c_str(),
                      gbt.c_str() + gbt.length(), r)) {
    LOG(ERROR) << "decode gbt failure: " << gbt;
    return "";
  }

  // check fields
  if (r["result"].type()                      != Utilities::JS::type::Obj ||
      r["result"]["previousblockhash"].type() != Utilities::JS::type::Str ||
      r["result"]["height"].type()            != Utilities::JS::type::Int ||
      r["result"]["coinbasevalue"].type()     != Utilities::JS::type::Int ||
      r["result"]["bits"].type()              != Utilities::JS::type::Str ||
      r["result"]["mintime"].type()           != Utilities::JS::type::Int ||
      r["result"]["version"].type()           != Utilities::JS::type::Int) {
    LOG(ERROR) << "gbt check fields failure";
    return "";
  }
  const uint256 gbtHash = Hash(gbt.begin(), gbt.end());

  LOG(INFO) << "gbt height: " << r["result"]["height"].uint32()
  << ", prev_hash: "          << r["result"]["previousblockhash"].str()
  << ", coinbase_value: "     << r["result"]["coinbasevalue"].uint64()
  << ", bits: "    << r["result"]["bits"].str()
  << ", mintime: " << r["result"]["mintime"].uint32()
  << ", version: " << r["result"]["version"].uint32()
  << "|0x" << Strings::Format("%08x", r["result"]["version"].uint32())
  << ", gbthash: " << gbtHash.ToString();

  return Strings::Format("{\"created_at_ts\":%u,"
                         "\"block_template_base64\":\"%s\","
                         "\"gbthash\":\"%s\"}",
                         (uint32_t)time(nullptr), EncodeBase64(gbt).c_str(),
                         gbtHash.ToString().c_str());
//  return Strings::Format("{\"created_at_ts\":%u,"
//                         "\"gbthash\":\"%s\"}",
//                         (uint32_t)time(nullptr),
//                         gbtHash.ToString().c_str());
}

void GbtMaker::submitRawGbtMsg(bool checkTime) {
  ScopeLock sl(lock_);

  if (checkTime &&
      lastGbtMakeTime_ + kRpcCallInterval_ > time(nullptr)) {
    return;
  }

  const string rawGbtMsg = makeRawGbtMsg();
  if (rawGbtMsg.length() == 0) {
    LOG(ERROR) << "get rawgbt failure";
    return;
  }
  lastGbtMakeTime_ = (uint32_t)time(nullptr);

  // submit to Kafka
  LOG(INFO) << "sumbit to Kafka, msg len: " << rawGbtMsg.length();
  kafkaProduceMsg(rawGbtMsg.c_str(), rawGbtMsg.length());
}

void GbtMaker::threadListenBitcoind() {
  zmq::socket_t subscriber(zmqContext_, ZMQ_SUB);
  subscriber.connect(zmqBitcoindAddr_);
  subscriber.setsockopt(ZMQ_SUBSCRIBE,
                        BITCOIND_ZMQ_HASHBLOCK, strlen(BITCOIND_ZMQ_HASHBLOCK));

  while (running_) {
    zmq::message_t ztype, zcontent;
    try {
      if (subscriber.recv(&ztype, ZMQ_DONTWAIT) == false) {
        if (!running_) { break; }
        usleep(50000);  // so we sleep and try again
        continue;
      }
      subscriber.recv(&zcontent);
    } catch (std::exception & e) {
      LOG(ERROR) << "bitcoind zmq recv exception: " << e.what();
      break;  // break big while
    }
    const string type    = std::string(static_cast<char*>(ztype.data()),    ztype.size());
    const string content = std::string(static_cast<char*>(zcontent.data()), zcontent.size());

    if (type == BITCOIND_ZMQ_HASHBLOCK)
    {
      string hashHex;
      Bin2Hex((const uint8 *)content.data(), content.size(), hashHex);
      LOG(INFO) << ">>>> bitcoind recv hashblock: " << hashHex << " <<<<";
      submitRawGbtMsg(false);
    }
    else
    {
      LOG(ERROR) << "unknown message type from bitcoind: " << type;
    }
  } /* /while */

  subscriber.close();
  LOG(INFO) << "stop thread listen to bitcoind";
}

void GbtMaker::run() {
  thread threadListenBitcoind = thread(&GbtMaker::threadListenBitcoind, this);

  while (running_) {
    sleep(1);
    submitRawGbtMsg(true);
  }

  if (threadListenBitcoind.joinable())
    threadListenBitcoind.join();
}
