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

#pragma once

#include "CommonGrin.h"

#include "grin/grin.pb.h"
#include "Stratum.h"

#include "utilities_js.hpp"

// [[[[ IMPORTANT REMINDER! ]]]]
// Please keep the Share structure forward compatible.
// That is: don't change it unless you add code so that
// both the modified and non-modified Shares can be processed.
// Please note that in the usual upgrade, the old version of Share
// and the new version will coexist for a while.
// If there is no forward compatibility, one of the versions of Share
// will be considered invalid, resulting in loss of users' hashrate.

class ShareGrin
  : public sharebase::Unserializable<ShareGrin, sharebase::GrinMsg> {
public:
  const static uint32_t CURRENT_VERSION =
      0x00400001u; // first 0040: Grin, second 0001: version 1

  ShareGrin() {
    set_version(0);
    set_workerhashid(0);
    set_userid(0);
    set_status(0);
    set_timestamp(0);
    set_ip("0.0.0.0");
    set_jobid(0);
    set_sharediff(0);
    set_blockdiff(0);
    set_height(0);
    set_nonce(0);
    set_sessionid(0);
    set_edgebits(0);
    set_scaling(0);
    set_hashprefix(0);
  }
  ShareGrin(const ShareGrin &r) = default;
  ShareGrin &operator=(const ShareGrin &r) = default;

  // Grin applies scaling when checking proof hash difficulty, to mitigate the
  // solving cost. The scaling is dynamic and is determined by height, edge bits
  // and secondary scaling field in job pre-PoW.
  uint32_t scaledShareDiff() const { return sharediff() * scaling(); }

  double score() const {
    if (!StratumStatus::isAccepted(status()) || scaledShareDiff() == 0 ||
        scaling() == 0 || blockdiff() == 0) {
      return 0.0;
    }

    // Network diff may less than share diff on testnet or regression test
    // network. On regression test network, the network diff may be zero. But no
    // matter how low the network diff is, you can only dig one block at a time.
    double networkDiff = blockdiff();
    double jobDiff = scaledShareDiff();
    if (networkDiff < jobDiff) {
      return 1.0;
    } else {
      return (double)jobDiff / networkDiff;
    }
  }

  bool isValid() const {
    if (version() != CURRENT_VERSION) {
      return false;
    }
    if (userid() == 0 || workerhashid() == 0 || blockdiff() == 0 ||
        scaledShareDiff() == 0 || scaling() == 0) {
      return false;
    }
    return true;
  }

  string toString() const {
    return Strings::Format(
        "share(height: %u, jobId: %u..., ip: %s, userId: %d, "
        "workerId: %d, time: %u/%s, "
        "shareDiff: %u, networkDiff: %u, nonce: %016x, "
        "sessionId: %08x, status: %d/%s)",
        height(),
        jobid(),
        ip(),
        userid(),
        workerhashid(),
        timestamp(),
        date("%F %T", timestamp()),
        scaledShareDiff(),
        blockdiff(),
        nonce(),
        sessionid(),
        status(),
        StratumStatus::toString(status()));
  }
};

class StratumJobGrin : public StratumJob {
public:
  StratumJobGrin();

  bool initFromRawJob(JsonNode &jparams);

  string serializeToJson() const override;
  bool unserializeFromJson(const char *s, size_t len) override;
  uint64_t height() const override { return height_; }
  string prePowStr(uint64_t difficulty) const;

  uint64_t height_;
  uint64_t nodeJobId_;
  uint64_t difficulty_;
  PrePowGrin prePow_;
  string prePowStr_;
};

// shares submitted by this session, for duplicate share check
struct LocalShareGrin {
  uint64_t exNonce2_; // extra nonce2 fixed 8 bytes
  uint32_t nonce_; // nonce in block header
  uint32_t time_; // nTime in block header

  LocalShareGrin(uint64_t exNonce2, uint32_t nonce, uint32_t time)
    : exNonce2_(exNonce2)
    , nonce_(nonce)
    , time_(time) {}

  LocalShareGrin &operator=(const LocalShareGrin &other) {
    exNonce2_ = other.exNonce2_;
    nonce_ = other.nonce_;
    time_ = other.time_;
    return *this;
  }

  bool operator<(const LocalShareGrin &r) const {
    if (exNonce2_ < r.exNonce2_ ||
        (exNonce2_ == r.exNonce2_ && nonce_ < r.nonce_) ||
        (exNonce2_ == r.exNonce2_ && nonce_ == r.nonce_ && time_ < r.time_)) {
      return true;
    }
    return false;
  }
};

class StratumServerGrin;
class StratumSessionGrin;

struct StratumTraitsGrin {
  using ServerType = StratumServerGrin;
  using SessionType = StratumSessionGrin;
  using JobDiffType = uint64_t;
  using LocalShareType = LocalShareGrin;

  struct LocalJobType : public LocalJobBase<LocalShareType> {
    LocalJobType(size_t chainId, uint64_t jobId, uint32_t prePowHash)
      : LocalJobBase<LocalShareType>(chainId, jobId)
      , prePowHash_(prePowHash) {}
    bool operator==(uint64_t prePowHash) const {
      return prePowHash_ == prePowHash;
    }

    uint32_t prePowHash_;
  };
};

enum class AlgorithmGrin {
  Unknown,
  Cuckaroo,
  Cuckatoo,
};
