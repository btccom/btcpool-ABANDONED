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

#include "BitcoinUtils.h"

#include <glog/logging.h>

#include <util.h>

#include "Utils.h"
#include "utilities_js.hpp"
#include "hash.h"

//
// bitcoind zmq pub msg type: "hashblock", "hashtx", "rawblock", "rawtx"
//
static const std::string BITCOIND_ZMQ_HASHBLOCK = "hashblock";
static const std::string BITCOIND_ZMQ_HASHTX = "hashtx";

//
// namecoind zmq pub msg type: "hashblock", "hashtx", "rawblock", "rawtx"
//
static const std::string NAMECOIND_ZMQ_HASHBLOCK = "hashblock";
static const std::string NAMECOIND_ZMQ_HASHTX = "hashtx";

static bool CheckZmqPublisher(
    zmq::context_t &context,
    const std::string &address,
    const std::string &msgType) {
  zmq::socket_t subscriber(context, ZMQ_SUB);
  subscriber.connect(address);
  subscriber.setsockopt(ZMQ_SUBSCRIBE, msgType.c_str(), msgType.size());
  zmq::message_t ztype, zcontent;

  LOG(INFO) << "check " << address << " zmq, waiting for zmq message '"
            << msgType << "'...";
  try {
    subscriber.recv(&ztype);
  } catch (zmq::error_t &e) {
    LOG(ERROR) << address << " zmq recv exception: " << e.what();
    return false;
  }
  const string type =
      std::string(static_cast<char *>(ztype.data()), ztype.size());

  if (type == msgType) {
    subscriber.recv(&zcontent);
    string content;
    Bin2Hex(static_cast<uint8_t *>(zcontent.data()), zcontent.size(), content);
    LOG(INFO) << address << " zmq recv " << type << ": " << content;
    return true;
  }

  LOG(ERROR) << "unknown zmq message type from " << address << ": " << type;
  return false;
}

static void ListenToZmqPublisher(
    zmq::context_t &context,
    const std::string &address,
    const std::string &msgType,
    const std::atomic<bool> &running,
    uint32_t timeout,
    std::function<void()> callback) {
  int timeoutMs = timeout * 1000;
  LOG_IF(FATAL, timeoutMs <= 0) << "zmq timeout has to be positive!";

  while (running) {
    zmq::socket_t subscriber(context, ZMQ_SUB);
    subscriber.connect(address);
    subscriber.setsockopt(ZMQ_SUBSCRIBE, msgType.c_str(), msgType.size());
    subscriber.setsockopt(ZMQ_RCVTIMEO, &timeoutMs, sizeof(timeout));

    while (running) {
      zmq::message_t zType, zContent;
      try {
        // use block mode with receive timeout
        if (subscriber.recv(&zType) == false) {
          LOG(WARNING) << "zmq recv timeout, reconnecting to " << address;
          break;
        }
      } catch (zmq::error_t &e) {
        LOG(ERROR) << address << " zmq recv exception: " << e.what();
        break; // break big while
      }

      if (0 ==
          msgType.compare(
              0,
              msgType.size(),
              static_cast<char *>(zType.data()),
              zType.size())) {
        subscriber.recv(&zContent);
        string content;
        Bin2Hex(
            static_cast<uint8_t *>(zContent.data()), zContent.size(), content);
        LOG(INFO) << ">>>> " << address << " zmq recv " << msgType << ": "
                  << content << " <<<<";
        LOG(INFO) << "get zmq message, call rpc getblocktemplate";
        callback();
      }
      // Ignore any unknown fields to keep forward compatible.
      // Message sender may add new fields in the future.

    } /* /while */

    subscriber.close();
  }
  LOG(INFO) << "stop thread listen to bitcoind";
}

///////////////////////////////////  GbtMaker  /////////////////////////////////
GbtMaker::GbtMaker(
    const string &zmqBitcoindAddr,
    uint32_t zmqTimeout,
    const string &bitcoindRpcAddr,
    const string &bitcoindRpcUserpass,
    const string &kafkaBrokers,
    const string &kafkaRawGbtTopic,
    uint32_t kRpcCallInterval,
    bool isCheckZmq)
  : running_(true)
  , zmqContext_(std::make_unique<zmq::context_t>(1 /*i/o threads*/))
  , zmqBitcoindAddr_(zmqBitcoindAddr)
  , zmqTimeout_(zmqTimeout)
  , bitcoindRpcAddr_(bitcoindRpcAddr)
  , bitcoindRpcUserpass_(bitcoindRpcUserpass)
  , lastGbtMakeTime_(0)
  , kRpcCallInterval_(kRpcCallInterval)
  , kafkaBrokers_(kafkaBrokers)
  , kafkaRawGbtTopic_(kafkaRawGbtTopic)
  , kafkaProducer_(
        kafkaBrokers_.c_str(), kafkaRawGbtTopic_.c_str(), 0 /* partition */)
  , isCheckZmq_(isCheckZmq) {
#if defined(CHAIN_TYPE_BCH) || defined(CHAIN_TYPE_BSV)
  lastGbtLightMakeTime_ = 0;
#endif
}

GbtMaker::~GbtMaker() {
}

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

  // check bitcoind network
  if (!checkBitcoinRPC(
          bitcoindRpcAddr_.c_str(), bitcoindRpcUserpass_.c_str())) {
    return false;
  }

  if (isCheckZmq_ &&
      !CheckZmqPublisher(*zmqContext_, zmqBitcoindAddr_, BITCOIND_ZMQ_HASHTX))
    return false;

  return true;
}

void GbtMaker::stop() {
  if (!running_) {
    return;
  }
  running_ = false;
  zmqContext_.reset();
  LOG(INFO) << "stop gbtmaker";
}

void GbtMaker::kafkaProduceMsg(const void *payload, size_t len) {
  kafkaProducer_.produce(payload, len);
}

bool GbtMaker::bitcoindRpcGBT(string &response) {
  string request =
      "{\"jsonrpc\":\"1.0\",\"id\":\"1\",\"method\":\"getblocktemplate\","
      "\"params\":[{\"rules\" : [\"segwit\"]}]}";
  bool res = blockchainNodeRpcCall(
      bitcoindRpcAddr_.c_str(),
      bitcoindRpcUserpass_.c_str(),
      request.c_str(),
      response);
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
  if (!JsonNode::parse(gbt.c_str(), gbt.c_str() + gbt.length(), r)) {
    LOG(ERROR) << "decode gbt failure: " << gbt;
    return "";
  }

  // check fields
  if (r["result"].type() != Utilities::JS::type::Obj ||
      r["result"]["previousblockhash"].type() != Utilities::JS::type::Str ||
      r["result"]["height"].type() != Utilities::JS::type::Int ||
#ifdef CHAIN_TYPE_ZEC
      r["result"]["coinbasetxn"].type() != Utilities::JS::type::Obj ||
      r["result"]["coinbasetxn"]["data"].type() != Utilities::JS::type::Str ||
      r["result"]["coinbasetxn"]["fee"].type() != Utilities::JS::type::Int ||
#else
      r["result"]["coinbasevalue"].type() != Utilities::JS::type::Int ||
#endif
      r["result"]["bits"].type() != Utilities::JS::type::Str ||
      r["result"]["mintime"].type() != Utilities::JS::type::Int ||
      r["result"]["curtime"].type() != Utilities::JS::type::Int ||
      r["result"]["version"].type() != Utilities::JS::type::Int) {
    LOG(ERROR) << "gbt check fields failure";
    return "";
  }
  const uint256 gbtHash = Hash(gbt.begin(), gbt.end());

  LOG(INFO) << "gbt height: " << r["result"]["height"].uint32()
            << ", prev_hash: " << r["result"]["previousblockhash"].str()
#ifdef CHAIN_TYPE_ZEC
            << ", coinbase_fee: "
            << r["result"]["coinbasevalue"]["fee"].uint64()
#else
            << ", coinbase_value: " << r["result"]["coinbasevalue"].uint64()
#endif
            << ", bits: " << r["result"]["bits"].str()
            << ", mintime: " << r["result"]["mintime"].uint32()
            << ", version: " << r["result"]["version"].uint32() << "|0x"
            << Strings::Format("%08x", r["result"]["version"].uint32())
            << ", gbthash: " << gbtHash.ToString();

  return Strings::Format(
      "{\"created_at_ts\":%u,"
      "\"block_template_base64\":\"%s\","
      "\"gbthash\":\"%s\"}",
      (uint32_t)time(nullptr),
      EncodeBase64(gbt),
      gbtHash.ToString());
  //  return Strings::Format("{\"created_at_ts\":%u,"
  //                         "\"gbthash\":\"%s\"}",
  //                         (uint32_t)time(nullptr),
  //                         gbtHash.ToString());
}

void GbtMaker::submitRawGbtMsg(bool checkTime) {
  ScopeLock sl(lock_);

  if (checkTime && lastGbtMakeTime_ + kRpcCallInterval_ > time(nullptr)) {
    return;
  }

  const string rawGbtMsg = makeRawGbtMsg();
  if (rawGbtMsg.length() == 0) {
    LOG(ERROR) << "get rawgbt failure";
    return;
  }
  lastGbtMakeTime_ = (uint32_t)time(nullptr);

  // submit to Kafka
  LOG(INFO) << "sumbit to Kafka, msg len: " << rawGbtMsg.size();
  kafkaProduceMsg(rawGbtMsg.data(), rawGbtMsg.size());
}

#if defined(CHAIN_TYPE_BCH) || defined(CHAIN_TYPE_BSV)
bool GbtMaker::bitcoindRpcGBTLight(string &response) {
#ifdef CHAIN_TYPE_BSV
  string request =
      "{\"jsonrpc\":\"1.0\",\"id\":\"1\",\"method\":\"getminingcandidate\"}";
#else
  string request =
      "{\"jsonrpc\":\"1.0\",\"id\":\"1\",\"method\":\"getblocktemplatelight\","
      "\"params\":[{\"rules\" : [\"segwit\"]}]}";
#endif
  bool res = blockchainNodeRpcCall(
      bitcoindRpcAddr_.c_str(),
      bitcoindRpcUserpass_.c_str(),
      request.c_str(),
      response);
  if (!res) {
    LOG(ERROR) << "bitcoind rpc gbtlight failure";
    return false;
  } else {
    LOG(INFO) << "bitcoind response: " << response;
  }
  return true;
}

string GbtMaker::makeRawGbtLightMsg() {
  string gbt;
  if (!bitcoindRpcGBTLight(gbt)) {
    return "";
  }

  JsonNode r;
  if (!JsonNode::parse(gbt.c_str(), gbt.c_str() + gbt.length(), r)) {
    LOG(ERROR) << "decode gbt failure: " << gbt;
    return "";
  }

  // check fields
  if (r["result"].type() != Utilities::JS::type::Obj ||
      r["result"][LIGHTGBT_PREV_HASH].type() != Utilities::JS::type::Str ||
      r["result"]["height"].type() != Utilities::JS::type::Int ||
      r["result"][LIGHTGBT_COINBASE_VALUE].type() != Utilities::JS::type::Int ||
      r["result"][LIGHTGBT_BITS].type() != Utilities::JS::type::Str ||
#ifndef CHAIN_TYPE_BSV
      r["result"]["mintime"].type() != Utilities::JS::type::Int ||
#endif
      r["result"][LIGHTGBT_TIME].type() != Utilities::JS::type::Int ||
      r["result"]["version"].type() != Utilities::JS::type::Int) {
    LOG(ERROR) << "gbt light check fields failure";
    return "";
  }

  const uint256 gbtHash = Hash(gbt.begin(), gbt.end());
  LOG(INFO) << "light gbt height: " << r["result"]["height"].uint32()
            << ", prev_hash: " << r["result"][LIGHTGBT_PREV_HASH].str()
            << ", coinbase_value: "
            << r["result"][LIGHTGBT_COINBASE_VALUE].uint64()
            << ", bits: " << r["result"][LIGHTGBT_BITS].str()
#ifndef CHAIN_TYPE_BSV
            << ", mintime: " << r["result"]["mintime"].uint32()
#endif
            << ", version: " << r["result"]["version"].uint32() << "|0x"
            << Strings::Format("%08x", r["result"]["version"].uint32())
            << ", gbthash: " << gbtHash.ToString();

  string result = Strings::Format(
      "{\"created_at_ts\":%u,"
      "\"block_template_base64\":\"%s\","
      "\"gbthash\":\"%s\"}",
      (uint32_t)time(nullptr),
      EncodeBase64(gbt),
      gbtHash.ToString());
  LOG(INFO) << "makeRawGbtLightMsg result: " << result;

  return result;
}

void GbtMaker::submitRawGbtLightMsg(bool checkTime) {
  ScopeLock sl(lock_);

  if (checkTime && lastGbtLightMakeTime_ + kRpcCallInterval_ > time(nullptr)) {
    return;
  }

  const string rawGbtLightMsg = makeRawGbtLightMsg();
  if (rawGbtLightMsg.length() == 0) {
    LOG(ERROR) << "get rawgbt light failure";
    return;
  }
  LOG(INFO) << "rawGbtlight message: " << rawGbtLightMsg;
  lastGbtLightMakeTime_ = (uint32_t)time(nullptr);

  // submit to Kafka
  LOG(INFO) << "sumbit to Kafka, msg len: " << rawGbtLightMsg.size();
  kafkaProduceMsg(rawGbtLightMsg.data(), rawGbtLightMsg.size());
}
#endif // CHAIN_TYPE_BCH

void GbtMaker::threadListenBitcoind() {
  ListenToZmqPublisher(
      *zmqContext_,
      zmqBitcoindAddr_,
      BITCOIND_ZMQ_HASHBLOCK,
      running_,
      zmqTimeout_,
      [this]() { submitRawGbtMsg(false); });
}

#if defined(CHAIN_TYPE_BCH) || defined(CHAIN_TYPE_BSV)

void GbtMaker::threadListenBitcoindLightMsg() {
  ListenToZmqPublisher(
      *zmqContext_,
      zmqBitcoindAddr_,
      BITCOIND_ZMQ_HASHBLOCK,
      running_,
      zmqTimeout_,
      [this]() { submitRawGbtLightMsg(false); });
}

void GbtMaker::runLightGbt() {
  auto threadListenBitcoind =
      std::thread(&GbtMaker::threadListenBitcoindLightMsg, this);

  while (running_) {
    std::this_thread::sleep_for(1s);
    submitRawGbtLightMsg(true);
  }

  if (threadListenBitcoind.joinable())
    threadListenBitcoind.join();
}

#endif
void GbtMaker::run() {
  auto threadListenBitcoind =
      std::thread(&GbtMaker::threadListenBitcoind, this);

  while (running_) {
    std::this_thread::sleep_for(1s);
    submitRawGbtMsg(true);
  }

  if (threadListenBitcoind.joinable())
    threadListenBitcoind.join();
}

//////////////////////////////// NMCAuxBlockMaker //////////////////////////////
NMCAuxBlockMaker::NMCAuxBlockMaker(
    const string &zmqNamecoindAddr,
    uint32_t zmqTimeout,
    const string &rpcAddr,
    const string &rpcUserpass,
    const string &kafkaBrokers,
    const string &kafkaAuxPowGwTopic,
    uint32_t kRpcCallInterval,
    const string &fileLastRpcCallTime,
    bool isCheckZmq,
    const string &coinbaseAddress)
  : running_(true)
  , zmqContext_(std::make_unique<zmq::context_t>(1 /*i/o threads*/))
  , zmqNamecoindAddr_(zmqNamecoindAddr)
  , zmqTimeout_(zmqTimeout)
  , rpcAddr_(rpcAddr)
  , rpcUserpass_(rpcUserpass)
  , lastCallTime_(0)
  , kRpcCallInterval_(kRpcCallInterval)
  , fileLastRpcCallTime_(fileLastRpcCallTime)
  , kafkaBrokers_(kafkaBrokers)
  , kafkaAuxPowGwTopic_(kafkaAuxPowGwTopic)
  , kafkaProducer_(
        kafkaBrokers_.c_str(), kafkaAuxPowGwTopic_.c_str(), 0 /* partition */)
  , isCheckZmq_(isCheckZmq)
  , coinbaseAddress_(coinbaseAddress) {
}

NMCAuxBlockMaker::~NMCAuxBlockMaker() {
}

bool NMCAuxBlockMaker::callRpcCreateAuxBlock(string &resp) {
  //
  // curl -v  --user "username:password"
  // -d '{"jsonrpc": "1.0", "id":"curltest", "method":
  // "createauxblock","params": []}' -H 'content-type: text/plain;'
  // "http://127.0.0.1:8336"
  //
  string request = "";
  if (useCreateAuxBlockInterface_) {
    request =
        "{\"jsonrpc\":\"1.0\",\"id\":\"1\",\"method\":\"createauxblock\","
        "\"params\":[\"";
    request += coinbaseAddress_;
    request += "\"]}";
  } else {
    request =
        "{\"jsonrpc\":\"1.0\",\"id\":\"1\",\"method\":\"getauxblock\","
        "\"params\":[]}";
  }

  bool res = blockchainNodeRpcCall(
      rpcAddr_.c_str(), rpcUserpass_.c_str(), request.c_str(), resp);
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
  if (!JsonNode::parse(aux.c_str(), aux.c_str() + aux.length(), r)) {
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
  if (r["result"].type() != Utilities::JS::type::Obj ||
      r["result"]["hash"].type() != Utilities::JS::type::Str ||
      r["result"]["chainid"].type() != Utilities::JS::type::Int ||
      r["result"]["previousblockhash"].type() != Utilities::JS::type::Str ||
      r["result"]["coinbasevalue"].type() != Utilities::JS::type::Int ||
      r["result"]["bits"].type() != Utilities::JS::type::Str ||
      r["result"]["height"].type() != Utilities::JS::type::Int) {
    LOG(ERROR) << "namecoin aux check fields failure";
    return "";
  }

  // the MergedMiningProxy will indicate (optional) merkle_size and merkle_nonce
  // https://github.com/btccom/stratumSwitcher/tree/master/mergedMiningProxy
  int32_t merkleSize = 1;
  int32_t merkleNonce = 0;

  if (r["result"]["merkle_size"].type() == Utilities::JS::type::Int) {
    merkleSize = r["result"]["merkle_size"].int32();
  }
  if (r["result"]["merkle_nonce"].type() == Utilities::JS::type::Int) {
    merkleNonce = r["result"]["merkle_nonce"].int32();
  }

  // message for kafka
  string msg = Strings::Format(
      "{\"created_at_ts\":%u,"
      " \"hash\":\"%s\", \"height\":%d,"
      " \"merkle_size\":%d, \"merkle_nonce\":%d,"
      " \"chainid\":%d,  \"bits\":\"%s\","
      " \"rpc_addr\":\"%s\", \"rpc_userpass\":\"%s\""
      "}",
      (uint32_t)time(nullptr),
      r["result"]["hash"].str(),
      r["result"]["height"].int32(),
      merkleSize,
      merkleNonce,
      r["result"]["chainid"].int32(),
      r["result"]["bits"].str(),
      rpcAddr_,
      rpcUserpass_);

  LOG(INFO) << "createauxblock, height: " << r["result"]["height"].int32()
            << ", hash: " << r["result"]["hash"].str()
            << ", previousblockhash: "
            << r["result"]["previousblockhash"].str();

  return msg;
}

void NMCAuxBlockMaker::submitAuxblockMsg(bool checkTime) {
  ScopeLock sl(lock_);

  if (checkTime && lastCallTime_ + kRpcCallInterval_ > time(nullptr)) {
    return;
  }
  const string auxMsg = makeAuxBlockMsg();
  if (auxMsg.length() == 0) {
    LOG(ERROR) << "createauxblock failure";
    return;
  }
  lastCallTime_ = (uint32_t)time(nullptr);

  // submit to Kafka
  LOG(INFO) << "sumbit to Kafka, msg len: " << auxMsg.size();
  kafkaProduceMsg(auxMsg.data(), auxMsg.size());

  // save the timestamp to file, for monitor system
  if (!fileLastRpcCallTime_.empty()) {
    writeTime2File(fileLastRpcCallTime_.c_str(), lastCallTime_);
  }
}

void NMCAuxBlockMaker::threadListenNamecoind() {
  ListenToZmqPublisher(
      *zmqContext_,
      zmqNamecoindAddr_,
      NAMECOIND_ZMQ_HASHBLOCK,
      running_,
      zmqTimeout_,
      [this]() { submitAuxblockMsg(false); });
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
  if (!checkBitcoinRPC(rpcAddr_.c_str(), rpcUserpass_.c_str())) {
    return false;
  }

  // check aux mining rpc commands: createauxblock & submitauxblock
  {
    string response;
    string request =
        "{\"jsonrpc\":\"1.0\",\"id\":\"1\",\"method\":\"help\",\"params\":[]}";
    bool res = blockchainNodeRpcCall(
        rpcAddr_.c_str(), rpcUserpass_.c_str(), request.c_str(), response);
    if (!res) {
      LOG(ERROR) << "namecoind rpc call failure";
      return false;
    }

    useCreateAuxBlockInterface_ =
        (response.find("createauxblock") == std::string::npos ||
         response.find("submitauxblock") == std::string::npos)
        ? false
        : true;

    LOG(INFO) << "namecoind "
              << (useCreateAuxBlockInterface_ ? " " : "doesn't ")
              << "support rpc commands: createauxblock and submitauxblock";
  }

  if (isCheckZmq_ &&
      !CheckZmqPublisher(*zmqContext_, zmqNamecoindAddr_, NAMECOIND_ZMQ_HASHTX))
    return false;

  return true;
}

void NMCAuxBlockMaker::stop() {
  if (!running_) {
    return;
  }
  running_ = false;
  zmqContext_.reset();
  LOG(INFO) << "stop namecoin auxblock maker";
}

void NMCAuxBlockMaker::run() {
  //
  // listen namecoind zmq for detect new block coming
  //
  auto threadListenNamecoind =
      std::thread(&NMCAuxBlockMaker::threadListenNamecoind, this);

  // createauxblock interval
  while (running_) {
    std::this_thread::sleep_for(1s);
    submitAuxblockMsg(true);
  }

  if (threadListenNamecoind.joinable())
    threadListenNamecoind.join();
}
