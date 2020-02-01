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

#include <boost/endian/buffers.hpp>

static const size_t ETH_HEADER_FIELDS = 13;

///////////////////////////////StratumJobEth///////////////////////////
StratumJobEth::StratumJobEth()
  : headerNoExtraData_{RLPValue::VARR} {
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

      ",\"uncles\":%u"
      ",\"transactions\":%u"
      ",\"gasUsedPercent\":%f"

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
      auto headerBin = ParseHex(header_);
      size_t consumed = 0;
      size_t wanted = 0;
      RLPValue headerValue;
      if (headerValue.read(
              &headerBin.front(), headerBin.size(), consumed, wanted) &&
          headerValue.size() == ETH_HEADER_FIELDS &&
          headerValue[ETH_HEADER_FIELDS - 1].type() == RLPValue::VBUF) {
        for (size_t i = 0; i < ETH_HEADER_FIELDS - 1; ++i) {
          headerNoExtraData_.push_back(headerValue[i]);
        }
        extraData_ = headerValue[ETH_HEADER_FIELDS - 1].get_str();
        if (extraData_.size() >= 4 &&
            extraData_.find_first_not_of('\0', extraData_.size() - 4) ==
                std::string::npos) {
          // Remove the substitutable zeros
          extraData_ = extraData_.substr(0, extraData_.size() - 4);
        }
      } else {
        LOG(ERROR) << "Decoding RLP failed for block header";
      }
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

string StratumJobEth::getHeaderWithExtraNonce(
    uint32_t extraNonce1, const boost::optional<uint32_t> &extraNonce2) const {
  boost::endian::big_uint32_buf_t extraNonce1Buf{extraNonce1};
  RLPValue headerValue{headerNoExtraData_};
  std::string extraData{extraData_};
  extraData.append(reinterpret_cast<const char *>(extraNonce1Buf.data()), 4);
  if (extraNonce2) {
    boost::endian::big_uint32_buf_t extraNonce2Buf{*extraNonce2};
    extraData.append(reinterpret_cast<const char *>(extraNonce2Buf.data()), 4);
  }
  headerValue.push_back(RLPValue{extraData});
  return headerValue.write();
}

bool StratumJobEth::hasHeader() const {
  return !headerNoExtraData_.empty();
}
