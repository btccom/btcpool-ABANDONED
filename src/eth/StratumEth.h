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
#include "eth/eth.pb.h"

#include <boost/optional.hpp>

#include <rlpvalue.h>
#include <uint256.h>

// [[[[ IMPORTANT REMINDER! ]]]]
// Please keep the Share structure forward compatible.
// That is: don't change it unless you add code so that
// both the modified and non-modified Shares can be processed.
// Please note that in the usual upgrade, the old version of Share
// and the new version will coexist for a while.
// If there is no forward compatibility, one of the versions of Share
// will be considered invalid, resulting in loss of users' hashrate.

class ShareEthBytesVersion {
public:
  uint32_t version_ = 0; // 0
  uint32_t checkSum_ = 0; // 4

  int64_t workerHashId_ = 0; // 8
  int32_t userId_ = 0; // 16
  int32_t status_ = 0; // 20
  int64_t timestamp_ = 0; // 24
  IpAddress ip_ = 0; // 32

  uint64_t headerHash_ = 0; // 48
  uint64_t shareDiff_ = 0; // 56
  uint64_t networkDiff_ = 0; // 64
  uint64_t nonce_ = 0; // 72
  uint32_t sessionId_ = 0; // 80
  uint32_t height_ = 0; // 84

  uint32_t checkSum() const {
    uint64_t c = 0;

    c += (uint64_t)version_;
    c += (uint64_t)workerHashId_;
    c += (uint64_t)userId_;
    c += (uint64_t)status_;
    c += (uint64_t)timestamp_;
    c += (uint64_t)ip_.addrUint64[0];
    c += (uint64_t)ip_.addrUint64[1];
    c += (uint64_t)headerHash_;
    c += (uint64_t)shareDiff_;
    c += (uint64_t)networkDiff_;
    c += (uint64_t)nonce_;
    c += (uint64_t)sessionId_;
    c += (uint64_t)height_;

    return ((uint32_t)c) + ((uint32_t)(c >> 32));
  }
};

class ShareEth : public sharebase::Serializable<sharebase::EthMsg> {
public:
  const static uint32_t CURRENT_VERSION_FOUNDATION =
      0x00110003u; // first 0011: ETH, second 0002: version 3
  const static uint32_t CURRENT_VERSION_CLASSIC =
      0x00160003u; // first 0016: ETC, second 0002: version 3
  const static uint32_t BYTES_VERSION_FOUNDATION =
      0x00110002u; // first 0011: ETH, second 0002: version 3
  const static uint32_t BYTES_VERSION_CLASSIC =
      0x00160002u; // first 0016: ETC, second 0002: version 3

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
    case BYTES_VERSION_FOUNDATION:
      return EthConsensus::Chain::FOUNDATION;
    case CURRENT_VERSION_CLASSIC:
    case BYTES_VERSION_CLASSIC:
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

  EthConsensus::Chain getChain() const { return getChain(version()); }

  double score() const {

    if (!StratumStatus::isAccepted(status()) || sharediff() == 0 ||
        networkdiff() == 0) {
      return 0.0;
    }

    double result = 0.0;

    // Network diff may less than share diff on testnet or regression test
    // network. On regression test network, the network diff may be zero. But no
    // matter how low the network diff is, you can only dig one block at a time.
    if (networkdiff() < sharediff()) {
      result = 1.0;
    } else {
      result = (double)sharediff() / (double)networkdiff();
    }

    // Share of the uncle block has a lower reward.
    if (StratumStatus::isAcceptedStale(status())) {
      result *= EthConsensus::getUncleBlockRewardRatio(height(), getChain());
    }

    return result;
  }

  bool isValid() const {

    if (version() != CURRENT_VERSION_FOUNDATION &&
        version() != CURRENT_VERSION_CLASSIC) {
      return false;
    }

    if (userid() == 0 || workerhashid() == 0 || height() == 0 ||
        networkdiff() == 0 || sharediff() == 0) {
      return false;
    }

    return true;
  }

  string toString() const {

    return Strings::Format(
        "share(height: %u, headerHash: %016x..., ip: %s, userId: %d, "
        "workerId: %d, time: %u/%s, "
        "shareDiff: %u, networkDiff: %u, nonce: %016x, "
        "sessionId: %08x, status: %d/%s)",
        height(),
        headerhash(),
        ip(),
        userid(),
        workerhashid(),
        timestamp(),
        date("%F %T", timestamp()),
        sharediff(),
        networkdiff(),
        nonce(),
        sessionid(),
        status(),
        StratumStatus::toString(status()));
  }

  bool UnserializeWithVersion(const uint8_t *data, uint32_t size) {

    if (nullptr == data || size <= 0) {
      return false;
    }

    const uint8_t *payload = data;
    uint32_t version = *((uint32_t *)payload);

    if (version == CURRENT_VERSION_FOUNDATION ||
        version == CURRENT_VERSION_CLASSIC) {

      if (!ParseFromArray(
              (const uint8_t *)(payload + sizeof(uint32_t)),
              size - sizeof(uint32_t))) {
        DLOG(INFO) << "share ParseFromArray failed!";
        return false;
      }
    } else if (
        (version == BYTES_VERSION_FOUNDATION ||
         version == BYTES_VERSION_CLASSIC) &&
        size == sizeof(ShareEthBytesVersion)) {

      ShareEthBytesVersion *share = (ShareEthBytesVersion *)payload;

      if (share->checkSum() != share->checkSum_) {
        DLOG(INFO) << "checkSum mismatched! checkSum_: " << share->checkSum_
                   << ", checkSum(): " << share->checkSum();
        return false;
      }

      set_version(getVersion(getChain(share->version_)));
      set_workerhashid(share->workerHashId_);
      set_userid(share->userId_);
      set_status(share->status_);
      set_timestamp(share->timestamp_);
      set_ip(share->ip_.toString());
      set_headerhash(share->headerHash_);
      set_sharediff(share->shareDiff_);
      set_networkdiff(share->networkDiff_);
      set_nonce(share->nonce_);
      set_sessionid(share->sessionId_);
      set_height(share->height_);

    } else {

      DLOG(INFO) << "unknow share received! data size: " << size;
      return false;
    }

    return true;
  }
};

class StratumJobEth : public StratumJob {
public:
  StratumJobEth();
  string serializeToJson() const override;
  bool unserializeFromJson(const char *s, size_t len) override;
  uint64_t height() const override { return height_; }
  bool initFromGw(
      const RskWorkEth &latestRskBlockJson,
      EthConsensus::Chain chain,
      uint8_t serverId);

  string getHeaderWithExtraNonce(
      uint32_t extraNonce1, const boost::optional<uint32_t> &extraNonce2) const;
  bool hasHeader() const;

  EthConsensus::Chain chain_ = EthConsensus::Chain::UNKNOWN;
  uint32_t height_ = 0;
  string parent_;

  uint256 networkTarget_;
  string headerHash_;
  string seedHash_;

  uint32_t uncles_ = 0;
  uint32_t transactions_ = 0;
  float gasUsedPercent_ = 0.0;

  string header_;
  RLPValue headerNoExtraData_;
  string extraData_;

  string rpcAddress_;
  string rpcUserPwd_;
};

// shares submitted by this session, for duplicate share check
struct LocalShareEth {
  uint64_t exNonce2_; // extra nonce2 fixed 8 bytes

  LocalShareEth(uint64_t exNonce2)
    : exNonce2_(exNonce2) {}

  LocalShareEth &operator=(const LocalShareEth &other) {
    exNonce2_ = other.exNonce2_;
    return *this;
  }

  bool operator<(const LocalShareEth &r) const {
    if (exNonce2_ < r.exNonce2_) {
      return true;
    }
    return false;
  }
};

class ServerEth;
class StratumSessionEth;

struct StratumTraitsEth {
  using ServerType = ServerEth;
  using SessionType = StratumSessionEth;
  using LocalShareType = LocalShareEth;
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
  struct LocalJobType : public LocalJobBase<LocalShareType> {
    LocalJobType(size_t chainId, uint64_t jobId, const std::string &headerHash)
      : LocalJobBase<LocalShareType>(chainId, jobId)
      , headerHash_(headerHash) {}
    bool operator==(const std::string &headerHash) const {
      return headerHash_ == headerHash;
    }

    std::string headerHash_;
  };
};

enum class StratumProtocolEth {
  ETHPROXY,
  STRATUM,
  // @see https://www.nicehash.com/sw/Ethereum_specification_R1.txt
  NICEHASH_STRATUM,
};

inline const char *getProtocolString(StratumProtocolEth protocol) {
  switch (protocol) {
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
