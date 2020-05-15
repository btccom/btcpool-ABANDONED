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
#include "StratumBitcoin.h"
#include "StratumMiner.h"
#include "BitcoinUtils.h"

#include <core_io.h>
#include <hash.h>
#include <script/script.h>

#if defined(CHAIN_TYPE_BSV)
#include <script/script_num.h>
#endif

#include <uint256.h>
#include <util.h>
#include <pubkey.h>
#include <streams.h>

#include "Utils.h"
#include <glog/logging.h>

#include <boost/endian/buffers.hpp>

#include <nlohmann/json.hpp>

using JSON = nlohmann::json;
using JSONException = nlohmann::detail::exception;

void BitcoinHeaderData::set(const CBlockHeader &header) {
  CDataStream ssBlockHeader(SER_NETWORK, PROTOCOL_VERSION);
  ssBlockHeader << header;
  // use std::string to save binary data
  const string headerBin = ssBlockHeader.str();
  assert(headerBin.size() <= BitcoinHeaderSize);
  // set header data
  memcpy(headerData_, headerBin.data(), headerBin.size());
}

bool BitcoinHeaderData::get(CBlockHeader &header) {
  std::vector<unsigned char> dataHeader(
      headerData_, headerData_ + BitcoinHeaderSize);
  CDataStream ssHeader(dataHeader, SER_NETWORK, PROTOCOL_VERSION);
  try {
    ssHeader >> header;
  } catch (const std::exception &e) {
    LOG(ERROR) << "BitcoinHeaderData: unserilze header failed: " << e.what();
  } catch (...) {
    LOG(ERROR) << "BitcoinHeaderData: unserilze header failed: unknown error";
    return false;
  }
  return true;
}

static void
makeMerkleBranch(const vector<uint256> &vtxhashs, vector<uint256> &steps) {
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
      hashs[i] = Hash(
          BEGIN(hashs[i * 2 + 1]),
          END(hashs[i * 2 + 1]),
          BEGIN(hashs[i * 2 + 2]),
          END(hashs[i * 2 + 2]));
    }
    hashs.resize((hashs.size() - 1) / 2);
  }
  assert(hashs.size() == 1);
  steps.push_back(*hashs.begin()); // put the last one
}

static int64_t findExtraNonceStart(
    const vector<char> &coinbaseOriTpl, const vector<char> &placeHolder) {
  // find for the end
  for (int64_t i = coinbaseOriTpl.size() - placeHolder.size(); i >= 0; i--) {
    if (memcmp(&coinbaseOriTpl[i], &placeHolder[0], placeHolder.size()) == 0) {
      return i;
    }
  }
  return -1;
}

StratumJobBitcoin::StratumJobBitcoin() {
}

string StratumJobBitcoin::serializeToJson() const {
  string merkleBranchStr;
  merkleBranchStr.reserve(merkleBranch_.size() * 64 + 1);
  for (size_t i = 0; i < merkleBranch_.size(); i++) {
    merkleBranchStr.append(merkleBranch_[i].ToString());
  }

  JSON subPool = JSON::object();
  for (const auto &itr : subPool_) {
    subPool[itr.first] = {
        {"coinbase1", itr.second.coinbase1_},
        {"coinbase2", itr.second.coinbase2_},
        {"grandCoinbase1", itr.second.grandCoinbase1_},
    };
  }

  //
  // we use key->value json string, so it's easy to update system
  //
  return Strings::Format(
      "{\"jobId\":%u"
      ",\"gbtHash\":\"%s\""
      ",\"prevHash\":\"%s\",\"prevHashBeStr\":\"%s\""
      ",\"height\":%d,\"coinbase1\":\"%s\",\"grandCoinbase1\":\"%s\","
      "\"coinbase2\":\"%s\""
      ",\"subPool\":%s"
      ",\"merkleBranch\":\"%s\""
      ",\"nVersion\":%d,\"nBits\":%u,\"nTime\":%u"
      ",\"minTime\":%u,\"coinbaseValue\":%d"
      ",\"witnessCommitment\":\"%s\""
#ifdef CHAIN_TYPE_UBTC
      ",\"rootStateHash\":\"%s\""
#endif
#ifdef CHAIN_TYPE_ZEC
      ",\"merkleRoot\":\"%s\""
      ",\"finalSaplingRoot\":\"%s\""
#endif
      // proxy stratum job, optional
      ",\"proxyExtraNonce2Size\":%u,\"proxyJobDifficulty\":%u"
      // namecoin, optional
      ",\"nmcBlockHash\":\"%s\",\"nmcBits\":%u,\"nmcHeight\":%d"
      ",\"nmcRpcAddr\":\"%s\",\"nmcRpcUserpass\":\"%s\""
      // RSK, optional
      ",\"rskBlockHashForMergedMining\":\"%s\",\"rskNetworkTarget\":\"0x%s\""
      ",\"rskFeesForMiner\":\"%s\""
      ",\"rskdRpcAddress\":\"%s\",\"rskdRpcUserPwd\":\"%s\""
      // namecoin and RSK
      // TODO: delete isRskCleanJob (keep it for forward compatible).
      ",\"isRskCleanJob\":%s,\"mergedMiningClean\":%s"
      // vcash
      ",\"vcashBlockHashForMergedMining\":\"%s\",\"vcashNetworkTarget\":\"0x%"
      "s\""
      ",\"vcashHeight\":%" PRIu64
      ",\"vcashdRpcAddress\":\"%s\",\"vcashdRpcUserPwd\":\"%s\""
      ",\"isVcashCleanJob\":%s"
      "}",
      jobId_,
      gbtHash_,
      prevHash_.ToString(),
      prevHashBeStr_,
      height_,
      coinbase1_,
      grandCoinbase1_,
      coinbase2_,
      subPool.dump(),
      // merkleBranch_ could be empty
      merkleBranchStr,
      nVersion_,
      nBits_,
      nTime_,
      minTime_,
      coinbaseValue_,
      witnessCommitment_,
#ifdef CHAIN_TYPE_UBTC
      rootStateHash_,
#endif
#ifdef CHAIN_TYPE_ZEC
      merkleRoot_.ToString().c_str(),
      finalSaplingRoot_.ToString().c_str(),
#endif
      // proxy stratum job
      proxyExtraNonce2Size_,
      proxyJobDifficulty_,
      // nmc
      nmcAuxBlockHash_.ToString(),
      nmcAuxBits_,
      nmcHeight_,
      nmcRpcAddr_,
      nmcRpcUserpass_,
      // rsk
      blockHashForMergedMining_,
      rskNetworkTarget_.GetHex(),
      feesForMiner_,
      rskdRpcAddress_,
      rskdRpcUserPwd_,
      isMergedMiningCleanJob_ ? "true" : "false",
      isMergedMiningCleanJob_ ? "true" : "false",

      // vcash
      vcashBlockHashForMergedMining_.size()
          ? vcashBlockHashForMergedMining_.c_str()
          : "",
      vcashNetworkTarget_.GetHex().c_str(),
      vcashHeight_,
      vcashdRpcAddress_.size() ? vcashdRpcAddress_.c_str() : "",
      vcashdRpcUserPwd_.size() ? vcashdRpcUserPwd_.c_str() : "",
      isMergedMiningCleanJob_ ? "true" : "false");
}

bool StratumJobBitcoin::unserializeFromJson(const char *s, size_t len) {
  JsonNode j;
  if (!JsonNode::parse(s, s + len, j)) {
    return false;
  }
  if (j["jobId"].type() != Utilities::JS::type::Int ||
      j["gbtHash"].type() != Utilities::JS::type::Str ||
      j["prevHash"].type() != Utilities::JS::type::Str ||
      j["prevHashBeStr"].type() != Utilities::JS::type::Str ||
      j["height"].type() != Utilities::JS::type::Int ||
      j["coinbase1"].type() != Utilities::JS::type::Str ||
      j["coinbase2"].type() != Utilities::JS::type::Str ||
      j["merkleBranch"].type() != Utilities::JS::type::Str ||
      j["nVersion"].type() != Utilities::JS::type::Int ||
      j["nBits"].type() != Utilities::JS::type::Int ||
      j["nTime"].type() != Utilities::JS::type::Int ||
      j["minTime"].type() != Utilities::JS::type::Int ||
      j["coinbaseValue"].type() != Utilities::JS::type::Int) {
    LOG(ERROR) << "parse stratum job failure: " << s;
    return false;
  }

  jobId_ = j["jobId"].uint64();
  gbtHash_ = j["gbtHash"].str();
  prevHash_ = uint256S(j["prevHash"].str());
  prevHashBeStr_ = j["prevHashBeStr"].str();
  height_ = j["height"].int32();
  coinbase1_ = j["coinbase1"].str();

  if (j["grandCoinbase1"].type() == Utilities::JS::type::Str) {
    grandCoinbase1_ = j["grandCoinbase1"].str();
  } else {
    grandCoinbase1_ = "";
  }

  coinbase2_ = j["coinbase2"].str();
  nVersion_ = j["nVersion"].int32();
  nBits_ = j["nBits"].uint32();
  nTime_ = j["nTime"].uint32();
  minTime_ = j["minTime"].uint32();
  coinbaseValue_ = j["coinbaseValue"].int64();

  subPool_.clear();
  if (j["subPool"].type() == Utilities::JS::type::Obj) {
    auto subPool = j["subPool"].obj();
    for (auto itr : subPool) {
      string name = itr.key();
      if (itr.type() == Utilities::JS::type::Obj &&
          itr["coinbase1"].type() == Utilities::JS::type::Str &&
          itr["coinbase2"].type() == Utilities::JS::type::Str) {
        subPool_[name] = {
            name, itr["coinbase1"].str(), itr["coinbase2"].str(), ""};
        if (itr["grandCoinbase1"].type() == Utilities::JS::type::Str) {
          auto &subPoolJob = subPool_[name];
          subPoolJob.grandCoinbase1_ = itr["grandCoinbase1"].str();
        }
      }
    }
  }

  // witnessCommitment, optional
  // witnessCommitment must be at least 38 bytes
  if (j["witnessCommitment"].type() == Utilities::JS::type::Str &&
      j["witnessCommitment"].str().length() >= 38 * 2) {
    witnessCommitment_ = j["witnessCommitment"].str();
  }

#ifdef CHAIN_TYPE_UBTC
  // rootStateHash, optional
  // rootStateHash must be at least 2 bytes (00f9, empty root state hash)
  if (j["rootStateHash"].type() == Utilities::JS::type::Str &&
      j["rootStateHash"].str().length() >= 2 * 2) {
    rootStateHash_ = j["rootStateHash"].str();
  }
#endif

#ifdef CHAIN_TYPE_ZEC
  if (j["merkleRoot"].type() == Utilities::JS::type::Str &&
      j["finalSaplingRoot"].type() == Utilities::JS::type::Str) {
    merkleRoot_ = uint256S(j["merkleRoot"].str());
    finalSaplingRoot_ = uint256S(j["finalSaplingRoot"].str());
  }
#endif

  // proxy stratum job, optional
  if (j["proxyExtraNonce2Size"].type() == Utilities::JS::type::Int &&
      j["proxyJobDifficulty"].type() == Utilities::JS::type::Int) {
    proxyExtraNonce2Size_ = j["proxyExtraNonce2Size"].uint32();
    proxyJobDifficulty_ = j["proxyJobDifficulty"].uint64();
  }

  // for Namecoin and RSK merged mining, optional
  if (j["mergedMiningClean"].type() == Utilities::JS::type::Bool) {
    isMergedMiningCleanJob_ = j["mergedMiningClean"].boolean();
  }

  //
  // namecoin, optional
  //
  if (j["nmcBlockHash"].type() == Utilities::JS::type::Str &&
      j["nmcBits"].type() == Utilities::JS::type::Int &&
      j["nmcHeight"].type() == Utilities::JS::type::Int &&
      j["nmcRpcAddr"].type() == Utilities::JS::type::Str &&
      j["nmcRpcUserpass"].type() == Utilities::JS::type::Str) {
    nmcAuxBlockHash_ = uint256S(j["nmcBlockHash"].str());
    nmcAuxBits_ = j["nmcBits"].uint32();
    nmcHeight_ = j["nmcHeight"].int32();
    nmcRpcAddr_ = j["nmcRpcAddr"].str();
    nmcRpcUserpass_ = j["nmcRpcUserpass"].str();
    BitsToTarget(nmcAuxBits_, nmcNetworkTarget_);
  }

  //
  // RSK, optional
  //
  if (j["rskBlockHashForMergedMining"].type() == Utilities::JS::type::Str &&
      j["rskNetworkTarget"].type() == Utilities::JS::type::Str &&
      j["rskFeesForMiner"].type() == Utilities::JS::type::Str &&
      j["rskdRpcAddress"].type() == Utilities::JS::type::Str &&
      j["rskdRpcUserPwd"].type() == Utilities::JS::type::Str) {
    blockHashForMergedMining_ = j["rskBlockHashForMergedMining"].str();
    rskNetworkTarget_ = uint256S(j["rskNetworkTarget"].str());
    feesForMiner_ = j["rskFeesForMiner"].str();
    rskdRpcAddress_ = j["rskdRpcAddress"].str();
    rskdRpcUserPwd_ = j["rskdRpcUserPwd"].str();
  }

  //
  // Vcash, optional
  //
  if (j["vcashBlockHashForMergedMining"].type() == Utilities::JS::type::Str &&
      j["vcashNetworkTarget"].type() == Utilities::JS::type::Str &&
      j["vcashHeight"].type() == Utilities::JS::type::Int &&
      j["vcashdRpcAddress"].type() == Utilities::JS::type::Str &&
      j["vcashdRpcUserPwd"].type() == Utilities::JS::type::Str) {
    vcashBlockHashForMergedMining_ = j["vcashBlockHashForMergedMining"].str();
    vcashNetworkTarget_ = uint256S(j["vcashNetworkTarget"].str());
    vcashHeight_ = j["vcashHeight"].uint64();
    vcashdRpcAddress_ = j["vcashdRpcAddress"].str();
    vcashdRpcUserPwd_ = j["vcashdRpcUserPwd"].str();

    nmcNetworkTarget_ = (UintToArith256(nmcNetworkTarget_) >
                         UintToArith256(vcashNetworkTarget_))
        ? nmcNetworkTarget_
        : vcashNetworkTarget_;
  }

  const string merkleBranchStr = j["merkleBranch"].str();
  const size_t merkleBranchCount = merkleBranchStr.length() / 64;
  merkleBranch_.resize(merkleBranchCount);
  for (size_t i = 0; i < merkleBranchCount; i++) {
    merkleBranch_[i] = uint256S(merkleBranchStr.substr(i * 64, 64));
  }

  if (proxyJobDifficulty_ > 0) {
    BitcoinDifficulty::DiffToTarget(proxyJobDifficulty_, networkTarget_);
  } else {
    BitsToTarget(nBits_, networkTarget_);
  }

  return true;
}

bool StratumJobBitcoin::initFromGbt(
    const char *gbt,
    const string &poolCoinbaseInfo,
    const CTxDestination &poolPayoutAddr,
    const vector<SubPoolInfo> &subPool,
    const uint32_t blockVersion,
    const string &nmcAuxBlockJson,
    const RskWork &latestRskBlockJson,
    const VcashWork &latestVcashBlockJson,
    const bool isMergedMiningUpdate,
    const bool grandPoolEnabled) {
  uint256 gbtHash = Hash(gbt, gbt + strlen(gbt));
  JsonNode r;
  if (!JsonNode::parse(gbt, gbt + strlen(gbt), r)) {
    LOG(ERROR) << "decode gbt json fail: >" << gbt << "<";
    return false;
  }
  JsonNode jgbt = r["result"];
  gbtHash_ = gbtHash.ToString();

#if defined(CHAIN_TYPE_BCH) || defined(CHAIN_TYPE_BSV)
  bool isLightVersion =
      jgbt[LIGHTGBT_JOB_ID].type() == Utilities::JS::type::Str;
  // merkle branch, merkleBranch_ could be empty
  if (isLightVersion) {
    prevHash_ = uint256S(jgbt[LIGHTGBT_PREV_HASH].str());
    nBits_ = jgbt[LIGHTGBT_BITS].uint32_hex();
    nTime_ = jgbt[LIGHTGBT_TIME].uint32();
    coinbaseValue_ = jgbt[LIGHTGBT_COINBASE_VALUE].int64();
    auto &gbtMerkle = jgbt[LIGHTGBT_MERKLE].array();
    for (auto &mHex : gbtMerkle) {
      uint256 m;
      m.SetHex(mHex.str().c_str());
      merkleBranch_.push_back(m);
    }
  } else
#endif
  // merkle branch, merkleBranch_ could be empty
  {
    prevHash_ = uint256S(jgbt["previousblockhash"].str());
    nBits_ = jgbt["bits"].uint32_hex();
    nTime_ = jgbt["curtime"].uint32();
    coinbaseValue_ = jgbt["coinbasevalue"].int64();
    // read txs hash/data
    vector<uint256> vtxhashs; // txs without coinbase
    for (JsonNode &node : jgbt["transactions"].array()) {
#ifdef CHAIN_TYPE_ZEC
      CTransaction tx;
      DecodeHexTx(tx, node["data"].str());
      vtxhashs.push_back(tx.GetHash());
#else
      CMutableTransaction tx;
      DecodeHexTx(tx, node["data"].str());
      vtxhashs.push_back(MakeTransactionRef(std::move(tx))->GetHash());
#endif
    }
    // make merkleSteps and merkle branch
    makeMerkleBranch(vtxhashs, merkleBranch_);
  }

  // height etc.
  // fields in gbt json has already checked by GbtMaker
  height_ = jgbt["height"].int32();
  if (blockVersion != 0) {
    nVersion_ = blockVersion;
  } else {
    nVersion_ = jgbt["version"].uint32();
  }
  minTime_ = jgbt["mintime"].type() == Utilities::JS::type::Int
      ? jgbt["mintime"].uint32()
      : nTime_;

  // default_witness_commitment must be at least 38 bytes
  if (jgbt["default_witness_commitment"].type() == Utilities::JS::type::Str &&
      jgbt["default_witness_commitment"].str().length() >= 38 * 2) {
    witnessCommitment_ = jgbt["default_witness_commitment"].str();
  }

#ifdef CHAIN_TYPE_UBTC
  // rootStateHash, optional
  // default_root_state_hash must be at least 2 bytes (00f9, empty root state
  // hash)
  if (jgbt["default_root_state_hash"].type() == Utilities::JS::type::Str &&
      jgbt["default_root_state_hash"].str().length() >= 2 * 2) {
    rootStateHash_ = jgbt["default_root_state_hash"].str();
  }
#endif

#ifdef CHAIN_TYPE_ZEC
  if (jgbt["finalsaplingroothash"].type() == Utilities::JS::type::Str) {
    finalSaplingRoot_ = uint256S(jgbt["finalsaplingroothash"].str());
  }
#endif

  BitsToTarget(nBits_, networkTarget_);

  // previous block hash
  // we need to convert to little-endian
  // 00000000000000000328e9fea9914ad83b7404a838aa66aefb970e5689c2f63d
  // 89c2f63dfb970e5638aa66ae3b7404a8a9914ad80328e9fe0000000000000000
  for (int i = 0; i < 8; i++) {
    uint32_t a = *(uint32_t *)(BEGIN(prevHash_) + i * 4);
    a = HToBe(a);
    prevHashBeStr_ += HexStr(BEGIN(a), END(a));
  }

  // for Namecoin and RSK merged mining
  isMergedMiningCleanJob_ = isMergedMiningUpdate;

  //
  // namecoin merged mining
  //
  if (!nmcAuxBlockJson.empty()) {
    do {
      JsonNode jNmcAux;
      if (!JsonNode::parse(
              nmcAuxBlockJson.c_str(),
              nmcAuxBlockJson.c_str() + nmcAuxBlockJson.length(),
              jNmcAux)) {
        LOG(ERROR) << "decode nmc auxblock json fail: >" << nmcAuxBlockJson
                   << "<";
        break;
      }
      // check fields created_at_ts
      if (jNmcAux["created_at_ts"].type() != Utilities::JS::type::Int ||
          jNmcAux["hash"].type() != Utilities::JS::type::Str ||
          jNmcAux["merkle_size"].type() != Utilities::JS::type::Int ||
          jNmcAux["merkle_nonce"].type() != Utilities::JS::type::Int ||
          jNmcAux["height"].type() != Utilities::JS::type::Int ||
          jNmcAux["bits"].type() != Utilities::JS::type::Str ||
          jNmcAux["rpc_addr"].type() != Utilities::JS::type::Str ||
          jNmcAux["rpc_userpass"].type() != Utilities::JS::type::Str) {
        LOG(ERROR) << "nmc auxblock fields failure";
        break;
      }
      // check timestamp
      if (jNmcAux["created_at_ts"].uint32() + 60u < time(nullptr)) {
        LOG(ERROR) << "too old nmc auxblock: "
                   << date("%F %T", jNmcAux["created_at_ts"].uint32());
        break;
      }

      // set nmc aux info
      nmcAuxBlockHash_ = uint256S(jNmcAux["hash"].str());
      nmcAuxMerkleSize_ = jNmcAux["merkle_size"].int32();
      nmcAuxMerkleNonce_ = jNmcAux["merkle_nonce"].int32();
      nmcAuxBits_ = jNmcAux["bits"].uint32_hex();
      nmcHeight_ = jNmcAux["height"].int32();
      nmcRpcAddr_ = jNmcAux["rpc_addr"].str();
      nmcRpcUserpass_ = jNmcAux["rpc_userpass"].str();
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
  }

  //
  // vcash merged mining
  //
  if (latestVcashBlockJson.isInitialized()) {

    // set vcash info
    vcashBlockHashForMergedMining_ = latestVcashBlockJson.getBlockHash();
    vcashNetworkTarget_ = uint256S(latestVcashBlockJson.getTarget());
    baserewards_ = latestVcashBlockJson.getBaseRewards();
    transactionsfee_ = latestVcashBlockJson.getTransactionsfee();
    vcashHeight_ = latestVcashBlockJson.getHeight();
    vcashdRpcAddress_ = latestVcashBlockJson.getRpcAddress();
    vcashdRpcUserPwd_ = latestVcashBlockJson.getRpcUserPwd();

    nmcNetworkTarget_ = (UintToArith256(nmcNetworkTarget_) >
                         UintToArith256(vcashNetworkTarget_))
        ? nmcNetworkTarget_
        : vcashNetworkTarget_;
  }

  // make coinbase1 & coinbase2
#ifdef CHAIN_TYPE_ZEC
  {
    // ZCash uses a pre-generated coinbase transaction template
    string coinbaseStr = jgbt["coinbasetxn"]["data"].str();
    CMutableTransaction cbtx;
    {
      CTransaction rotx;
      DecodeHexTx(rotx, coinbaseStr);
      cbtx = rotx;
    }

    // ------------- input -------------
    if (cbtx.vin.size() != 1) {
      LOG(ERROR) << "wrong coinbase input size: " << cbtx.vin.size()
                 << ", tx data: " << coinbaseStr;
      return false;
    }
    CTxIn &cbIn = cbtx.vin[0];

    // add current timestamp to coinbase tx input, so if the block's merkle root
    // hash is the same, there's no risk for miners to calc the same space.
    // https://github.com/btccom/btcpool/issues/5
    //
    // 5 bytes in script: 0x04xxxxxxxx.
    // eg. 0x0402363d58 -> 0x583d3602 = 1480406530 = 2016-11-29 16:02:10
    //
    cbIn.scriptSig << CScriptNum((uint32_t)time(nullptr));

    // pool's info
    cbIn.scriptSig.insert(
        cbIn.scriptSig.end(), poolCoinbaseInfo.begin(), poolCoinbaseInfo.end());

    // 100: coinbase script sig max len, range: (2, 100).
    //
    // zcashd/src/main.cpp: CheckTransactionWithoutProofVerification()
    //   if (tx.IsCoinBase())
    //   {
    //     if (tx.vin[0].scriptSig.size() < 2 || tx.vin[0].scriptSig.size() >
    //     100)
    //       return state.DoS(100, false, REJECT_INVALID, "bad-cb-length");
    //   }
    //
    if (cbIn.scriptSig.size() >= 100) {
      LOG(FATAL) << "coinbase input script size over than 100, shold < 100";
      return false;
    }

    // ------------- outputs -------------
    if (cbtx.vout.size() < 1) {
      LOG(ERROR) << "wrong coinbase output size: " << cbtx.vout.size()
                 << ", tx data: " << coinbaseStr;
      return false;
    }

    CTxOut &poolReward = cbtx.vout[0];
    auto consensusParams = Params().GetConsensus();

    if (height_ <= consensusParams.GetLastFoundersRewardBlockHeight()) {
      if (cbtx.vout.size() < 2) {
        LOG(ERROR) << "wrong coinbase output size: " << cbtx.vout.size()
                   << ", tx data: " << coinbaseStr;
        return false;
      }

      CTxOut &foundersReward = cbtx.vout[1];
      auto rewardByHeight = GetBlockReward(height_, consensusParams);

      if (foundersReward.nValue > poolReward.nValue) {
        LOG(ERROR) << "wrong coinbase output value, foundersReward.nValue ("
                   << foundersReward.nValue << ") > poolReward.nValue ("
                   << poolReward.nValue << ")"
                   << ", tx data: " << coinbaseStr;
        return false;
      }

      if (poolReward.nValue < rewardByHeight) {
        LOG(ERROR) << "wrong coinbase output value, poolReward.nValue ("
                   << poolReward.nValue << ") < GetBlockReward(" << height_
                   << ") (" << rewardByHeight << ")"
                   << ", tx data: " << coinbaseStr;
        return false;
      }
    }

    poolReward.scriptPubKey = GetScriptForDestination(poolPayoutAddr);

    // ------------- compute merkle root -------------
    vector<char> coinbaseTpl;
    {
      CSerializeData sdata;
      CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION);
      ssTx << cbtx; // put coinbase CTransaction to CDataStream
      ssTx.GetAndClear(sdata); // dump coinbase bin to coinbaseTpl
      coinbaseTpl.insert(coinbaseTpl.end(), sdata.begin(), sdata.end());
    }

    // We store coinbaseTpl in coinbase_, keep coinbase2_ empty.
    coinbase1_ = HexStr(coinbaseTpl.begin(), coinbaseTpl.end());
    coinbase2_.clear();

    merkleRoot_ = ComputeCoinbaseMerkleRoot(coinbaseTpl, merkleBranch_);
  }
#else
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

    uint32_t beforeCoinbaseInfo = cbIn.scriptSig.size();

    // pool's info
    cbIn.scriptSig.insert(
        cbIn.scriptSig.end(), poolCoinbaseInfo.begin(), poolCoinbaseInfo.end());

    uint32_t afterCoinbaseInfo = cbIn.scriptSig.size();

    //
    // put namecoin merged mining info, 44 bytes
    // https://en.bitcoin.it/wiki/Merged_mining_specification
    //
    if (nmcAuxBits_ != 0u) {
      string merkleSize, merkleNonce;
      Bin2Hex((uint8_t *)&nmcAuxMerkleSize_, 4, merkleSize);
      Bin2Hex((uint8_t *)&nmcAuxMerkleNonce_, 4, merkleNonce);
      string mergedMiningCoinbase = Strings::Format(
          "%s%s%s%s",
          // magic: 0xfa, 0xbe, 0x6d('m'), 0x6d('m')
          "fabe6d6d",
          // block_hash: Hash of the AuxPOW block header
          nmcAuxBlockHash_.ToString(),
          merkleSize, // merkle_size : 1
          merkleNonce // merkle_nonce: 0
      );
      vector<char> mergedMiningBin;
      Hex2Bin(mergedMiningCoinbase.c_str(), mergedMiningBin);
      assert(mergedMiningBin.size() == (12 + 32));
      cbIn.scriptSig.insert(
          cbIn.scriptSig.end(), mergedMiningBin.begin(), mergedMiningBin.end());
    }

    //  placeHolder: extra nonce1 (4bytes) + extra nonce2 (8bytes)
    const vector<char> placeHolder(
        StratumMiner::kExtraNonce1Size_ +
            (grandPoolEnabled ? StratumMiner::kExtraGrandNonce1Size_ : 0) +
            StratumMiner::kExtraNonce2Size_,
        0xEE);
    // pub extra nonce place holder
    cbIn.scriptSig.insert(
        cbIn.scriptSig.end(), placeHolder.begin(), placeHolder.end());

    // 100: coinbase script sig max len, range: (2, 100).
    //
    // bitcoind/src/main.cpp: CheckTransaction()
    //   if (tx.IsCoinBase())
    //   {
    //     if (tx.vin[0].scriptSig.size() < 2 || tx.vin[0].scriptSig.size() >
    //     100)
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
    size_t paymentTxOutIndex = cbOut.size();
    {
      CTxOut paymentTxOut;
      paymentTxOut.scriptPubKey = GetScriptForDestination(poolPayoutAddr);

      paymentTxOut.nValue = AMOUNT_TYPE(coinbaseValue_);

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
      witnessTxOut.scriptPubKey = CScript(
          (unsigned char *)binBuf.data(),
          (unsigned char *)binBuf.data() + binBuf.size());
      witnessTxOut.nValue = AMOUNT_TYPE(0);

      cbOut.push_back(witnessTxOut);
    }

#ifdef CHAIN_TYPE_UBTC
    //
    // output[2] (optional): root state hash of UB smart contract
    //
    if (!rootStateHash_.empty()) {
      DLOG(INFO) << "root state hash: " << rootStateHash_.c_str();
      vector<char> binBuf;
      Hex2Bin(rootStateHash_.c_str(), binBuf);

      CTxOut rootStateTxOut;
      rootStateTxOut.scriptPubKey = CScript(
          (unsigned char *)binBuf.data(),
          (unsigned char *)binBuf.data() + binBuf.size());
      rootStateTxOut.nValue = 0;

      cbOut.push_back(rootStateTxOut);
    }
#endif
    //
    // output[3] (optional): RSK merge mining
    //
    if (latestRskBlockJson.isInitialized()) {
      DLOG(INFO) << "RSK blockhash: " << blockHashForMergedMining_;
      string rskBlockTag =
          "\x6a\x29\x52\x53\x4B\x42\x4C\x4F\x43\x4B\x3A"; // "RSKBLOCK:"
      vector<char> rskTag(rskBlockTag.begin(), rskBlockTag.end());
      vector<char> binBuf;

      Hex2Bin(blockHashForMergedMining_.c_str(), binBuf);

      rskTag.insert(std::end(rskTag), std::begin(binBuf), std::end(binBuf));

      CTxOut rskTxOut;
      rskTxOut.scriptPubKey = CScript(
          (unsigned char *)rskTag.data(),
          (unsigned char *)rskTag.data() + rskTag.size());
      rskTxOut.nValue = AMOUNT_TYPE(0);

      cbOut.push_back(rskTxOut);
    }

    //
    // output[3] (optional): VCASH merge mining
    //
    if (latestVcashBlockJson.isInitialized()) {
      DLOG(INFO) << "Vcash blockhash: " << vcashBlockHashForMergedMining_;
      string vcashBlockTag = "\x6a\x24\xb9\xe1\x1b\x6d";
      // OP_RETURN(0x6a) + Length(0x24) + MagicNum(0xb9e11b6d) + Vcash Header
      // Hash
      vector<char> vcashTag(vcashBlockTag.begin(), vcashBlockTag.end());
      vector<char> binBuf;

      Hex2Bin(vcashBlockHashForMergedMining_.c_str(), binBuf);

      vcashTag.insert(std::end(vcashTag), std::begin(binBuf), std::end(binBuf));

      CTxOut vcashTxOut;
      vcashTxOut.scriptPubKey = CScript(
          (unsigned char *)vcashTag.data(),
          (unsigned char *)vcashTag.data() + vcashTag.size());
      vcashTxOut.nValue = AMOUNT_TYPE(0);

      cbOut.push_back(vcashTxOut);
    }

    if (nmcAuxBits_ == 0u && latestVcashBlockJson.isInitialized()) {
      nmcAuxBits_ = latestVcashBlockJson.getBits();
      nmcRpcAddr_ = vcashdRpcAddress_;
      nmcRpcUserpass_ = vcashdRpcUserPwd_;
    }

    CMutableTransaction cbtx;
    size_t coinbaseTxInIndex = cbtx.vin.size();
    cbtx.vin.push_back(cbIn);
    cbtx.vout = cbOut;

    vector<char> coinbaseTpl;
    {
      CSerializeData sdata;
      CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION);
      ssTx << cbtx; // put coinbase CTransaction to CDataStream
      ssTx.GetAndClear(sdata); // dump coinbase bin to coinbaseTpl
      coinbaseTpl.insert(coinbaseTpl.end(), sdata.begin(), sdata.end());
    }

    // check coinbase tx size
    if (coinbaseTpl.size() >= COINBASE_TX_MAX_SIZE) {
      LOG(FATAL) << "coinbase tx size " << coinbaseTpl.size()
                 << " is over than max " << COINBASE_TX_MAX_SIZE;
      return false;
    }

    const int64_t extraNonceStart =
        findExtraNonceStart(coinbaseTpl, placeHolder);
    coinbase1_ = HexStr(&coinbaseTpl[0], &coinbaseTpl[extraNonceStart]);
    coinbase2_ = HexStr(
        &coinbaseTpl[extraNonceStart + placeHolder.size()],
        &coinbaseTpl[coinbaseTpl.size()]);

    if (grandPoolEnabled) {
      // extranNonce1+extraGrandNonce1+extraNonce2
      grandCoinbase1_ = coinbase1_;
      // 00000000+extraNonce1+extraNonce2
      coinbase1_ += string((StratumMiner::kExtraGrandNonce1Size_)*2, '0');
    } else {
      grandCoinbase1_ = "";
    }

    // add sub-pool coinbase tx
    if (!subPool.empty()) {
      vector<char> coinbase1(
          cbIn.scriptSig.begin(), cbIn.scriptSig.begin() + beforeCoinbaseInfo);
      vector<char> coinbase2(
          cbIn.scriptSig.begin() + afterCoinbaseInfo, cbIn.scriptSig.end());

      for (const auto &pool : subPool) {
        auto &vin = cbtx.vin[coinbaseTxInIndex];
        auto &vout = cbtx.vout[paymentTxOutIndex];

        vin.scriptSig = CScript();
        vin.scriptSig.insert(
            vin.scriptSig.end(), coinbase1.begin(), coinbase1.end());
        vin.scriptSig.insert(
            vin.scriptSig.end(),
            pool.coinbaseInfo_.begin(),
            pool.coinbaseInfo_.end());
        vin.scriptSig.insert(
            vin.scriptSig.end(), coinbase2.begin(), coinbase2.end());

        vout.scriptPubKey = GetScriptForDestination(pool.payoutAddr_);

        coinbaseTpl.clear();
        CSerializeData sdata;
        CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION);
        ssTx << cbtx; // put coinbase CTransaction to CDataStream
        ssTx.GetAndClear(sdata); // dump coinbase bin to coinbaseTpl
        coinbaseTpl.insert(coinbaseTpl.end(), sdata.begin(), sdata.end());

        // check coinbase tx size
        if (coinbaseTpl.size() >= COINBASE_TX_MAX_SIZE) {
          LOG(FATAL) << "coinbase tx for subpool " << pool.name_ << " size "
                     << coinbaseTpl.size() << " is over than max "
                     << COINBASE_TX_MAX_SIZE;
          return false;
        }

        const int64_t extraNonceStart =
            findExtraNonceStart(coinbaseTpl, placeHolder);

        subPool_[pool.name_] = {
            pool.name_,
            HexStr(&coinbaseTpl[0], &coinbaseTpl[extraNonceStart]), // coinbase1
            HexStr(
                &coinbaseTpl[extraNonceStart + placeHolder.size()],
                &coinbaseTpl[coinbaseTpl.size()]), // coinbase2
            "" // grandCoinbase1
        };

        if (grandPoolEnabled) {
          auto &subPoolJob = subPool_[pool.name_];
          subPoolJob.grandCoinbase1_ = subPoolJob.coinbase1_;
          subPoolJob.coinbase1_ = subPoolJob.coinbase1_ +
              string((StratumMiner::kExtraGrandNonce1Size_)*2, '0');
        }
      }
    }
  } // make coinbase1 & coinbase2
#endif

  return true;
}

bool StratumJobBitcoin::initFromStratumJob(
    vector<JsonNode> &jparamsArr,
    uint64_t currentDifficulty,
    const string &extraNonce1,
    uint32_t extraNonce2Size) {
  /*
[
"1", // job id
"8eb660b39a615d8c30bec6ded52c7189113aadda008508...", // prevHashBeStr_
"0200000001000000000000000000000000000000000000...", // coinbase1
"ffffffff0106208a4a000000001976a914da5b5f794566...", // coinbase2
[ // merkle branch
  "cfa8950989b3cbf4447262d63ec6edfa198293010f8bb1cc6d8b2b146dc73b00",
  "89778ea377892a8f119963659bae3e1e594c5137cfc8ab7703a71c9305c5ba9d",
  "b6e066003c7ba3026067690a6f20b35226581bfe36cce75b979078d4a260e2b0",
  "46dbb24024d458944e2009a2474ca5975ee203287fb768480befb340426367ac",
  "ae2ced91a3b828e8fe178bd36852d722c6e8d5a570b1b72977c4e33c5e2d8e40",
  "ebec0dfd3da58068b281c084e3194d0dfb1cc9564932ef6cc1e3d3e0434cbb2d",
  "8374240c7e9846f4e3942310650bf041b2532cbffe2cb3055edf0200d67ce5fd",
  "034854853e5295add7fb867c723fd925d2982cd44caacaeae167b661ae307ef5",
  "57f8fa1b986f2af62f190a469fc4967231c27533d23b96ee605a55481faa2f3a",
  "20684502470dfed35cbc6bf75a787017ee2786689828d86a8d75f0daa4329379"
],
"20000000", // version
"1802f650", // bits
"5cc59c40", // time
false // is clean
]
*/
  if (jparamsArr.size() < 9) {
    LOG(WARNING) << "job missing params (expect 9 params but only "
                 << jparamsArr.size() << ")";
    return false;
  }
  if (jparamsArr[1].type() != Utilities::JS::type::Str ||
      jparamsArr[1].size() != 64 ||
      jparamsArr[2].type() != Utilities::JS::type::Str ||
      jparamsArr[2].size() % 2 != 0 ||
      jparamsArr[3].type() != Utilities::JS::type::Str ||
      jparamsArr[3].size() % 2 != 0 ||
      jparamsArr[4].type() != Utilities::JS::type::Array ||
      jparamsArr[5].type() != Utilities::JS::type::Str ||
      jparamsArr[5].size() != 8 ||
      jparamsArr[6].type() != Utilities::JS::type::Str ||
      jparamsArr[6].size() != 8 ||
      jparamsArr[7].type() != Utilities::JS::type::Str ||
      jparamsArr[7].size() != 8 ||
      jparamsArr[8].type() != Utilities::JS::type::Bool) {
    LOG(WARNING) << "unexpected job param types";
    return false;
  }
  auto merkleBranchArr = jparamsArr[4].array();
  for (const auto &item : merkleBranchArr) {
    if (item.type() != Utilities::JS::type::Str || item.size() != 64) {
      LOG(WARNING) << "unexpected merkle branch types";
      return false;
    }
  }

  proxyExtraNonce2Size_ = extraNonce2Size;
  proxyJobDifficulty_ = currentDifficulty;
  BitcoinDifficulty::DiffToTarget(proxyJobDifficulty_, networkTarget_);

  prevHashBeStr_ = jparamsArr[1].str();
  coinbase1_ = jparamsArr[2].str() + extraNonce1;
  coinbase2_ = jparamsArr[3].str();
  nVersion_ = (int32_t)jparamsArr[5].uint32_hex();
  nBits_ = jparamsArr[6].uint32_hex();
  nTime_ = jparamsArr[7].uint32_hex();
  minTime_ = nTime_ - 600;

  for (const auto &item : merkleBranchArr) {
    merkleBranch_.push_back(reverse8bit(uint256S(item.str())));
  }

  prevHash_ = reverse32bit(uint256S(prevHashBeStr_));

  string coinbaseTxStr =
      coinbase1_ + string(proxyExtraNonce2Size_ * 2, '0') + coinbase2_;

  string fakeGbt = prevHashBeStr_ + coinbaseTxStr + jparamsArr[5].str() +
      jparamsArr[6].str() + jparamsArr[7].str();

  for (const auto &item : merkleBranchArr) {
    fakeGbt += item.str();
  }

  uint256 gbtHash = Hash(fakeGbt.data(), fakeGbt.data() + fakeGbt.size());
  gbtHash_ = gbtHash.ToString();
  height_ = getBlockHeightFromCoinbase(coinbase1_);

  // Make sserver send the job immediately
  isMergedMiningCleanJob_ = true;
  return true;
}

bool StratumJobBitcoin::isEmptyBlock() {
  return merkleBranch_.size() == 0 ? true : false;
}
