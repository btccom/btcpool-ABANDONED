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
#include "share.pro.pb.h"
#include <uint256.h>

// [[[[ IMPORTANT REMINDER! ]]]]
// Please keep the Share structure forward compatible.
// That is: don't change it unless you add code so that
// both the modified and non-modified Shares can be processed.
// Please note that in the usual upgrade, the old version of Share
// and the new version will coexist for a while.
// If there is no forward compatibility, one of the versions of Share
// will be considered invalid, resulting in loss of users' hashrate.


class ShareEth : public sharebase::EthMsg
{
public:

  const static uint32_t CURRENT_VERSION_FOUNDATION = 0x00110002u; // first 0011: ETH, second 0002: version 2
  const static uint32_t CURRENT_VERSION_CLASSIC    = 0x00160002u; // first 0016: ETC, second 0002: version 2


  ShareEth() {
    set_version(0);
    set_workerhashid(0);
    set_userid(0);
    set_status(0);
    set_timestamp(0);
    set_ip("0.0.0.0");
    set_headerhash(0);
    set_sharediff(0);
    set_networkdiff(0);
    set_height(0);
    set_nonce(0);
    set_sessionid(0);
  }
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
    if (!IsInitialized()) {
      DLOG(INFO) << "share_ is not  Initialize" ;
      return EthConsensus::Chain::UNKNOWN;
    }

    return getChain(version());
  }

  double score() const
  {

    if (!IsInitialized()) {
      DLOG(INFO) << "share_ is not  Initialize" ;
      return 0.0;
    }


    if (!StratumStatus::isAccepted(status()) || sharediff() == 0 || networkdiff() == 0) {
      return 0.0;
    }

    double result = 0.0;

    // Network diff may less than share diff on testnet or regression test network.
    // On regression test network, the network diff may be zero.
    // But no matter how low the network diff is, you can only dig one block at a time.
    if (networkdiff() < sharediff()) {
      result = 1.0;
    }
    else {
      result = (double)sharediff() / (double)networkdiff();
    }

    // Share of the uncle block has a lower reward.
    if (StratumStatus::isStale(status())) {
      result *= EthConsensus::getUncleBlockRewardRatio(height(), getChain());
    }

    return result;
  }


  bool isValid() const
  {

    if (version() != CURRENT_VERSION_FOUNDATION && version() != CURRENT_VERSION_CLASSIC) {
      return false;
    }

    if (userid() == 0 || workerhashid() == 0 || height() == 0 ||
        networkdiff() == 0 || sharediff() == 0)
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
                           height(), headerhash(), ip().c_str(), userid(),
                           workerhashid(), timestamp(), date("%F %T", timestamp()).c_str(),
                           sharediff(), networkdiff(), nonce(),
                           sessionid(), status(), StratumStatus::toString(status()));
  }


  bool SerializeToBuffer(string& data, uint32_t& size) const{
    size = ByteSize();
    data.resize(size);

    if (!SerializeToArray((uint8_t *)data.data(), size)) {
        DLOG(INFO) << "base.SerializeToArray failed!" << std::endl;
        return false;
      }

    return true;
  }

  bool SerializeToArrayWithLength(string& data, uint32_t& size) const {
    size = ByteSize();
    data.resize(size + sizeof(uint32_t));

    *((uint32_t*)data.data()) = size;
    uint8_t * payload = (uint8_t *)data.data();

    if (!SerializeToArray(payload + sizeof(uint32_t), size)) {
       DLOG(INFO) << "base.SerializeToArray failed!";
      return false;
    }
      
    size += sizeof(uint32_t);
    return true;
  }

  size_t getsharelength() {
    return IsInitialized() ? ByteSize() : 0;
  }
};



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
        : LocalJob(jobId), headerHash_(headerHash) {
    }
    bool operator==(const std::string &headerHash) const { return headerHash_ == headerHash; }

    std::string headerHash_;
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
