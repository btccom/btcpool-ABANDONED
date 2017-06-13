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

#include "zcash/util.h"
#include "zcash/utilstrencodings.h"

#include "Utils.h"
#include "utilities_js.hpp"

#include "zcash/random.h"
#include "zcash/chainparams.h"

//
// zcashd zmq pub msg type: "hashblock", "hashtx", "rawblock", "rawtx"
//
#define ZCASHD_ZMQ_HASHBLOCK      "hashblock"
#define ZCASHD_ZMQ_HASHTX         "hashtx"


///////////////////////////////////  GbtMaker  /////////////////////////////////
GbtMaker::GbtMaker(const string poolCoinbaseInfo, const string &poolPayoutAddr,
                   const string &zmqZcashdAddr,
                   const string &zcashdRpcAddr, const string &zcashdRpcUserpass,
                   const string &kafkaBrokers, uint32_t kRpcCallInterval,
                   bool isCheckZmq)
: running_(true),
poolCoinbaseInfo_(poolCoinbaseInfo), poolPayoutAddr_(poolPayoutAddr),
zmqContext_(1/*i/o threads*/),
zmqZcashdAddr_(zmqZcashdAddr), zcashdRpcAddr_(zcashdRpcAddr),
zcashdRpcUserpass_(zcashdRpcUserpass), lastGbtMakeTime_(0), kRpcCallInterval_(kRpcCallInterval),
kafkaBrokers_(kafkaBrokers),
kafkaProducer_(kafkaBrokers_.c_str(), KAFKA_TOPIC_RAWGBT, 0/* partition */),
isCheckZmq_(isCheckZmq), blockVersion_(0)
{
  if (poolCoinbaseInfo_.length() > 40) {
    poolCoinbaseInfo_.resize(40);
    LOG(WARNING) << "pool coinbase info is too long, cut to 40 bytes";
  }
}

GbtMaker::~GbtMaker() {}

bool GbtMaker::init() {
  // check pool payout address
  if (!poolPayoutAddr_.IsValid()) {
    LOG(ERROR) << "invalid pool payout address";
    return false;
  }

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

  // check zcash
  {
    string response;
    string request = "{\"jsonrpc\":\"1.0\",\"id\":\"1\",\"method\":\"getinfo\",\"params\":[]}";
    bool res = zcashdRpcCall(zcashdRpcAddr_.c_str(), zcashdRpcUserpass_.c_str(),
                               request.c_str(), response);
    if (!res) {
      LOG(ERROR) << "zcash rpc call failure";
      return false;
    }
    LOG(INFO) << "zcash getinfo: " << response;

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
      LOG(ERROR) << "zcash connections is zero";
      return false;
    }
  }

  if (isCheckZmq_ && !checkZcashdZMQ())
    return false;

  return true;
}

bool GbtMaker::checkZcashdZMQ() {
  //
  // zcash MUST with option: -zmqpubhashtx
  //
  zmq::socket_t subscriber(zmqContext_, ZMQ_SUB);
  subscriber.connect(zmqZcashdAddr_);
  subscriber.setsockopt(ZMQ_SUBSCRIBE,
                        ZCASHD_ZMQ_HASHTX, strlen(ZCASHD_ZMQ_HASHTX));
  zmq::message_t ztype, zcontent;

  LOG(INFO) << "check zcashd zmq, waiting for zmq message 'hashtx'...";
  try {
    subscriber.recv(&ztype);
    subscriber.recv(&zcontent);
  } catch (std::exception & e) {
    LOG(ERROR) << "zcashd zmq recv exception: " << e.what();
    return false;
  }
  const string type    = std::string(static_cast<char*>(ztype.data()),    ztype.size());
  const string content = std::string(static_cast<char*>(zcontent.data()), zcontent.size());

  if (type == ZCASHD_ZMQ_HASHTX) {
    string hashHex;
    Bin2Hex((const uint8 *)content.data(), content.size(), hashHex);
    LOG(INFO) << "zcashd zmq recv hashtx: " << hashHex;
    return true;
  }

  LOG(ERROR) << "unknown zmq message type from zcash: " << type;
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

bool GbtMaker::zcashdRpcGBT(string &response) {
  string request = "{\"jsonrpc\":\"1.0\",\"id\":\"1\",\"method\":\"getblocktemplate\",\"params\":[]}";
  bool res = bitcoindRpcCall(zcashdRpcAddr_.c_str(), zcashdRpcUserpass_.c_str(),
                             request.c_str(), response);
  if (!res) {
    LOG(ERROR) << "zcashd rpc failure";
    return false;
  }
  return true;
}

string GbtMaker::makeRawGbtMsg() {
  string gbt;
  if (!zcashdRpcGBT(gbt)) {
    return "";
  }

  JsonNode r;
  if (!JsonNode::parse(gbt.c_str(),
                      gbt.c_str() + gbt.length(), r)) {
    LOG(ERROR) << "decode gbt failure: " << gbt;
    return "";
  }

  // check fields
  if (r["result"].type()                       != Utilities::JS::type::Obj ||
      r["result"]["previousblockhash"].type()  != Utilities::JS::type::Str ||
      r["result"]["height"].type()             != Utilities::JS::type::Int ||
      r["result"]["transactions"].type()       != Utilities::JS::type::Array ||
      r["result"]["coinbasetxn"].type()        != Utilities::JS::type::Obj ||
      r["result"]["coinbasetxn"]["fee"].type() != Utilities::JS::type::Int ||
      r["result"]["bits"].type()               != Utilities::JS::type::Str ||
      r["result"]["mintime"].type()            != Utilities::JS::type::Int ||
      r["result"]["curtime"].type()            != Utilities::JS::type::Int ||
      r["result"]["version"].type()            != Utilities::JS::type::Int) {
    LOG(ERROR) << "gbt check fields failure";
    return "";
  }

  JsonNode jgbt = r["result"];

  const uint256 prevHash = uint256S(jgbt["previousblockhash"].str());
  const int32_t height = jgbt["height"].int32();
  int32_t nVersion = 0;
  if (blockVersion_ != 0) {
    nVersion = blockVersion_;
  } else {
    nVersion = jgbt["version"].uint32();
  }
  assert(nVersion != 0);
  const uint32_t nBits   = jgbt["bits"].uint32();
  const uint32_t nTime   = jgbt["curtime"].uint32();
  const uint32_t minTime = jgbt["mintime"].uint32();
  const uint32_t maxTime = nTime + 2*60*60 - 60/* sanitly */;

  // fees from the block
  const int64_t nFees = jgbt["coinbasetxn"]["fee"].int64() * -1;
  if (nFees < 0) {
    LOG(ERROR) << "invalid nFees: " << nFees;
    return "";
  }

  // make coinbase transaction
  {
    CTxIn cbIn;
    //
    // block height, 4 bytes in script: 0x03xxxxxx
    // https://github.com/bitcoin/bips/blob/master/bip-0034.mediawiki
    // https://github.com/bitcoin/bitcoin/pull/1526
    //
    cbIn.scriptSig = CScript() << height << OP_0;

    // add current timestamp to coinbase tx input, so if the block's merkle root
    // hash is the same, there's no risk for miners to calc the same space.
    // https://github.com/btccom/btcpool/issues/5
    //
    // 5 bytes in script: 0x04xxxxxxxx.
    // eg. 0x0402363d58 -> 0x583d3602 = 1480406530 = 2016-11-29 16:02:10
    //
    cbIn.scriptSig << CScriptNum((uint32_t)time(nullptr));

    // put a random hash, just in case. the same as above timestamp.
    const uint256 randHash = GetRandHash();
    cbIn.scriptSig.insert(cbIn.scriptSig.end(), randHash.begin(), randHash.end());

    // pool's info
    cbIn.scriptSig.insert(cbIn.scriptSig.end(),
                          poolCoinbaseInfo_.begin(), poolCoinbaseInfo_.end());

    //
    // zcash/src/main.cpp: CheckTransaction()
    //     if (tx.vin[0].scriptSig.size() < 2 || tx.vin[0].scriptSig.size() > 100)
    //       return state.DoS(100, false, REJECT_INVALID, "bad-cb-length");
    //
    // 100: coinbase script sig max len, range: (2, 100).
    //
    if (cbIn.scriptSig.size() >= 99) {
      cbIn.scriptSig.resize(99);
      LOG(WARNING) << "coinbase input script size over than 100, shold < 100";
    }

    //
    // output[0]: pool payment address
    //
    vector<CTxOut> cbOut;
    cbOut.push_back(CTxOut());
    // pool's address
    cbOut[0].scriptPubKey = GetScriptForDestination(poolPayoutAddr_.Get());

    const CChainParams& chainparams = Params();
    cbOut[0].nValue = GetBlockSubsidy(height_, chainparams.GetConsensus());

    if ((height_ > 0) && (height_ <= chainparams.GetConsensus().GetLastFoundersRewardBlockHeight())) {
      // Founders reward is 20% of the block subsidy
      auto vFoundersReward = cbOut[0].nValue / 5;
      // Take some reward away from us
      cbOut[0].nValue -= vFoundersReward;

      // And give it to the founders
      cbOut.push_back(CTxOut(vFoundersReward, chainparams.GetFoundersRewardScriptAtHeight(height_)));
    }

    // add fees
    cbOut[0].nValue += nFees;

    CMutableTransaction cbtx;
    cbtx.vin.push_back(cbIn);
    cbtx.vout = cbOut;
  }

  CBlock block;
  // push coinbase tx
  block.vtx.push_back(cbtx);
  // push other txs
  for (JsonNode & node : jgbt["transactions"].array()) {
    CTransaction tx;
    DecodeHexTx(tx, node["data"].str());
    block.vtx.push_back(tx);
  }

  // clac merkle root hash, should do this after push all txs
  const uint256 merkleRootHash = block.BuildMerkleTree();

  // update header fields
  block.nVersion       = nVersion;
  block.hashPrevBlock  = prevHash;
  block.hashMerkleRoot = merkleRootHash;
  block.nTime  = nTime;
  block.nBits  = nBits;
  // block.hashReserved && block.nNonce will use default value(empty uint256)

  const uint256 originalHash = block.GetHash();

  // block hex string
  CDataStream ssBlock(SER_NETWORK, PROTOCOL_VERSION);
  ssBlock << block;
  const std::string blockHex = HexStr(ssBlock.begin(), ssBlock.end());

  LOG(INFO) << "GBT height: " << height
  << ", prev_hash: "          << block.hashPrevBlock.ToString()
  << ", coinbase_value: "     << block.vtx[0].vin[0].nValue  // coinbase first output
  << ", bits: "    << Strings::Format("%08x", block.nBits)
  << ", mintime: " << minTime
  << ", version: " << block.nVersion
  << "|0x" << Strings::Format("%08x", block.nVersion)
  << ", original_hash: " << originalHash.ToString();

  const uint32_t nowTs = (uint32_t)time(nullptr);
  //
  // Kafka Message: KAFKA_TOPIC_RAWGBT
  //
  // StratumJob::initFromGbt() and JobMaker::addRawgbt() will decode this message
  //
  return Strings::Format("{\"original_hash\":\"%s\","
                         "\"height\":%d,\"min_time\":%u,\"max_time\":%u,"
                         "\"tx_count\":%d,\"created_at\":%u,\"created_at_str\":\"%s\","
                         "\"block_hex\":\"%s\""
                         "}",
                         originalHash.ToString().c_str(),
                         height, minTime, maxTime,
                         (int32_t)block.vtx.size(),
                         nowTs, date("%F %T", nowTs).c_str(),
                         blockHex.c_str());
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

void GbtMaker::threadListenZcashd() {
  zmq::socket_t subscriber(zmqContext_, ZMQ_SUB);
  subscriber.connect(zmqZcashdAddr_);
  subscriber.setsockopt(ZMQ_SUBSCRIBE,
                        ZCASHD_ZMQ_HASHBLOCK, strlen(ZCASHD_ZMQ_HASHBLOCK));

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
      LOG(ERROR) << "zcash zmq recv exception: " << e.what();
      break;  // break big while
    }
    const string type    = std::string(static_cast<char*>(ztype.data()),    ztype.size());
    const string content = std::string(static_cast<char*>(zcontent.data()), zcontent.size());

    if (type == ZCASHD_ZMQ_HASHBLOCK)
    {
      string hashHex;
      Bin2Hex((const uint8 *)content.data(), content.size(), hashHex);
      LOG(INFO) << ">>>> zcashd recv hashblock: " << hashHex << " <<<<";
      submitRawGbtMsg(false);
    }
    else
    {
      LOG(ERROR) << "unknown message type from zcash: " << type;
    }
  } /* /while */

  subscriber.close();
  LOG(INFO) << "stop thread listen to zcash";
}

void GbtMaker::run() {
  thread threadListenZcashd = thread(&GbtMaker::threadListenZcashd, this);

  while (running_) {
    sleep(1);
    submitRawGbtMsg(true);
  }

  if (threadListenZcashd.joinable())
    threadListenZcashd.join();
}
