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
#include "StratumEth.h"

#include "Utils.h"
#include <glog/logging.h>

#include "bitcoin/CommonBitcoin.h"
#include "libethash/sha3.h"

#include <boost/endian/buffers.hpp>

///////////////////////////////StratumJobEth///////////////////////////
StratumJobEth::StratumJobEth() {
}

bool StratumJobEth::initFromGw(
    const RskWorkEth &work, EthConsensus::Chain chain, uint8_t serverId) {
  if (work.isInitialized()) {
    chain_ = chain;

    height_ = work.getHeight();
    parent_ = work.getParent();
    networkTarget_ = uint256S(work.getTarget());

    headerHash_ = work.getBlockHash();
    seedHash_ = work.getSeedHash();

    uncles_ = work.getUncles();
    transactions_ = work.getTransactions();
    gasUsedPercent_ = work.getGasUsedPercent();

    rpcAddress_ = work.getRpcAddress();
    rpcUserPwd_ = work.getRpcUserPwd();

    header_ = work.getHeader();

    // generate job id
    string header = headerHash_.substr(2, 64);
    // jobId: timestamp + hash of header + server id
    jobId_ = (static_cast<uint64_t>(time(nullptr)) << 32) |
        (djb2(header.c_str()) & 0xFFFFFF00) | serverId;
  }
  return seedHash_.size() && headerHash_.size();
}

string StratumJobEth::serializeToJson() const {
  return Strings::Format(
      "{\"jobId\":%u"

      ",\"chain\":\"%s\""
      ",\"height\":%u"
      ",\"parent\":\"%s\""

      ",\"networkTarget\":\"0x%s\""
      ",\"headerHash\":\"%s\""
      ",\"sHash\":\"%s\""

      ",\"uncles\":\"%u\""
      ",\"transactions\":\"%u\""
      ",\"gasUsedPercent\":\"%f\""

      "%s"

      ",\"rpcAddress\":\"%s\""
      ",\"rpcUserPwd\":\"%s\""

      // backward compatible
      ",\"rskNetworkTarget\":\"0x%s\""
      ",\"rskBlockHashForMergedMining\":\"%s\""
      ",\"rskFeesForMiner\":\"\""
      ",\"rskdRpcAddress\":\"\""
      ",\"rskdRpcUserPwd\":\"\""
      ",\"isRskCleanJob\":false"
      "}",
      jobId_,

      EthConsensus::getChainStr(chain_),
      height_,
      parent_,

      networkTarget_.GetHex(),
      headerHash_,
      seedHash_,

      uncles_,
      transactions_,
      gasUsedPercent_,

      header_.empty() ? "" : Strings::Format(",\"header\":\"%s\"", header_),

      rpcAddress_,
      rpcUserPwd_,

      // backward compatible
      networkTarget_.GetHex(),
      headerHash_);
}

bool StratumJobEth::unserializeFromJson(const char *s, size_t len) {
  JsonNode j;
  if (!JsonNode::parse(s, s + len, j)) {
    return false;
  }

  if (j["jobId"].type() != Utilities::JS::type::Int ||
      j["chain"].type() != Utilities::JS::type::Str ||
      j["height"].type() != Utilities::JS::type::Int ||
      j["networkTarget"].type() != Utilities::JS::type::Str ||
      j["headerHash"].type() != Utilities::JS::type::Str ||
      j["sHash"].type() != Utilities::JS::type::Str) {
    LOG(ERROR) << "parse eth stratum job failure: " << s;
    return false;
  }

  jobId_ = j["jobId"].uint64();
  chain_ = EthConsensus::getChain(j["chain"].str());
  height_ = j["height"].uint64();
  networkTarget_ = uint256S(j["networkTarget"].str());
  headerHash_ = j["headerHash"].str();
  seedHash_ = j["sHash"].str();

  if (j["parent"].type() == Utilities::JS::type::Str &&
      j["uncles"].type() == Utilities::JS::type::Int &&
      j["transactions"].type() == Utilities::JS::type::Int &&
      j["gasUsedPercent"].type() == Utilities::JS::type::Real) {
    parent_ = j["parent"].str();
    uncles_ = j["uncles"].uint32();
    transactions_ = j["transactions"].uint32();
    gasUsedPercent_ = j["gasUsedPercent"].real();
  }

  if (j["header"].type() == Utilities::JS::type::Str) {
    header_ = HexStripPrefix(j["header"].str());
    if (IsHex(header_)) {
      headerBin_ = ParseHex(header_);
    } else {
      header_.clear();
    }
  }

  if (j["rpcAddress"].type() == Utilities::JS::type::Str &&
      j["rpcUserPwd"].type() == Utilities::JS::type::Str) {
    rpcAddress_ = j["rpcAddress"].str();
    rpcUserPwd_ = j["rpcUserPwd"].str();
  }

  return true;
}

string StratumJobEth::getHeaderHashWithExtraNonce(uint32_t extraNonce) const {
  if (header_.empty()) {
    return headerHash_;
  } else {
    boost::endian::little_uint32_buf_t extraNonceBuf{extraNonce};
    std::copy_n(extraNonceBuf.data(), 4, headerBin_.rbegin());
    uint8_t hash[32];
    sha3_256(hash, 32, headerBin_.data(), headerBin_.size());
    string headerHash;
    Bin2Hex(hash, 32, headerHash);
    return headerHash;
  }
}
