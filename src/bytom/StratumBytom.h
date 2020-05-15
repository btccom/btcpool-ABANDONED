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
#ifndef STRATUM_BYTOM_H_
#define STRATUM_BYTOM_H_

#include "Stratum.h"
#include "CommonBytom.h"
#include "bytom/bytom.pb.h"
union BytomCombinedHeader {
  struct {
    uint64_t blockCommitmentMerkleRootCheapHash_;
    uint64_t blockCommitmentStatusHashCheapHash_;
    uint64_t timestamp_;
    uint64_t nonce_;
  };
  uint8_t bytes[32];
};

class ShareBytomBytesVersion {
public:
  uint32_t version_ = 0; // 0
  uint32_t checkSum_ = 0; // 4

  uint64_t jobId_ = 0; // 8
  int64_t workerHashId_ = 0; // 16
  int64_t timestamp_ = 0; // 24
  uint64_t shareDiff_ = 0; // 32
  uint64_t blkBits_ = 0; // 40
  uint64_t height_ = 0; // 48
  IpAddress ip_; // 56
  BytomCombinedHeader combinedHeader_; // 72

  int32_t userId_ = 0; // 104
  int32_t status_ = 0; // 108

  uint32_t checkSum() const {
    uint64_t c = 0;

    c += (uint64_t)version_;
    c += (uint64_t)workerHashId_;
    c += (uint64_t)userId_;
    c += (uint64_t)status_;
    c += (uint64_t)timestamp_;
    c += (uint64_t)ip_.addrUint64[0];
    c += (uint64_t)ip_.addrUint64[1];
    c += (uint64_t)jobId_;
    c += (uint64_t)shareDiff_;
    c += (uint64_t)blkBits_;
    c += (uint64_t)height_;
    c += (uint64_t)combinedHeader_.blockCommitmentMerkleRootCheapHash_;
    c += (uint64_t)combinedHeader_.blockCommitmentStatusHashCheapHash_;
    c += (uint64_t)combinedHeader_.timestamp_;
    c += (uint64_t)combinedHeader_.nonce_;

    return ((uint32_t)c) + ((uint32_t)(c >> 32));
  }
};

class ShareBytom : public sharebase::Serializable<sharebase::BytomMsg> {
public:
  const static uint32_t BYTES_VERSION =
      0x00030001u; // first 0003: bytom, second 0001: version 1.
  const static uint32_t CURRENT_VERSION =
      0x00030002u; // first 0003: bytom, second 0002: version 2.

  ShareBytom() { set_version(CURRENT_VERSION); }
  ShareBytom(const ShareBytom &r) = default;
  ShareBytom &operator=(const ShareBytom &r) = default;

  double score() const {
    if (sharediff() == 0 || blkbits() == 0) {
      return 0.0;
    }

    uint64_t difficulty = Bytom_TargetCompactToDifficulty(blkbits());

    // Network diff may less than share diff on testnet or regression test
    // network. On regression test network, the network diff may be zero. But no
    // matter how low the network diff is, you can only dig one block at a time.
    if (difficulty < sharediff()) {
      return 1.0;
    }

    return (double)sharediff() / (double)difficulty;
  }

  bool isValid() const {
    if (version() != CURRENT_VERSION) {
      return false;
    }

    if (jobid() == 0 || userid() == 0 || workerhashid() == 0 || height() == 0 ||
        blkbits() == 0 || sharediff() == 0) {
      return false;
    }

    return true;
  }

  string toString() const {
    uint64_t networkDifficulty = Bytom_TargetCompactToDifficulty(blkbits());

    BytomCombinedHeader combinedHeader;
    memcpy(
        &combinedHeader, combinedheader().data(), sizeof(BytomCombinedHeader));

    return Strings::Format(
        "share(jobId: %u, ip: %s, userId: %d, "
        "workerId: %d, time: %u/%s, height: %u, "
        "blkBits: %08x/%d, nonce: %08x, shareDiff: %u, "
        "status: %d/%s)",
        jobid(),
        ip(),
        userid(),
        workerhashid(),
        timestamp(),
        date("%F %T", timestamp()),
        height(),
        blkbits(),
        networkDifficulty,
        combinedHeader.nonce_,
        sharediff(),
        status(),
        StratumStatus::toString(status()));
  }

  bool UnserializeWithVersion(const uint8_t *data, uint32_t size) {

    if (nullptr == data || size <= 0) {
      return false;
    }

    const uint8_t *payload = data;
    uint32_t version = *((uint32_t *)payload);

    if (version == CURRENT_VERSION) {

      if (!ParseFromArray(
              (const uint8_t *)(payload + sizeof(uint32_t)),
              size - sizeof(uint32_t))) {
        DLOG(INFO) << "share ParseFromArray failed!";
        return false;
      }
    } else if (
        version == BYTES_VERSION && size == sizeof(ShareBytomBytesVersion)) {

      ShareBytomBytesVersion *share = (ShareBytomBytesVersion *)payload;

      if (share->checkSum() != share->checkSum_) {
        DLOG(INFO) << "checkSum mismatched! checkSum_: " << share->checkSum_
                   << ", checkSum(): " << share->checkSum();
        return false;
      }

      set_version(CURRENT_VERSION);
      set_jobid(share->jobId_);
      set_workerhashid(share->workerHashId_);
      set_timestamp(share->timestamp_);
      set_sharediff(share->shareDiff_);
      set_blkbits(share->blkBits_);
      set_ip(share->ip_.toString());
      set_combinedheader(
          &(share->combinedHeader_), sizeof(BytomCombinedHeader));
      set_userid(share->userId_);
      set_status(share->status_);

    } else {

      DLOG(INFO) << "unknow share received!";
      return false;
    }

    return true;
  }
};

struct BlockHeaderBytom {
  uint64_t version; // The version of the block.
  uint64_t height; // The height of the block.
  string previousBlockHash; // The hash of the previous block.
  uint64_t timestamp; // The time of the block in seconds.
  uint64_t bits; // Difficulty target for the block.
  string transactionsMerkleRoot;
  string transactionStatusHash;
  string serializeToJson() const;
};

class StratumJobBytom : public StratumJob {
public:
  StratumJobBytom();
  string serializeToJson() const override;
  bool unserializeFromJson(const char *s, size_t len) override;
  uint64_t height() const override { return blockHeader_.height; }

  BlockHeaderBytom blockHeader_;
  string seed_;
  string hHash_;

  void updateBlockHeaderFromHash();

  uint32_t nTime_;
};

// shares submitted by this session, for duplicate share check
struct LocalShareBytom {
  uint64_t exNonce2_; // extra nonce2 fixed 8 bytes

  LocalShareBytom(uint64_t exNonce2)
    : exNonce2_(exNonce2) {}

  LocalShareBytom &operator=(const LocalShareBytom &other) {
    exNonce2_ = other.exNonce2_;
    return *this;
  }

  bool operator<(const LocalShareBytom &r) const {
    if (exNonce2_ < r.exNonce2_) {
      return true;
    }
    return false;
  }
};

class ServerBytom;
class StratumSessionBytom;

struct StratumTraitsBytom {
  using ServerType = ServerBytom;
  using SessionType = StratumSessionBytom;
  using LocalShareType = LocalShareBytom;
  using JobDiffType = uint64_t;
  struct LocalJobType : public LocalJobBase<LocalShareType> {
    LocalJobType(size_t chainId, uint64_t jobId, uint8_t shortJobId)
      : LocalJobBase<LocalShareType>(chainId, jobId)
      , shortJobId_(shortJobId)
      , jobDifficulty_(0) {}
    bool operator==(uint8_t shortJobId) const {
      return shortJobId_ == shortJobId;
    }
    uint8_t shortJobId_;
    uint64_t jobDifficulty_;
  };
};

#endif
