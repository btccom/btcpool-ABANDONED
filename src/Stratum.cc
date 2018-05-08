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

#include <core_io.h>
#include <hash.h>
#include <script/script.h>
#include <uint256.h>
#include <util.h>
#include <pubkey.h>

#include "Utils.h"

#include <glog/logging.h>


// filter for woker name and miner agent
string filterWorkerName(const string &workerName) {
  string s;
  s.reserve(workerName.size());

  for (const auto &c : workerName) {
    if (('a' <= c && c <= 'z') ||
        ('A' <= c && c <= 'Z') ||
        ('0' <= c && c <= '9') ||
        c == '-' || c == '.' || c == '_' || c == ':' ||
        c == '|' || c == '^' || c == '/') {
      s += c;
    }
  }

  return s;
}


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
      return "Invalid username";
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

void StratumWorker::reset() {
  userId_ = 0;
  workerHashId_ = 0;

  fullName_.clear();
  userName_.clear();
  workerName_.clear();
}

string StratumWorker::getUserName(const string &fullName) const {
  auto pos = fullName.find(".");
  if (pos == fullName.npos) {
    return fullName;
  }
  return fullName.substr(0, pos);
}

void StratumWorker::setUserIDAndNames(const int32_t userId, const string &fullName) {
  reset();
  userId_ = userId;

  auto pos = fullName.find(".");
  if (pos == fullName.npos) {
    userName_   = fullName;
  } else {
    userName_   = fullName.substr(0, pos);
    workerName_ = fullName.substr(pos+1);
  }

  // the worker name will insert to DB, so must be filter
  workerName_ = filterWorkerName(workerName_);

  // max length for worker name is 20
  if (workerName_.length() > 20) {
    workerName_.resize(20);
  }

  if (workerName_.empty()) {
    workerName_ = DEFAULT_WORKER_NAME;
  }

  workerHashId_ = calcWorkerId(workerName_);
  fullName_ = userName_ + "." + workerName_;
}

int64_t StratumWorker::calcWorkerId(const string &workerName) {
  int64_t workerHashId = 0;

  // calc worker hash id, 64bits
  // https://en.wikipedia.org/wiki/Birthday_attack
  const uint256 workerNameHash = Hash(workerName.begin(), workerName.end());

  // need to convert to uint64 first than copy memory
  const uint64_t tmpId = strtoull(workerNameHash.ToString().substr(0, 16).c_str(),
                                  nullptr, 16);
  memcpy((uint8_t *)&workerHashId, (uint8_t *)&tmpId, 8);

  if (workerHashId == 0) {  // zero is kept
    workerHashId++;
  }

  return workerHashId;
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
nTime_(0U), minTime_(0U), coinbaseValue_(0), nmcAuxBits_(0u), isRskCleanJob_(false) {
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
  return Strings::Format("{\"jobId\":%" PRIu64",\"gbtHash\":\"%s\""
                         ",\"prevHash\":\"%s\",\"prevHashBeStr\":\"%s\""
                         ",\"height\":%d,\"coinbase1\":\"%s\",\"coinbase2\":\"%s\""
                         ",\"merkleBranch\":\"%s\""
                         ",\"nVersion\":%d,\"nBits\":%u,\"nTime\":%u"
                         ",\"minTime\":%u,\"coinbaseValue\":%lld,\"witnessCommitment\":\"%s\""
                         // namecoin, optional
                         ",\"nmcBlockHash\":\"%s\",\"nmcBits\":%u,\"nmcHeight\":%d"
                         ",\"nmcRpcAddr\":\"%s\",\"nmcRpcUserpass\":\"%s\""
                         // rsk 
                         ",\"rskBlockHashForMergedMining\":\"%s\",\"rskNetworkTarget\":\"0x%s\""
                         ",\"rskFeesForMiner\":\"%s\""
                         ",\"rskdRpcAddress\":\"%s\",\"rskdRpcUserPwd\":\"%s\""
                         ",\"isRskCleanJob\":%s"
                         "}",
                         jobId_, gbtHash_.c_str(),
                         prevHash_.ToString().c_str(), prevHashBeStr_.c_str(),
                         height_, coinbase1_.c_str(), coinbase2_.c_str(),
                         // merkleBranch_ could be empty
                         merkleBranchStr.size() ? merkleBranchStr.c_str() : "",
                         nVersion_, nBits_, nTime_,
                         minTime_, coinbaseValue_,
                         witnessCommitment_.size() ? witnessCommitment_.c_str() : "",
                         // nmc
                         nmcAuxBlockHash_.ToString().c_str(),
                         nmcAuxBits_, nmcHeight_,
                         nmcRpcAddr_.size()     ? nmcRpcAddr_.c_str()     : "",
                         nmcRpcUserpass_.size() ? nmcRpcUserpass_.c_str() : "",
                         // rsk
                         blockHashForMergedMining_.size() ? blockHashForMergedMining_.c_str() : "",
                         rskNetworkTarget_.GetHex().c_str(),
                         feesForMiner_.size()             ? feesForMiner_.c_str()             : "",
                         rskdRpcAddress_.size()           ? rskdRpcAddress_.c_str()           : "",
                         rskdRpcUserPwd_.c_str()          ? rskdRpcUserPwd_.c_str()           : "",
                         isRskCleanJob_ ? "true" : "false");
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
  prevHash_      = uint256S(j["prevHash"].str());
  prevHashBeStr_ = j["prevHashBeStr"].str();
  height_        = j["height"].int32();
  coinbase1_     = j["coinbase1"].str();
  coinbase2_     = j["coinbase2"].str();
  nVersion_      = j["nVersion"].int32();
  nBits_         = j["nBits"].uint32();
  nTime_         = j["nTime"].uint32();
  minTime_       = j["minTime"].uint32();
  coinbaseValue_ = j["coinbaseValue"].int64();

  // witnessCommitment, optional
  // default_witness_commitment must be at least 38 bytes
  if (j["default_witness_commitment"].type() == Utilities::JS::type::Str &&
      j["default_witness_commitment"].str().length() >= 38*2) {
    witnessCommitment_ = j["default_witness_commitment"].str();
  }

  //
  // namecoin, optional
  //
  if (j["nmcBlockHash"].type()   == Utilities::JS::type::Str &&
      j["nmcBits"].type()        == Utilities::JS::type::Int &&
      j["nmcHeight"].type()      == Utilities::JS::type::Int &&
      j["nmcRpcAddr"].type()     == Utilities::JS::type::Str &&
      j["nmcRpcUserpass"].type() == Utilities::JS::type::Str) {
    nmcAuxBlockHash_ = uint256S(j["nmcBlockHash"].str());
    nmcAuxBits_      = j["nmcBits"].uint32();
    nmcHeight_       = j["nmcHeight"].int32();
    nmcRpcAddr_      = j["nmcRpcAddr"].str();
    nmcRpcUserpass_  = j["nmcRpcUserpass"].str();
    BitsToTarget(nmcAuxBits_, nmcNetworkTarget_);
  }

  //
  // rsk, optional
  //
  if (j["rskBlockHashForMergedMining"].type()   == Utilities::JS::type::Str &&
      j["rskNetworkTarget"].type()              == Utilities::JS::type::Str &&
      j["rskFeesForMiner"].type()               == Utilities::JS::type::Str &&
      j["rskdRpcAddress"].type()                == Utilities::JS::type::Str &&
      j["rskdRpcUserPwd"].type()                == Utilities::JS::type::Str &&
      j["isRskCleanJob"].type()                 == Utilities::JS::type::Bool) {
    blockHashForMergedMining_ = j["rskBlockHashForMergedMining"].str();
    rskNetworkTarget_         = uint256S(j["rskNetworkTarget"].str());
    feesForMiner_             = j["rskFeesForMiner"].str();
    rskdRpcAddress_           = j["rskdRpcAddress"].str();
    rskdRpcUserPwd_           = j["rskdRpcUserPwd"].str();
    isRskCleanJob_            = j["isRskCleanJob"].boolean();
  }

  const string merkleBranchStr = j["merkleBranch"].str();
  const size_t merkleBranchCount = merkleBranchStr.length() / 64;
  merkleBranch_.resize(merkleBranchCount);
  for (size_t i = 0; i < merkleBranchCount; i++) {
    merkleBranch_[i] = uint256S(merkleBranchStr.substr(i*64, 64));
  }

  BitsToTarget(nBits_, networkTarget_);

  return true;
}

bool StratumJob::initFromGbt(const char *gbt, const string &poolCoinbaseInfo,
                             const CTxDestination &poolPayoutAddr,
                             const uint32_t blockVersion,
                             const string &nmcAuxBlockJson,
                             const RskWork &latestRskBlockJson) {
  uint256 gbtHash = Hash(gbt, gbt + strlen(gbt));
  JsonNode r;
  if (!JsonNode::parse(gbt, gbt + strlen(gbt), r)) {
    LOG(ERROR) << "decode gbt json fail: >" << gbt << "<";
    return false;
  }
  JsonNode jgbt = r["result"];

  // jobId: timestamp + gbtHash, we need to make sure jobId is unique in a some time
  // jobId can convert to uint64_t
  const string jobIdStr = Strings::Format("%08x%s", (uint32_t)time(nullptr),
                                          gbtHash.ToString().substr(0, 8).c_str());
  assert(jobIdStr.length() == 16);
  jobId_ = strtoull(jobIdStr.c_str(), nullptr, 16/* hex */);

  gbtHash_ = gbtHash.ToString();

  // height etc.
  // fields in gbt json has already checked by GbtMaker
  prevHash_ = uint256S(jgbt["previousblockhash"].str());
  height_   = jgbt["height"].int32();
  if (blockVersion != 0) {
    nVersion_ = blockVersion;
  } else {
    nVersion_ = jgbt["version"].uint32();
  }
  nBits_         = jgbt["bits"].uint32_hex();
  nTime_         = jgbt["curtime"].uint32();
  minTime_       = jgbt["mintime"].uint32();
  coinbaseValue_ = jgbt["coinbasevalue"].int64();

  // default_witness_commitment must be at least 38 bytes
  if (jgbt["default_witness_commitment"].type() == Utilities::JS::type::Str &&
      jgbt["default_witness_commitment"].str().length() >= 38*2) {
    witnessCommitment_ = jgbt["default_witness_commitment"].str();
  }
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

  bool isLightVersion = jgbt["job_id"].type() == Utilities::JS::type::Str;
  // merkle branch, merkleBranch_ could be empty
  if(isLightVersion)
  {
    auto& gbtMerkle = jgbt["merkle"].array();
    for(auto& mHex : gbtMerkle)
    {
      uint256 m;
      m.SetHex(mHex.str().c_str());
      merkleBranch_.push_back(m);
    }
  }
  else
  {
    // read txs hash/data
    vector<uint256> vtxhashs;  // txs without coinbase
    for (JsonNode & node : jgbt["transactions"].array()) {
      CMutableTransaction tx;
      DecodeHexTx(tx, node["data"].str());
      vtxhashs.push_back(MakeTransactionRef(std::move(tx))->GetHash());
    }
    // make merkleSteps and merkle branch
    makeMerkleBranch(vtxhashs, merkleBranch_);
  }

  //
  // namecoin merged mining
  //
  if (!nmcAuxBlockJson.empty()) {
    do {
      JsonNode jNmcAux;
      if (!JsonNode::parse(nmcAuxBlockJson.c_str(),
                           nmcAuxBlockJson.c_str() + nmcAuxBlockJson.length(),
                           jNmcAux)) {
        LOG(ERROR) << "decode nmc auxblock json fail: >" << nmcAuxBlockJson << "<";
        break;
      }
      // check fields created_at_ts
      if (jNmcAux["created_at_ts"].type() != Utilities::JS::type::Int ||
          jNmcAux["hash"].type()          != Utilities::JS::type::Str ||
          jNmcAux["merkle_size"].type()   != Utilities::JS::type::Int ||
          jNmcAux["merkle_nonce"].type()  != Utilities::JS::type::Int ||
          jNmcAux["height"].type()        != Utilities::JS::type::Int ||
          jNmcAux["bits"].type()          != Utilities::JS::type::Str ||
          jNmcAux["rpc_addr"].type()      != Utilities::JS::type::Str ||
          jNmcAux["rpc_userpass"].type()  != Utilities::JS::type::Str) {
        LOG(ERROR) << "nmc auxblock fields failure";
        break;
      }
      // check timestamp
      if (jNmcAux["created_at_ts"].uint32() + 60u < time(nullptr)) {
        LOG(ERROR) << "too old nmc auxblock: " << date("%F %T", jNmcAux["created_at_ts"].uint32());
        break;
      }

      // set nmc aux info
      nmcAuxBlockHash_   = uint256S(jNmcAux["hash"].str());
      nmcAuxMerkleSize_  = jNmcAux["merkle_size"].int32();
      nmcAuxMerkleNonce_ = jNmcAux["merkle_nonce"].int32();
      nmcAuxBits_        = jNmcAux["bits"].uint32_hex();
      nmcHeight_         = jNmcAux["height"].int32();
      nmcRpcAddr_        = jNmcAux["rpc_addr"].str();
      nmcRpcUserpass_    = jNmcAux["rpc_userpass"].str();
      BitsToTarget(nmcAuxBits_, nmcNetworkTarget_);
    } while (0);
  }
  
  //
  // rsk merged mining
  //
  if (latestRskBlockJson.isInitialized()) {

    // set rsk info
    blockHashForMergedMining_ = latestRskBlockJson.getBlockHash();
    rskNetworkTarget_ = uint256S(latestRskBlockJson.getTarget());
    feesForMiner_ = latestRskBlockJson.getFees();
    rskdRpcAddress_ = latestRskBlockJson.getRpcAddress();
    rskdRpcUserPwd_ = latestRskBlockJson.getRpcUserPwd();
    isRskCleanJob_ = latestRskBlockJson.getIsCleanJob();
  }

  // make coinbase1 & coinbase2
  {
    CTxIn cbIn;
    //
    // block height, 4 bytes in script: 0x03xxxxxx
    // https://github.com/bitcoin/bips/blob/master/bip-0034.mediawiki
    // https://github.com/bitcoin/bitcoin/pull/1526
    //
    cbIn.scriptSig = CScript();
    cbIn.scriptSig << (uint32_t)height_;

    // add current timestamp to coinbase tx input, so if the block's merkle root
    // hash is the same, there's no risk for miners to calc the same space.
    // https://github.com/btccom/btcpool/issues/5
    //
    // 5 bytes in script: 0x04xxxxxxxx.
    // eg. 0x0402363d58 -> 0x583d3602 = 1480406530 = 2016-11-29 16:02:10
    //
    cbIn.scriptSig << CScriptNum((uint32_t)time(nullptr));

    // pool's info
    cbIn.scriptSig.insert(cbIn.scriptSig.end(),
                          poolCoinbaseInfo.begin(), poolCoinbaseInfo.end());

    //
    // put namecoin merged mining info, 44 bytes
    // https://en.bitcoin.it/wiki/Merged_mining_specification
    //
    if (nmcAuxBits_ != 0u) {
      string merkleSize, merkleNonce;
      Bin2Hex((uint8_t *)&nmcAuxMerkleSize_,  4, merkleSize);
      Bin2Hex((uint8_t *)&nmcAuxMerkleNonce_, 4, merkleNonce);
      string mergedMiningCoinbase = Strings::Format("%s%s%s%s",
                                                    // magic: 0xfa, 0xbe, 0x6d('m'), 0x6d('m')
                                                    "fabe6d6d",
                                                    // block_hash: Hash of the AuxPOW block header
                                                    nmcAuxBlockHash_.ToString().c_str(),
                                                    merkleSize.c_str(), // merkle_size : 1
                                                    merkleNonce.c_str() // merkle_nonce: 0
                                                    );
      vector<char> mergedMiningBin;
      Hex2Bin(mergedMiningCoinbase.c_str(), mergedMiningBin);
      assert(mergedMiningBin.size() == (12+32));
      cbIn.scriptSig.insert(cbIn.scriptSig.end(),
                            mergedMiningBin.begin(), mergedMiningBin.end());
    }

  #ifdef USER_DEFINED_COINBASE
    // reserved for user defined coinbase info
    string userCoinbaseInfoPadding;
    userCoinbaseInfoPadding.resize(USER_DEFINED_COINBASE_SIZE, '\x20');
    cbIn.scriptSig.insert(cbIn.scriptSig.end(), userCoinbaseInfoPadding.begin(), userCoinbaseInfoPadding.end());
  #endif

    //  placeHolder: extra nonce1 (4bytes) + extra nonce2 (8bytes)
    const vector<char> placeHolder(4 + 8, 0xEE);
    // pub extra nonce place holder
    cbIn.scriptSig.insert(cbIn.scriptSig.end(), placeHolder.begin(), placeHolder.end());

    // 100: coinbase script sig max len, range: (2, 100).
    //
    // bitcoind/src/main.cpp: CheckTransaction()
    //   if (tx.IsCoinBase())
    //   {
    //     if (tx.vin[0].scriptSig.size() < 2 || tx.vin[0].scriptSig.size() > 100)
    //       return state.DoS(100, false, REJECT_INVALID, "bad-cb-length");
    //   }
    //
    if (cbIn.scriptSig.size() >= 100) {
      LOG(FATAL) << "coinbase input script size over than 100, shold < 100";
      return false;
    }

    // coinbase outputs
    vector<CTxOut> cbOut;

    //
    // output[0]: pool payment address
    //
    {
      CTxOut paymentTxOut;
      paymentTxOut.scriptPubKey = GetScriptForDestination(poolPayoutAddr);

    #ifdef CHAIN_TYPE_BCH
      paymentTxOut.nValue = Amount(coinbaseValue_);
    #else
      paymentTxOut.nValue = coinbaseValue_;
    #endif

      cbOut.push_back(paymentTxOut);
    }

    //
    // output[1] (optional): witness commitment
    //
    if (!witnessCommitment_.empty()) {
      DLOG(INFO) << "witness commitment: " << witnessCommitment_.c_str();
      vector<char> binBuf;
      Hex2Bin(witnessCommitment_.c_str(), binBuf);

      CTxOut witnessTxOut;
      witnessTxOut.scriptPubKey = CScript((unsigned char*)binBuf.data(),
                                      (unsigned char*)binBuf.data() + binBuf.size());
    #ifdef CHAIN_TYPE_BCH
      witnessTxOut.nValue = Amount(0);
    #else
      witnessTxOut.nValue = 0;
    #endif

      cbOut.push_back(witnessTxOut);
    }

    //
    // output[2]: RSK merge mining
    // Tips: it may be output[1] if segwit not enabled in a chain (like BitcoinCash).
    //
    if (latestRskBlockJson.isInitialized()) {
      DLOG(INFO) << "RSK blockhash: " << blockHashForMergedMining_;
      string rskBlockTag = "\x52\x53\x4B\x42\x4C\x4F\x43\x4B\x3A"; // "RSKBLOCK:"
      vector<char> rskTag(rskBlockTag.begin(), rskBlockTag.end());
      vector<char> binBuf;

      Hex2Bin(blockHashForMergedMining_.c_str(), binBuf);

      rskTag.insert(std::end(rskTag), std::begin(binBuf), std::end(binBuf));

      CTxOut rskTxOut;
      rskTxOut.scriptPubKey = CScript((unsigned char*)rskTag.data(),
                                      (unsigned char*)rskTag.data() + rskTag.size());
    #ifdef CHAIN_TYPE_BCH
      rskTxOut.nValue = Amount(0);
    #else
      rskTxOut.nValue = 0;
    #endif

      cbOut.push_back(rskTxOut);
    }

    CMutableTransaction cbtx;
    cbtx.vin.push_back(cbIn);
    cbtx.vout = cbOut;

    vector<char> coinbaseTpl;
    {
      CSerializeData sdata;
      CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION);
      ssTx << cbtx;  // put coinbase CTransaction to CDataStream
      ssTx.GetAndClear(sdata);  // dump coinbase bin to coinbaseTpl
      coinbaseTpl.insert(coinbaseTpl.end(), sdata.begin(), sdata.end());
    }

    // check coinbase tx size
    if (coinbaseTpl.size() >= COINBASE_TX_MAX_SIZE) {
      LOG(FATAL) << "coinbase tx size " << coinbaseTpl.size()
      << " is over than max " << COINBASE_TX_MAX_SIZE;
      return false;
    }

    const int64 extraNonceStart = findExtraNonceStart(coinbaseTpl, placeHolder);
    coinbase1_ = HexStr(&coinbaseTpl[0], &coinbaseTpl[extraNonceStart]);
    coinbase2_ = HexStr(&coinbaseTpl[extraNonceStart + placeHolder.size()],
                        &coinbaseTpl[coinbaseTpl.size()]);
  }
  
  return true;
}

bool StratumJob::isEmptyBlock() {
  return merkleBranch_.size() == 0 ? true : false;
}