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
#include "Stratum.h"

#include "bitcoin/core.h"
#include "bitcoin/hash.h"
#include "bitcoin/script.h"
#include "bitcoin/uint256.h"
#include "bitcoin/util.h"

#include "utilities_js.hpp"
#include "Utils.h"

#include <glog/logging.h>


//////////////////////////////// StratumError ////////////////////////////////
const char * StratumError::toString(int err) {
  switch (err) {
    case NO_ERROR:
      return "no error";

    case JOB_NOT_FOUND:
      return "Job not found (=stale)";
    case DUPLICATE_SHARE:
      return "Duplicate share";
    case LOW_DIFFICULTY:
      return "Low difficulty";
    case UNAUTHORIZED:
      return "Unauthorized worker";
    case NOT_SUBSCRIBED:
      return "Not subscribed";

    case ILLEGAL_METHOD:
      return "Illegal method";
    case ILLEGAL_PARARMS:
      return "Illegal params";
    case IP_BANNED:
      return "Ip banned";
    case INVALID_USERNAME:
      return "Invliad username";
    case INTERNAL_ERROR:
      return "Internal error";
    case TIME_TOO_OLD:
      return "Time too old";
    case TIME_TOO_NEW:
      return "Time too new";

    case UNKNOWN: default:
      return "Unknown";
  }
}

//////////////////////////////// StratumWorker ////////////////////////////////
StratumWorker::StratumWorker(): userId_(0), workerHashId_(0) {}

string StratumWorker::getUserName(const string &fullName) {
  auto pos = fullName.find(".");
  if (pos == fullName.npos) {
    return fullName;
  }
  return fullName.substr(0, pos);
}

void StratumWorker::setUserIDAndNames(const int32_t userId, const string &fullName) {
  userId_ = userId;

  auto pos = fullName.find(".");
  if (pos == fullName.npos) {
    userName_   = fullName;
    workerName_ = "default";
  } else {
    userName_   = fullName.substr(0, pos);
    workerName_ = fullName.substr(pos+1);
  }

  // max length for worker name is 20
  if (workerName_.length() > 20) {
    workerName_.resize(20);
  }

  // calc worker hash id, 64bits
  // https://en.wikipedia.org/wiki/Birthday_attack
  const uint256 workerNameHash = Hash(workerName_.begin(), workerName_.end());
  workerHashId_ = strtoll(workerNameHash.ToString().substr(0, 8).c_str(), 0, 16);
  if (workerHashId_ == 0) {  // zero is kept
    workerHashId_++;
  }

  fullName_ = userName_ + "." + workerName_;
}


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


//////////////////////////////////  StratumJob  ////////////////////////////////
StratumJob::StratumJob(): jobId_(0), height_(0), nVersion_(0), nBits_(0U),
nTime_(0U), minTime_(0U), coinbaseValue_(0) {
}

string StratumJob::serializeToJson() const {
  string merkleBranchStr;
  merkleBranchStr.reserve(merkleBranch_.size() * 64 + 1);
  for (size_t i = 0; i < merkleBranch_.size(); i++) {
    merkleBranchStr.append(merkleBranch_[i].ToString());
  }

  //
  // we use key->value json string, so it's easy to update system
  //
  return Strings::Format("{\"jobId\":%llu,\"gbtHash\":\"%s\""
                         ",\"prevHash\":\"%s\",\"prevHashBeStr\":\"%s\""
                         ",\"height\":%d,\"coinbase1\":\"%s\",\"coinbase2\":\"%s\""
                         ",\"merkleBranch\":\"%s\""
                         ",\"nVersion\":%d,\"nBits\":%u,\"nTime\":%u"
                         ",\"minTime\":%u,\"coinbaseValue\":%lld}",
                         jobId_, gbtHash_.c_str(),
                         prevHash_.ToString().c_str(), prevHashBeStr_.c_str(),
                         height_, coinbase1_.c_str(), coinbase2_.c_str(),
                         // merkleBranch_ could be empty
                         merkleBranchStr.size() ? merkleBranchStr.c_str() : "",
                         nVersion_, nBits_, nTime_,
                         minTime_, coinbaseValue_);
}

bool StratumJob::unserializeFromJson(const char *s, size_t len) {
  JsonNode j;
  if (!JsonNode::parse(s, s + len, j)) {
    return false;
  }
  if (j["jobId"].type()        != Utilities::JS::type::Int ||
      j["gbtHash"].type()      != Utilities::JS::type::Str ||
      j["prevHash"].type()     != Utilities::JS::type::Str ||
      j["prevHashBeStr"].type()!= Utilities::JS::type::Str ||
      j["height"].type()       != Utilities::JS::type::Int ||
      j["coinbase1"].type()    != Utilities::JS::type::Str ||
      j["coinbase2"].type()    != Utilities::JS::type::Str ||
      j["merkleBranch"].type() != Utilities::JS::type::Str ||
      j["nVersion"].type()     != Utilities::JS::type::Int ||
      j["nBits"].type()        != Utilities::JS::type::Int ||
      j["nTime"].type()        != Utilities::JS::type::Int ||
      j["minTime"].type()      != Utilities::JS::type::Int ||
      j["coinbaseValue"].type()!= Utilities::JS::type::Int) {
    LOG(ERROR) << "parse stratum job failure: " << s;
    return false;
  }

  jobId_         = j["jobId"].uint64();
  gbtHash_       = j["gbtHash"].str();
  prevHash_      = uint256(j["prevHash"].str());
  prevHashBeStr_ = j["prevHashBeStr"].str();
  height_        = j["height"].int32();
  coinbase1_     = j["coinbase1"].str();
  coinbase2_     = j["coinbase2"].str();
  nVersion_      = j["nVersion"].int32();
  nBits_         = j["nBits"].uint32();
  nTime_         = j["nTime"].uint32();
  minTime_       = j["minTime"].uint32();
  coinbaseValue_ = j["coinbaseValue"].int64();

  const string merkleBranchStr = j["merkleBranch"].str();
  const size_t merkleBranchCount = merkleBranchStr.length() / 64;
  merkleBranch_.resize(merkleBranchCount);
  for (size_t i = 0; i < merkleBranchCount; i++) {
    merkleBranch_[i] = uint256(merkleBranchStr.substr(i*64, 64));
  }

  BitsToTarget(nBits_, networkTarget_);

  return true;
}

bool StratumJob::initFromGbt(const char *gbt, const string &poolCoinbaseInfo,
                             const CBitcoinAddress &poolPayoutAddr,
                             const uint32_t blockVersion) {
  uint256 gbtHash = Hash(gbt, gbt + strlen(gbt));
  JsonNode r;
  if (!JsonNode::parse(gbt, gbt + strlen(gbt), r)) {
    LOG(ERROR) << "decode gbt json fail";
    return false;
  }
  JsonNode jgbt = r["result"];

  // jobId: timestamp + gbtHash, we need to make sure jobId is unique in a some time
  // jobId can convert to uint64_t
  const string jobIdStr = Strings::Format("%08x%s", (uint32_t)time(nullptr),
                                          gbtHash.ToString().substr(0, 8).c_str());
  assert(jobIdStr.length() == 16);
  jobId_ = stoull(jobIdStr, 0, 16/* hex */);

  gbtHash_ = gbtHash.ToString();

  // height etc.
  // fields in gbt json has already checked by GbtMaker
  prevHash_ = uint256(jgbt["previousblockhash"].str());
  height_   = jgbt["height"].int32();
  if (blockVersion != 0) {
    nVersion_ = blockVersion;
  } else {
    nVersion_ = jgbt["version"].uint32();
  }
  nBits_    = jgbt["bits"].uint32_hex();
  nTime_    = jgbt["curtime"].uint32();
  minTime_  = jgbt["mintime"].uint32();
  coinbaseValue_ = jgbt["coinbasevalue"].int64();
  BitsToTarget(nBits_, networkTarget_);

  // previous block hash
  // we need to convert to little-endian
  // 00000000000000000328e9fea9914ad83b7404a838aa66aefb970e5689c2f63d
  // 89c2f63dfb970e5638aa66ae3b7404a8a9914ad80328e9fe0000000000000000
  for (int i = 0; i < 8; i++) {
    uint32 a = *(uint32 *)(BEGIN(prevHash_) + i * 4);
    a = HToBe(a);
    prevHashBeStr_ += HexStr(BEGIN(a), END(a));
  }

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
    makeMerkleBranch(vtxhashs, merkleBranch_);
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
