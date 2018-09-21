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
#ifndef STRATUM_ETH_H_
#define STRATUM_ETH_H_

#include "Stratum.h"
#include "EthConsensus.h"

#include "rsk/RskWork.h"

#include <uint256.h>

// [[[[ IMPORTANT REMINDER! ]]]]
// Please keep the Share structure forward compatible.
// That is: don't change it unless you add code so that
// both the modified and non-modified Shares can be processed.
// Please note that in the usual upgrade, the old version of Share
// and the new version will coexist for a while.
// If there is no forward compatibility, one of the versions of Share
// will be considered invalid, resulting in loss of users' hashrate.
class ShareEth
{
public:

  const static uint32_t CURRENT_VERSION_FOUNDATION = 0x00110002u; // first 0011: ETH, second 0002: version 2
  const static uint32_t CURRENT_VERSION_CLASSIC    = 0x00160002u; // first 0016: ETC, second 0002: version 2

  uint32_t  version_      = 0;
  uint32_t  checkSum_     = 0;

  int64_t   workerHashId_ = 0;
  int32_t   userId_       = 0;
  int32_t   status_       = 0;
  int64_t   timestamp_    = 0;
  IpAddress ip_           = 0;

  uint64_t headerHash_  = 0;
  uint64_t shareDiff_   = 0;
  uint64_t networkDiff_ = 0;
  uint64_t nonce_       = 0;
  uint32_t sessionId_   = 0;
  uint32_t height_      = 0;

  ShareEth() = default;
  ShareEth(const ShareEth &r) = default;
  ShareEth &operator=(const ShareEth &r) = default;

  inline static EthConsensus::Chain getChain(uint32_t version) {
    switch (version) {
    case CURRENT_VERSION_FOUNDATION:
      return EthConsensus::Chain::FOUNDATION;
    case CURRENT_VERSION_CLASSIC:
      return EthConsensus::Chain::CLASSIC;
    default:
      return EthConsensus::Chain::UNKNOWN;
    }
  }

  inline static uint32_t getVersion(EthConsensus::Chain chain) {
    switch (chain) {
      case EthConsensus::Chain::FOUNDATION:
        return CURRENT_VERSION_FOUNDATION;
      case EthConsensus::Chain::CLASSIC:
        return CURRENT_VERSION_CLASSIC;
      case EthConsensus::Chain::UNKNOWN:
        LOG(FATAL) << "Unknown chain";
        return 0;
    }
    // should not be here
    LOG(FATAL) << "Inexpectant const value";
    return 0;
  }

  EthConsensus::Chain getChain() const {
    return getChain(version_);
  }

  double score() const
  {
    if (!StratumStatus::isAccepted(status_) || shareDiff_ == 0 || networkDiff_ == 0) {
      return 0.0;
    }

    double result = 0.0;

    // Network diff may less than share diff on testnet or regression test network.
    // On regression test network, the network diff may be zero.
    // But no matter how low the network diff is, you can only dig one block at a time.
    if (networkDiff_ < shareDiff_) {
      result = 1.0;
    }
    else {
      result = (double)shareDiff_ / (double)networkDiff_;
    }

    // Share of the uncle block has a lower reward.
    if (StratumStatus::isStale(status_)) {
      result *= EthConsensus::getUncleBlockRewardRatio(height_, getChain());
    }

    return result;
  }

  uint32_t checkSum() const {
    uint64_t c = 0;

    c += (uint64_t) version_;
    c += (uint64_t) workerHashId_;
    c += (uint64_t) userId_;
    c += (uint64_t) status_;
    c += (uint64_t) timestamp_;
    c += (uint64_t) ip_.addrUint64[0];
    c += (uint64_t) ip_.addrUint64[1];
    c += (uint64_t) headerHash_;
    c += (uint64_t) shareDiff_;
    c += (uint64_t) networkDiff_;
    c += (uint64_t) nonce_;
    c += (uint64_t) sessionId_;
    c += (uint64_t) height_;

    return ((uint32_t) c) + ((uint32_t) (c >> 32));
  }

  bool isValid() const
  {
    if (version_ != CURRENT_VERSION_FOUNDATION && version_ != CURRENT_VERSION_CLASSIC) {
      return false;
    }

    if (checkSum_ != checkSum()) {
      DLOG(INFO) << "checkSum mismatched! checkSum_: " << checkSum_ << ", checkSum(): " << checkSum();
      return false;
    }

    if (userId_ == 0 || workerHashId_ == 0 || height_ == 0 ||
        networkDiff_ == 0 || shareDiff_ == 0)
    {
      return false;
    }
    
    return true;
  }

  string toString() const
  {
    return Strings::Format("share(height: %u, headerHash: %016" PRIx64 "..., ip: %s, userId: %d, "
                           "workerId: %" PRId64 ", time: %u/%s, "
                           "shareDiff: %" PRIu64 ", networkDiff: %" PRIu64 ", nonce: %016" PRIx64 ", "
                           "sessionId: %08x, status: %d/%s)",
                           height_, headerHash_, ip_.toString().c_str(), userId_,
                           workerHashId_, timestamp_, date("%F %T", timestamp_).c_str(),
                           shareDiff_, networkDiff_, nonce_,
                           sessionId_, status_, StratumStatus::toString(status_));
  }
};

static_assert(sizeof(ShareEth) == 88, "ShareEth should be 88 bytes");

class StratumJobEth : public StratumJob
{
public:
  StratumJobEth();
  string serializeToJson() const override;
  bool unserializeFromJson(const char *s, size_t len) override;
  bool initFromGw(const RskWorkEth &latestRskBlockJson, EthConsensus::Chain chain, uint8_t serverId);

  EthConsensus::Chain chain_ = EthConsensus::Chain::UNKNOWN;
  uint32_t height_ = 0;
  string parent_;

  uint256 networkTarget_;
  string headerHash_;
  string seedHash_;

  uint32_t uncles_ = 0;
  uint32_t transactions_ = 0;
  float gasUsedPercent_ = 0.0;

  string rpcAddress_;
  string rpcUserPwd_;
};

class ServerEth;
class StratumSessionEth;

struct StratumTraitsEth {
  using ServerType = ServerEth;
  using SessionType = StratumSessionEth;
  struct JobDiffType {
    // difficulty of this job (due to difficulty adjustment,
    // there can be multiple diffs in the same job)
    uint64_t currentJobDiff_;
    std::set<uint64_t> jobDiffs_;

    JobDiffType &operator=(uint64_t diff) {
      jobDiffs_.insert(diff);
      currentJobDiff_ = diff;
      return *this;
    }
  };
  struct LocalJobType : public LocalJob {
    LocalJobType(uint64_t jobId, const std::string &headerHash)
        : LocalJob(jobId), headerHash_(headerHash), currentJobDiff_(0) {
    }
    bool operator==(const std::string &headerHash) const { return headerHash_ == headerHash; }

    std::string headerHash_;
    uint64_t currentJobDiff_;
  };
};

enum class StratumProtocolEth {
  ETHPROXY,
  STRATUM,
  // @see https://www.nicehash.com/sw/Ethereum_specification_R1.txt
  NICEHASH_STRATUM,
};

inline const char* getProtocolString(StratumProtocolEth protocol) {
  switch(protocol) {
  case StratumProtocolEth::ETHPROXY:
    return "ETHPROXY";
  case StratumProtocolEth::STRATUM:
    return "STRATUM";
  case StratumProtocolEth::NICEHASH_STRATUM:
    return "NICEHASH_STRATUM";
  }
  // should not be here
  return "UNKNOWN";
}

#endif
