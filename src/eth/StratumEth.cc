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

///////////////////////////////StratumJobEth///////////////////////////
StratumJobEth::StratumJobEth()
{

}

bool StratumJobEth::initFromGw(const RskWorkEth &latestRskBlockJson, EthConsensus::Chain chain)
{
  if (latestRskBlockJson.isInitialized())
  {
    chain_ = chain;
    blockHashForMergedMining_ = latestRskBlockJson.getBlockHash();
    rskNetworkTarget_ = uint256S(latestRskBlockJson.getTarget());
    feesForMiner_ = latestRskBlockJson.getFees();
    rskdRpcAddress_ = latestRskBlockJson.getRpcAddress();
    rskdRpcUserPwd_ = latestRskBlockJson.getRpcUserPwd();
    isRskCleanJob_ = latestRskBlockJson.getIsCleanJob();
    seedHash_ = latestRskBlockJson.getSeedHash();
    height_ = latestRskBlockJson.getHeight();

    // generate job id
    string header = blockHashForMergedMining_.substr(2, 64);
    uint32 h = djb2(header.c_str());
    DLOG(INFO) << "djb2=" << std::hex << h << " for header " << header;
    // jobId: timestamp + hash of header
    const string jobIdStr = Strings::Format("%08x%08x",
                                            (uint32_t)time(nullptr),
                                            h);
    DLOG(INFO) << "job id string: " << jobIdStr;
    assert(jobIdStr.length() == 16);
    
    jobId_ = stoull(jobIdStr, nullptr, 16 /* hex */);
  }
  return seedHash_.size() && blockHashForMergedMining_.size();
}

string StratumJobEth::serializeToJson() const {
  return Strings::Format("{\"jobId\":%" PRIu64""
                         ",\"chain\":\"%s\""
                         ",\"height\":%" PRIu64""
                         ",\"sHash\":\"%s\""
                         ",\"rskBlockHashForMergedMining\":\"%s\",\"rskNetworkTarget\":\"0x%s\""
                         ",\"rskFeesForMiner\":\"%s\""
                         ",\"rskdRpcAddress\":\"%s\",\"rskdRpcUserPwd\":\"%s\""
                         ",\"isRskCleanJob\":%s"
                         "}",
                         jobId_,
                         EthConsensus::getChainStr(chain_).c_str(),
                         height_,
                         // rsk
                         seedHash_.c_str(),
                         blockHashForMergedMining_.size() ? blockHashForMergedMining_.c_str() : "",
                         rskNetworkTarget_.GetHex().c_str(),
                         feesForMiner_.size()             ? feesForMiner_.c_str()             : "",
                         rskdRpcAddress_.size()           ? rskdRpcAddress_.c_str()           : "",
                         rskdRpcUserPwd_.c_str()          ? rskdRpcUserPwd_.c_str()           : "",
                         isRskCleanJob_ ? "true" : "false");
}

bool StratumJobEth::unserializeFromJson(const char *s, size_t len)
{
  JsonNode j;
  if (!JsonNode::parse(s, s + len, j))
  {
    return false;
  }
  if (j["jobId"].type() != Utilities::JS::type::Int ||
      j["chain"].type() != Utilities::JS::type::Str ||
      j["height"].type() != Utilities::JS::type::Int ||
      j["sHash"].type() != Utilities::JS::type::Str)
  {
    LOG(ERROR) << "parse eth stratum job failure: " << s;
    return false;
  }

  jobId_ = j["jobId"].uint64();
  chain_ = EthConsensus::getChain(j["chain"].str());
  height_ = j["height"].uint64();
  seedHash_ = j["sHash"].str();

  if (j["rskBlockHashForMergedMining"].type() == Utilities::JS::type::Str &&
      j["rskNetworkTarget"].type() == Utilities::JS::type::Str &&
      j["rskFeesForMiner"].type() == Utilities::JS::type::Str &&
      j["rskdRpcAddress"].type() == Utilities::JS::type::Str &&
      j["rskdRpcUserPwd"].type() == Utilities::JS::type::Str &&
      j["isRskCleanJob"].type() == Utilities::JS::type::Bool)
  {
    blockHashForMergedMining_ = j["rskBlockHashForMergedMining"].str();
    rskNetworkTarget_ = uint256S(j["rskNetworkTarget"].str());
    feesForMiner_ = j["rskFeesForMiner"].str();
    rskdRpcAddress_ = j["rskdRpcAddress"].str();
    rskdRpcUserPwd_ = j["rskdRpcUserPwd"].str();
    isRskCleanJob_ = j["isRskCleanJob"].boolean();
  }

  BitsToTarget(nBits_, networkTarget_); //  Gani#Question: Does eth require this call?

  return true;
}