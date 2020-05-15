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
#ifndef STRATUM_CKB_H_
#define STRATUM_CKB_H_

#include "Stratum.h"
#include "CommonCkb.h"

#include "ckb/ckb.pb.h"
#include <uint256.h>

class ShareCkb : public sharebase::Unserializable<ShareCkb, sharebase::CkbMsg> {
public:
  const static uint32_t CURRENT_VERSION =
      0x0cb0001u; // first 0cb0: Ckb, second 0001: version 1

  ShareCkb() {
    set_version(0);
    set_workerhashid(0);
    set_userid(0);
    set_status(0);
    set_timestamp(0);
    set_ip("0.0.0.0");
    set_sharediff(0);
    set_blockbits(0);
    set_height(0);
    set_nonce(0);
    set_sessionid(0);
    set_username("");
    set_workername("");
  }
  ShareCkb(const ShareCkb &r) = default;
  ShareCkb &operator=(const ShareCkb &r) = default;

  double score() const {

    if (!StratumStatus::isAccepted(status()) || sharediff() == 0 ||
        blockbits() == 0) {
      return 0.0;
    }

    // Network diff may less than share diff on testnet or regression test
    // network. On regression test network, the network diff may be zero. But no
    // matter how low the network diff is, you can only dig one block at a time.

    double networkDiff = blockdiff();
    // CkbDifficulty::BitsToDifficulty(blockbits(), &networkDiff);

    if (networkDiff < sharediff()) {
      return 1.0;
    } else {
      return (double)sharediff() / networkDiff;
    }
  }

  bool isValid() const {
    if (version() != CURRENT_VERSION) {
      return false;
    }
    if (workerhashid() == 0 || blockbits() == 0 || sharediff() == 0) {
      return false;
    }
    return true;
  }

  string toString() const {
    return Strings::Format(
        "share(jobId: %" PRIu64 ",height: %" PRIu64
        ",..., ip: %s, userId: %d, "
        "workerId: %" PRId64
        ", time: %u/%s, "
        "shareDiff: %" PRIu64 ", blockBits: %" PRIu64 ", nonce: %016" PRIx64
        ", "
        "sessionId: %08x, status: %d/%s, username: %s, workername: %s)",
        jobid(),
        height(),
        ip().c_str(),
        userid(),
        workerhashid(),
        timestamp(),
        date("%F %T", timestamp()).c_str(),
        sharediff(),
        blockbits(),
        nonce(),
        sessionid(),
        status(),
        StratumStatus::toString(status()),
        username(),
        workername());
  }
};

class StratumJobCkb : public StratumJob {
public:
  StratumJobCkb();
  string serializeToJson() const override;
  bool unserializeFromJson(const char *s, size_t len) override;
  bool initFromRawJob(const string &msg);
  uint64_t height() const override { return height_; }

  string pow_hash_;
  string parent_hash_;
  uint64_t height_;
  uint64_t work_id_;
  string target_;
  uint64_t nTime_;
  uint64_t timestamp_;
};

// shares submitted by this session, for duplicate share check
struct LocalShareCkb {
  uint64_t exNonce2_; // extra nonce2 fixed 8 bytes
  uint32_t nonce_; // nonce in block header
  uint32_t time_; // nTime in block header

  LocalShareCkb(uint64_t exNonce2, uint32_t nonce, uint32_t time)
    : exNonce2_(exNonce2)
    , nonce_(nonce)
    , time_(time) {}

  LocalShareCkb &operator=(const LocalShareCkb &other) {
    exNonce2_ = other.exNonce2_;
    nonce_ = other.nonce_;
    time_ = other.time_;
    return *this;
  }

  bool operator<(const LocalShareCkb &r) const {
    if (exNonce2_ < r.exNonce2_ ||
        (exNonce2_ == r.exNonce2_ && nonce_ < r.nonce_) ||
        (exNonce2_ == r.exNonce2_ && nonce_ == r.nonce_ && time_ < r.time_)) {
      return true;
    }
    return false;
  }
};

class StratumServerCkb;
class StratumSessionCkb;

struct StratumTraitsCkb {
  using ServerType = StratumServerCkb;
  using SessionType = StratumSessionCkb;
  using LocalShareType = LocalShareCkb;
  struct LocalJobType : public LocalJobBase<LocalShareType> {
    LocalJobType(size_t chainId, uint64_t jobId)
      : LocalJobBase<LocalShareType>(chainId, jobId) {}

    // copy from LocalJob
    bool operator==(uint64_t jobId) const { return jobId_ == jobId; }
  };

  struct JobDiffType {
    uint64_t currentJobDiff_;
    std::set<uint64_t> jobDiffs_;

    JobDiffType &operator=(uint64_t diff) {
      jobDiffs_.insert(diff);
      currentJobDiff_ = diff;
      return *this;
    }
  };
};
#endif
