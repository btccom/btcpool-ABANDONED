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
#include "bitcoin/utilstrencodings.h"

#include "Utils.h"
#include "utilities_js.hpp"

//
// bitcoind zmq pub msg type: "hashblock", "hashtx", "rawblock", "rawtx"
//
#define BITCOIND_ZMQ_HASHBLOCK      "hashblock"
#define BITCOIND_ZMQ_HASHTX         "hashtx"

//
// namecoind zmq pub msg type: "hashblock", "hashtx", "rawblock", "rawtx"
//
#define NAMECOIND_ZMQ_HASHBLOCK      "hashblock"
#define NAMECOIND_ZMQ_HASHTX         "hashtx"


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
  string request = "{\"jsonrpc\":\"1.0\",\"id\":\"1\",\"method\":\"getblocktemplate\",\"params\":[{\"rules\" : [\"segwit\"]}]}";
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
      r["result"]["curtime"].type()           != Utilities::JS::type::Int ||
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
    zmq::message_t zType, zContent, zSequence;
    try {
      // if we use block mode, can't quit this thread
      if (subscriber.recv(&zType, ZMQ_DONTWAIT) == false) {
        if (!running_) { break; }
        usleep(20000);  // so we sleep and try again
        continue;
      }
      subscriber.recv(&zContent);
      subscriber.recv(&zSequence);
    } catch (std::exception & e) {
      LOG(ERROR) << "bitcoind zmq recv exception: " << e.what();
      break;  // break big while
    }
    const string type     = std::string(static_cast<char*>(zType.data()),     zType.size());
    const string content  = std::string(static_cast<char*>(zContent.data()),  zContent.size());
    const string sequence = std::string(static_cast<char*>(zSequence.data()), zSequence.size());

    if (type == BITCOIND_ZMQ_HASHBLOCK)
    {
      string hashHex;
      Bin2Hex((const uint8 *)content.data(), content.size(), hashHex);
      string sequenceHex;
      Bin2Hex((const uint8 *)sequence.data(), sequence.size(), sequenceHex);
      LOG(INFO) << ">>>> bitcoind recv hashblock: " << hashHex << ", sequence: " << sequenceHex << " <<<<";
    }
    else
    {
      LOG(ERROR) << "unknown message type from bitcoind: " << type;
    }

    // sometimes will decode zmq message fail, no matter what it is, we just
    // call gbt again
    LOG(INFO) << "get zmq message, call rpc getblocktemplate";
    submitRawGbtMsg(false);

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



//////////////////////////////// NMCAuxBlockMaker //////////////////////////////
NMCAuxBlockMaker::NMCAuxBlockMaker(const string &zmqNamecoindAddr,
                                   const string &rpcAddr,
                                   const string &rpcUserpass,
                                   const string &kafkaBrokers,
                                   uint32_t kRpcCallInterval,
                                   const string &fileLastRpcCallTime,
                                   bool isCheckZmq,
                                   const string &coinbaseAddress) :
running_(true), zmqContext_(1/*i/o threads*/),
zmqNamecoindAddr_(zmqNamecoindAddr),
rpcAddr_(rpcAddr), rpcUserpass_(rpcUserpass),
lastCallTime_(0), kRpcCallInterval_(kRpcCallInterval),
fileLastRpcCallTime_(fileLastRpcCallTime),
kafkaBrokers_(kafkaBrokers),
kafkaProducer_(kafkaBrokers_.c_str(), KAFKA_TOPIC_NMC_AUXBLOCK, 0/* partition */),
isCheckZmq_(isCheckZmq), coinbaseAddress_(coinbaseAddress)
{
}

NMCAuxBlockMaker::~NMCAuxBlockMaker() {}

bool NMCAuxBlockMaker::checkNamecoindZMQ() {
  //
  // namecoind MUST with option: -zmqpubhashtx
  //
  zmq::socket_t subscriber(zmqContext_, ZMQ_SUB);
  subscriber.connect(zmqNamecoindAddr_);
  subscriber.setsockopt(ZMQ_SUBSCRIBE,
                        NAMECOIND_ZMQ_HASHTX, strlen(NAMECOIND_ZMQ_HASHTX));
  zmq::message_t ztype, zcontent;

  LOG(INFO) << "check namecoind zmq, waiting for zmq message 'hashtx'...";
  try {
    subscriber.recv(&ztype);
    subscriber.recv(&zcontent);
  } catch (std::exception & e) {
    LOG(ERROR) << "namecoind zmq recv exception: " << e.what();
    return false;
  }
  const string type    = std::string(static_cast<char*>(ztype.data()),    ztype.size());
  const string content = std::string(static_cast<char*>(zcontent.data()), zcontent.size());

  if (type == NAMECOIND_ZMQ_HASHTX) {
    string hashHex;
    Bin2Hex((const uint8 *)content.data(), content.size(), hashHex);
    LOG(INFO) << "namecoind zmq recv hashtx: " << hashHex;
    return true;
  }

  LOG(ERROR) << "unknown zmq message type from namecoind: " << type;
  return false;
}

bool NMCAuxBlockMaker::callRpcCreateAuxBlock(string &resp) {
  //
  // curl -v  --user "username:password"
  // -d '{"jsonrpc": "1.0", "id":"curltest", "method": "createauxblock","params": []}'
  // -H 'content-type: text/plain;' "http://127.0.0.1:8336"
  //
  string request = "{\"jsonrpc\":\"1.0\",\"id\":\"1\",\"method\":\"createauxblock\",\"params\":[\"";
  request += coinbaseAddress_;
  request += "\"]}";
  bool res = bitcoindRpcCall(rpcAddr_.c_str(), rpcUserpass_.c_str(),
                             request.c_str(), resp);
  if (!res) {
    LOG(ERROR) << "namecoind rpc failure";
    return false;
  }
  return true;
}

string NMCAuxBlockMaker::makeAuxBlockMsg() {
  string aux;
  if (!callRpcCreateAuxBlock(aux)) {
    return "";
  }
  DLOG(INFO) << "createauxblock json: " << aux;

  JsonNode r;
  if (!JsonNode::parse(aux.c_str(),
                       aux.c_str() + aux.length(), r)) {
    LOG(ERROR) << "decode createauxblock json failure: " << aux;
    return "";
  }

  //
  // {"result":
  //    {"hash":"ae3384a9c21956efb385801ccd16e2799d3a88b4245c592e37dd0b46ea3bf0f5",
  //     "chainid":1,
  //     "previousblockhash":"ba22e44e25ce8197d3ed1468d3d8441977ff18394c94c9cb486836511e020108",
  //     "coinbasevalue":2500000000,
  //     "bits":"180a7f3c","height":303852,
  //     "_target":"0000000000000000000000000000000000000000003c7f0a0000000000000000"
  //    },
  //    "error":null,"id":"curltest"
  // }
  //
  // check fields
  if (r["result"].type()                      != Utilities::JS::type::Obj ||
      r["result"]["hash"].type()              != Utilities::JS::type::Str ||
      r["result"]["chainid"].type()           != Utilities::JS::type::Int ||
      r["result"]["previousblockhash"].type() != Utilities::JS::type::Str ||
      r["result"]["coinbasevalue"].type()     != Utilities::JS::type::Int ||
      r["result"]["bits"].type()              != Utilities::JS::type::Str ||
      r["result"]["height"].type()            != Utilities::JS::type::Int) {
    LOG(ERROR) << "namecoin aux check fields failure";
    return "";
  }

  // message for kafka
  string msg = Strings::Format("{\"created_at_ts\":%u,"
                               " \"hash\":\"%s\", \"height\":%d,"
                               " \"chainid\":%d,  \"bits\":\"%s\","
                               " \"rpc_addr\":\"%s\", \"rpc_userpass\":\"%s\""
                               "}",
                               (uint32_t)time(nullptr),
                               r["result"]["hash"].str().c_str(),
                               r["result"]["height"].int32(),
                               r["result"]["chainid"].int32(),
                               r["result"]["bits"].str().c_str(),
                               rpcAddr_.c_str(), rpcUserpass_.c_str());

  LOG(INFO) << "createauxblock, height: " << r["result"]["height"].int32()
  << ", hash: " << r["result"]["hash"].str()
  << ", previousblockhash: " << r["result"]["previousblockhash"].str();

  return msg;
}

void NMCAuxBlockMaker::submitAuxblockMsg(bool checkTime) {
  ScopeLock sl(lock_);

  if (checkTime &&
      lastCallTime_ + kRpcCallInterval_ > time(nullptr)) {
    return;
  }

  const string auxMsg = makeAuxBlockMsg();
  if (auxMsg.length() == 0) {
    LOG(ERROR) << "createauxblock failure";
    return;
  }
  lastCallTime_ = (uint32_t)time(nullptr);

  // submit to Kafka
  LOG(INFO) << "sumbit to Kafka, msg len: " << auxMsg.length();
  kafkaProduceMsg(auxMsg.c_str(), auxMsg.length());

  // save the timestamp to file, for monitor system
  if (!fileLastRpcCallTime_.empty()) {
  	writeTime2File(fileLastRpcCallTime_.c_str(), lastCallTime_);
  }
}

void NMCAuxBlockMaker::threadListenNamecoind() {
  zmq::socket_t subscriber(zmqContext_, ZMQ_SUB);
  subscriber.connect(zmqNamecoindAddr_);
  subscriber.setsockopt(ZMQ_SUBSCRIBE,
                        NAMECOIND_ZMQ_HASHBLOCK, strlen(NAMECOIND_ZMQ_HASHBLOCK));

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
      LOG(ERROR) << "namecoind zmq recv exception: " << e.what();
      break;  // break big while
    }
    const string type    = std::string(static_cast<char*>(ztype.data()),    ztype.size());
    const string content = std::string(static_cast<char*>(zcontent.data()), zcontent.size());

    if (type == NAMECOIND_ZMQ_HASHBLOCK)
    {
      string hashHex;
      Bin2Hex((const uint8 *)content.data(), content.size(), hashHex);
      LOG(INFO) << ">>>> namecoind recv hashblock: " << hashHex << " <<<<";
      submitAuxblockMsg(false);
    }
    else
    {
      LOG(ERROR) << "unknown message type from namecoind: " << type;
    }
  } /* /while */

  subscriber.close();
  LOG(INFO) << "stop thread listen to namecoind";
}

void NMCAuxBlockMaker::kafkaProduceMsg(const void *payload, size_t len) {
  kafkaProducer_.produce(payload, len);
}

bool NMCAuxBlockMaker::init() {
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

  // check namecoind
  {
    string response;
    string request = "{\"jsonrpc\":\"1.0\",\"id\":\"1\",\"method\":\"getinfo\",\"params\":[]}";
    bool res = bitcoindRpcCall(rpcAddr_.c_str(), rpcUserpass_.c_str(),
                               request.c_str(), response);
    if (!res) {
      LOG(ERROR) << "namecoind rpc call failure";
      return false;
    }
    LOG(INFO) << "namecoind getinfo: " << response;

    JsonNode r;
    if (!JsonNode::parse(response.c_str(),
                         response.c_str() + response.length(), r)) {
      LOG(ERROR) << "decode getinfo failure";
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
      LOG(ERROR) << "namecoind connections is zero";
      return false;
    }
  }

  // check aux mining rpc commands: createauxblock & submitauxblock
  {
    string response;
    string request = "{\"jsonrpc\":\"1.0\",\"id\":\"1\",\"method\":\"help\",\"params\":[]}";
    bool res = bitcoindRpcCall(rpcAddr_.c_str(), rpcUserpass_.c_str(),
                               request.c_str(), response);
    if (!res) {
      LOG(ERROR) << "namecoind rpc call failure";
      return false;
    }

    if (response.find("createauxblock") == std::string::npos ||
        response.find("submitauxblock") == std::string::npos) {
      LOG(ERROR) << "namecoind doesn't support rpc commands: createauxblock and submitauxblock";
      return false;
    }
  }

  if (isCheckZmq_ && !checkNamecoindZMQ())
    return false;
  
  return true;
}

void NMCAuxBlockMaker::stop() {
  if (!running_) {
    return;
  }
  running_ = false;
  LOG(INFO) << "stop namecoin auxblock maker";
}

void NMCAuxBlockMaker::run() {
  //
  // listen namecoind zmq for detect new block coming
  //
  thread threadListenNamecoind = thread(&NMCAuxBlockMaker::threadListenNamecoind, this);

  // createauxblock interval
  while (running_) {
    sleep(1);
    submitAuxblockMsg(true);
  }

  if (threadListenNamecoind.joinable())
    threadListenNamecoind.join();
}

