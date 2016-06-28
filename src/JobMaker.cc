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
#include "JobMaker.h"

#include <stdlib.h>

#include <glog/logging.h>
#include <librdkafka/rdkafka.h>

#include "bitcoin/core.h"
#include "bitcoin/hash.h"
#include "bitcoin/script.h"
#include "bitcoin/uint256.h"
#include "bitcoin/util.h"

#include "utilities_js.hpp"
#include "Utils.h"


static
void makeMerkleBranch(const vector<uint256> &vtxhashs, vector<uint256> &steps) {
  if (vtxhashs.size() == 0) {
    return;
  }
  vector<uint256> hashs(vtxhashs.begin(), vtxhashs.end());
  while (hashs.size() > 1) {
    // put first element
    steps.push_back(*hashs.begin());
    if (hashs.size() % 2 == 0) {
      // if even, push_back the end one, size should be an odd number.
      // because we ignore the coinbase tx when make merkle branch.
      hashs.push_back(*hashs.rbegin());
    }
    // ignore the first one than merge two
    for (size_t i = 0; i < (hashs.size() - 1) / 2; i++) {
      // Hash = Double SHA256
      hashs[i] = Hash(BEGIN(hashs[i*2 + 1]), END(hashs[i*2 + 1]),
                      BEGIN(hashs[i*2 + 2]), END(hashs[i*2 + 2]));
    }
    hashs.resize((hashs.size() - 1) / 2);
  }
  assert(hashs.size() == 1);
  steps.push_back(*hashs.begin());  // put the last one
}


//
// https://github.com/bitcoin/bips/blob/master/bip-0034.mediawiki
//
// The format of the height is "serialized CScript" -- first byte is number of
// bytes in the number (will be 0x03 on main net for the next 300 or so years),
// following bytes are little-endian representation of the number. Height is the
// height of the mined block in the block chain, where the genesis block is
// height zero (0).
static
void _EncodeUNum(std::vector<unsigned char> *in, uint32 v) {
  if (v == 0) {
    in->push_back((unsigned char)0);
  } else if (v <= 0xffU) {
    in->push_back((unsigned char)1);
    in->push_back((unsigned char)(v & 0xff));
  } else if (v <= 0xffffU) {
    in->push_back((unsigned char)2);
    in->push_back((unsigned char)(v & 0xff));
    in->push_back((unsigned char)((v >> 8)& 0xff));
  } else if (v <= 0xffffffU) {
    in->push_back((unsigned char)3);
    in->push_back((unsigned char)(v & 0xff));
    in->push_back((unsigned char)((v >> 8)& 0xff));
    in->push_back((unsigned char)((v >> 16)& 0xff));
  } else {
    in->push_back((unsigned char)4);
    in->push_back((unsigned char)(v & 0xff));
    in->push_back((unsigned char)((v >> 8)& 0xff));
    in->push_back((unsigned char)((v >> 16)& 0xff));
    in->push_back((unsigned char)((v >> 24)& 0xff));
  }
}

static
int64 findExtraNonceStart(const vector<char> &coinbaseOriTpl,
                          const vector<char> &placeHolder) {
  // find for the end
  for (int64 i = coinbaseOriTpl.size() - placeHolder.size(); i >= 0; i--) {
    if (memcmp(&coinbaseOriTpl[i], &placeHolder[0], placeHolder.size()) == 0) {
      return i;
    }
  }
  return -1;
}


/////////////////////////////////  StratumJobMsg  //////////////////////////////
StratumJobMsg::StratumJobMsg(): height_(0), nVersion_(0), nBits_(0U),
nTime_(0U), minTime_(0U), coinbaseValue_(0) {
}

string StratumJobMsg::toString() const {
  // we use key->value json string, so it's easy to update system
  return Strings::Format("{\"jobID\":\"%s\",\"gbtHash\":\"%s\",\"prevHashBe\":\"%s\""
                         ",\"height\":%d,\"coinbase1\":\"%s\",\"coinbase2\":\"%s\""
                         ",\"merkleBranch\":\"%s\""
                         ",\"nVersion\":%d,\"nBits\":%u,\"nTime\":%u"
                         ",\"minTime\":%u,\"coinbaseValue\":%lld}",
                         jobID_.c_str(), gbtHash_.c_str(), prevHashBeStr_.c_str(),
                         height_, coinbase1_.c_str(), coinbase2_.c_str(),
                         // merkleBranch_ could be empty
                         merkleBranch_.size() ? merkleBranch_.c_str() : "",
                         nVersion_, nBits_, nTime_,
                         minTime_, coinbaseValue_);
}

bool StratumJobMsg::parse(const char *s) {
  JsonNode j;
  if (!JsonNode::parse(s, s + strlen(s), j)) {
    return false;
  }
  if (j["jobID"].type()        != Utilities::JS::type::Str ||
      j["gbtHash"].type()      != Utilities::JS::type::Str ||
      j["prevHashBe"].type()   != Utilities::JS::type::Str ||
      j["height"].type()       != Utilities::JS::type::Int ||
      j["coinbase1"].type()    != Utilities::JS::type::Str ||
      j["coinbase2"].type()    != Utilities::JS::type::Str ||
      j["merkleBranch"].type() != Utilities::JS::type::Str ||
      j["nVersion"].type()     != Utilities::JS::type::Int ||
      j["nBits"].type()        != Utilities::JS::type::Int ||
      j["nTime"].type()        != Utilities::JS::type::Int ||
      j["minTime"].type()      != Utilities::JS::type::Int ||
      j["coinbaseValue"].type()!= Utilities::JS::type::Int) {
    LOG(ERROR) << "parse stratum job message failure: " << s;
    return false;
  }

  jobID_         = j["jobID"].str();
  gbtHash_       = j["gbtHash"].str();
  prevHashBeStr_ = j["prevHashBe"].str();
  height_        = j["height"].int32();
  coinbase1_     = j["coinbase1"].str();
  coinbase2_     = j["coinbase2"].str();
  merkleBranch_  = j["merkleBranch"].str();
  nVersion_      = j["nVersion"].int32();
  nBits_         = j["nBits"].uint32();
  nTime_         = j["nTime"].uint32();
  minTime_       = j["minTime"].uint32();
  coinbaseValue_ = j["coinbaseValue"].int64();

  return true;
}

bool StratumJobMsg::initFromGbt(const char *gbt, const string &poolCoinbaseInfo,
                                const CBitcoinAddress &poolPayoutAddr) {
  uint256 gbtHash = Hash(gbt, gbt + strlen(gbt));
  JsonNode r;
  if (!JsonNode::parse(gbt, gbt + strlen(gbt), r)) {
    LOG(ERROR) << "decode gbt json fail";
    return false;
  }
  JsonNode jgbt = r["result"];

  // jobID: timestamp + gbtHash, we need to make sure jobID is unique in a some time
  // jobID can convert to uint64_t
  jobID_ = Strings::Format("%08x%s", (uint32_t)time(nullptr),
                           gbtHash.ToString().substr(0, 4).c_str());
  gbtHash_ = gbtHash.ToString();

  // previous block hash
  // we need to convert to little-endian
  // 00000000000000000328e9fea9914ad83b7404a838aa66aefb970e5689c2f63d
  // 89c2f63dfb970e5638aa66ae3b7404a8a9914ad80328e9fe0000000000000000
  {
    const uint256 prevBlockHash(jgbt["previousblockhash"].str());
    for (int i = 0; i < 8; i++) {
      uint32 a = *(uint32 *)(BEGIN(prevBlockHash) + i * 4);
      a = HToBe(a);
      prevHashBeStr_ += HexStr(BEGIN(a), END(a));
    }
  }

  // height etc.
  height_   = jgbt["height"].int32();
  nVersion_ = jgbt["version"].uint32();
  nBits_    = jgbt["bits"].uint32_hex();
  nTime_    = jgbt["curtime"].uint32();
  minTime_  = jgbt["mintime"].uint32();
  coinbaseValue_ = jgbt["coinbasevalue"].int64();

  // merkle branch, merkleBranch_ could be empty
  {
    // read txs hash/data
    vector<uint256> vtxhashs;  // txs without coinbase
    for (JsonNode & node : jgbt["transactions"].array()) {
      CTransaction tx;
      DecodeHexTx(tx, node["data"].str());
      vtxhashs.push_back(tx.GetHash());
    }
    // make merkleSteps and merkle branch
    vector<uint256> merkleSteps;
    makeMerkleBranch(vtxhashs, merkleSteps);
    merkleBranch_.reserve(merkleSteps.size() * 64 + 1);
    for (size_t i = 0; i < merkleSteps.size(); i++) {
      string merklStr;
      // Do NOT use GetHex(), because of the order you need to use Bin2Hex()
      // just dump the memory to hex str
      Bin2Hex(merkleSteps[i].begin(), 32, merklStr);
      merkleBranch_.append(merklStr);
    }
  }

  // make coinbase1 & coinbase2
  {
    CTxIn cbIn;
    _EncodeUNum(dynamic_cast<vector<unsigned char> *>(&cbIn.scriptSig), (uint32_t)height_);
    cbIn.scriptSig.insert(cbIn.scriptSig.end(), poolCoinbaseInfo.begin(), poolCoinbaseInfo.end());
    // 100: coinbase script sig max len, range: (2, 100]
    //  12: extra nonce1 (4bytes) + extra nonce2 (8bytes)
    const vector<char> placeHolder(4 + 8, 0xEE);
    const size_t maxScriptSigLen = 100 - placeHolder.size();
    if (cbIn.scriptSig.size() > maxScriptSigLen) {
      cbIn.scriptSig.resize(maxScriptSigLen);
    }
    // pub extra nonce place holder
    cbIn.scriptSig.insert(cbIn.scriptSig.end(), placeHolder.begin(), placeHolder.end());
    assert(cbIn.scriptSig.size() <= 100);

    vector<CTxOut> cbOut;
    cbOut.push_back(CTxOut());
    cbOut[0].nValue = coinbaseValue_;
    cbOut[0].scriptPubKey.SetDestination(poolPayoutAddr.Get());

    CTransaction cbtx;
    cbtx.vin.push_back(cbIn);
    cbtx.vout = cbOut;
    assert(cbtx.nVersion == 1);  // current our block version is 1

    vector<char> coinbaseTpl;
    CDataStream ssTx(SER_NETWORK, BITCOIN_PROTOCOL_VERSION);
    ssTx << cbtx;  // put coinbase CTransaction to CDataStream
    ssTx.GetAndClear(coinbaseTpl);

    const int64 extraNonceStart = findExtraNonceStart(coinbaseTpl, placeHolder);
    coinbase1_ = HexStr(&coinbaseTpl[0], &coinbaseTpl[extraNonceStart]);
    coinbase2_ = HexStr(&coinbaseTpl[extraNonceStart + placeHolder.size()],
                        &coinbaseTpl[coinbaseTpl.size()]);
  }

  return true;
}


///////////////////////////////////  JobMaker  /////////////////////////////////
JobMaker::JobMaker(const string &kafkaBrokers,  uint32_t stratumJobInterval,
                   const string &payoutAddr, uint32_t gbtLifeTime):
running_(true),
kafkaBrokers_(kafkaBrokers),
kafkaProducer_(kafkaBrokers_.c_str(), KAFKA_TOPIC_STRATUM_JOB, RD_KAFKA_PARTITION_UA/* partition */),
kafkaConsumer_(kafkaBrokers_.c_str(), KAFKA_TOPIC_RAWGBT,      0/* partition */, KAFKA_GROUP_JOBMAKER/*group id*/),
currBestHeight_(0), stratumJobInterval_(stratumJobInterval),
poolPayoutAddr_(payoutAddr), kGbtLifeTime_(gbtLifeTime)
{
  poolCoinbaseInfo_ = "/BTC.COM/";
}

JobMaker::~JobMaker() {}

void JobMaker::stop() {
  if (!running_) {
    return;
  }
  running_ = false;
  LOG(INFO) << "stop jobmaker";
}

bool JobMaker::init() {
  const int32_t consumeLatestN = 20;
  // check pool payout address
  if (!poolPayoutAddr_.IsValid()) {
    LOG(ERROR) << "invalid pool payout address";
    return false;
  }

  /* setup kafka */
  if (!kafkaProducer_.setup()) {
    LOG(ERROR) << "kafka producer setup failure";
    return false;
  }
  if (!kafkaProducer_.checkAlive()) {
    LOG(ERROR) << "kafka producer is NOT alive";
    return false;
  }
  // consumer offset: latest N messages
  if (!kafkaConsumer_.setup(RD_KAFKA_OFFSET_TAIL(consumeLatestN))) {
    LOG(ERROR) << "kafka consumer setup failure";
    return false;
  }
  if (!kafkaConsumer_.checkAlive()) {
    LOG(ERROR) << "kafka consumer is NOT alive";
    return false;
  }

  // read latest gbtmsg from kafka
  LOG(INFO) << "consume latest rawgbt message from kafka...";
  // sleep 3 seconds, wait for the latest N messages transfer from broker to client
  sleep(3);
  // consumer latest N messages
  for (int32_t i = 0; i < consumeLatestN; i++) {
    rd_kafka_message_t *rkmessage;
    rkmessage = kafkaConsumer_.consumer(5000/* timeout ms */);
    if (rkmessage == nullptr) {
      break;
    }
    consumeRawGbtMsg(rkmessage, false);
    rd_kafka_message_destroy(rkmessage);
  }
  LOG(INFO) << "consume latest rawgbt messages done";
  checkAndSendStratumJob();

  return true;
}

void JobMaker::consumeRawGbtMsg(rd_kafka_message_t *rkmessage, bool needToSend) {
  // check error
  if (rkmessage->err) {
    if (rkmessage->err == RD_KAFKA_RESP_ERR__PARTITION_EOF) {
      // Reached the end of the topic+partition queue on the broker.
      // Not really an error.
//      LOG(INFO) << "consumer reached end of " << rd_kafka_topic_name(rkmessage->rkt)
//      << "[" << rkmessage->partition << "] "
//      << " message queue at offset " << rkmessage->offset;
      // acturlly 
      return;
    }

    LOG(ERROR) << "consume error for topic " << rd_kafka_topic_name(rkmessage->rkt)
    << "[" << rkmessage->partition << "] offset " << rkmessage->offset
    << ": " << rd_kafka_message_errstr(rkmessage);

    if (rkmessage->err == RD_KAFKA_RESP_ERR__UNKNOWN_PARTITION ||
        rkmessage->err == RD_KAFKA_RESP_ERR__UNKNOWN_TOPIC) {
      LOG(FATAL) << "consume fatal";
      stop();
    }
    return;
  }

  LOG(INFO) << "received rawgbt message, len: " << rkmessage->len;
  addRawgbt((const char *)rkmessage->payload, rkmessage->len);

  if (needToSend) {
    checkAndSendStratumJob();
  }
}

void JobMaker::run() {
  const int32_t timeoutMs = 1000;

  while (running_) {
    rd_kafka_message_t *rkmessage;
    rkmessage = kafkaConsumer_.consumer(timeoutMs);
    if (rkmessage == nullptr) /* timeout */
      continue;

    consumeRawGbtMsg(rkmessage, true);

    /* Return message to rdkafka */
    rd_kafka_message_destroy(rkmessage);

  } /* /while */
}

void JobMaker::addRawgbt(const char *str, size_t len) {
  JsonNode r;
  if (!JsonNode::parse(str, str + len, r)) {
    LOG(ERROR) << "parse rawgbt message to json fail";
    return;
  }
  if (r["created_at_ts"].type() != Utilities::JS::type::Int ||
      r["block_template_base64"].type() != Utilities::JS::type::Str ||
      r["gbthash"].type() != Utilities::JS::type::Str) {
    LOG(ERROR) << "invalid rawgbt: missing fields";
    return;
  }

  const uint32_t gbtTime = r["created_at_ts"].uint32();
  const int64_t timeDiff = (int64_t)time(nullptr) - (int64_t)gbtTime;
  if (labs(timeDiff) >= 60) {
    LOG(ERROR) << "rawgbt diff time is more than 60, ingore it";
    return;  // time diff too large, there must be some problems, so ignore it
  }
  if (labs(timeDiff) >= 3) {
    LOG(WARNING) << "rawgbt diff time is too large: " << timeDiff << " seconds";
  }

  const string gbt = DecodeBase64(r["block_template_base64"].str());
  assert(gbt.length() > 64);  // valid gbt string's len at least 64 bytes

  JsonNode nodeGbt;
  if (!JsonNode::parse(gbt.c_str(), gbt.c_str() + gbt.length(), nodeGbt)) {
    LOG(ERROR) << "parse gbt message to json fail";
    return;
  }
  assert(nodeGbt["result"]["height"].type() == Utilities::JS::type::Int);
  const uint32_t height = nodeGbt["result"]["height"].uint32();

  {
    ScopeLock sl(lock_);
    const uint64_t key = ((uint64_t)gbtTime << 32) + (uint64_t)height;
    rawgbtMap_.insert(std::make_pair(key, gbt));
  }
  LOG(INFO) << "add rawgbt, height: "<< height << ", gbthash: "
  << r["gbthash"].str().substr(0, 16) << "..., gbtTime(UTC): " << date("%F %T", gbtTime);
}

void JobMaker::clearTimeoutGbt() {
  // Maps (and sets) are sorted, so the first element is the smallest,
  // and the last element is the largest.
  while (rawgbtMap_.size()) {
    auto itr = rawgbtMap_.begin();
    const uint32_t ts = (uint32_t)(itr->first >> 32);
    if (ts + kGbtLifeTime_ > time(nullptr)) {
      break;
    }
    // remove the oldest gbt
//    LOG(INFO) << "remove oldest rawgbt: " << date("%F %T", ts) << "|" << ts;
    rawgbtMap_.erase(itr);
  }
}

void JobMaker::sendStratumJob(const char *gbt) {
  StratumJobMsg sjobMsg;
  if (!sjobMsg.initFromGbt(gbt, poolCoinbaseInfo_, poolPayoutAddr_)) {
    LOG(ERROR) << "init stratum job message from gbt str fail";
    return;
  }
  const string msg = sjobMsg.toString();

  // sent to kafka
  kafkaProducer_.produce(msg.data(), msg.size());

  // set last send time
  lastJobSendTime_ = (uint32_t)time(nullptr);

  LOG(INFO) << "--------producer stratum job, jobID: " << sjobMsg.jobID_
  << ", height: " << sjobMsg.height_ << "--------";
  LOG(INFO) << "sjob: " << msg;
}

void JobMaker::findNewBestHeight(std::map<uint64_t/* height + ts */, const char *> *gbtByHeight) {
  for (const auto &itr : rawgbtMap_) {
    const uint32_t timestamp = (uint32_t)((itr.first >> 32) & 0x00000000FFFFFFFFULL);
    const uint32_t height    = (uint32_t)(itr.first         & 0x00000000FFFFFFFFULL);

    // using Map to sort by: height + timestamp
    const uint64_t key = ((uint64_t)height << 32) + (uint64_t)timestamp;
    gbtByHeight->insert(std::make_pair(key, itr.second.c_str()));
  }
}

bool JobMaker::isReachTimeout() {
  if (lastJobSendTime_ + stratumJobInterval_ <= time(nullptr)) {
    return true;
  }
  return false;
}

void JobMaker::checkAndSendStratumJob() {
  ScopeLock sl(lock_);
  if (rawgbtMap_.size() == 0) {
    return;
  }
  std::map<uint64_t/* height + ts */, const char *> gbtByHeight;

  // clear expired gbt first
  clearTimeoutGbt();

  // we need to build 'gbtByHeight' first
  findNewBestHeight(&gbtByHeight);
  bool isFindNewHeight = false;

  // key: height + timestamp
  const uint64_t bestKey = gbtByHeight.rbegin()->first;
  const uint32_t bestHeight = (uint32_t)((bestKey >> 32) & 0x00000000FFFFFFFFULL);
  if (bestHeight != currBestHeight_) {
    LOG(INFO) << ">>>> found new best height: " << bestHeight
    << ", curr: " << currBestHeight_ << " <<<<";
    isFindNewHeight = true;
    currBestHeight_ = bestHeight;
  }

  if (isFindNewHeight || isReachTimeout()) {
    sendStratumJob(gbtByHeight.rbegin()->second);
  }
}


