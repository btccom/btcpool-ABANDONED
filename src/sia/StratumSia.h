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
#ifndef STRATUM_SIA_H_
#define STRATUM_SIA_H_

#include "sia/CommonSia.h"
#include "Stratum.h"
#include <uint256.h>
#include "sia/sia.pb.h"

class ShareSiaBytesVersion {
public:
  uint32_t version_ = 0;
  uint32_t checkSum_ = 0;

  int64_t workerHashId_ = 0; // 8
  int32_t userId_ = 0; // 16
  int32_t status_ = 0; // 20
  int64_t timestamp_ = 0; // 24
  IpAddress ip_ = 0; // 32

  uint64_t jobId_ = 0; // 48
  uint64_t shareDiff_ = 0; // 56
  uint32_t blkBits_ = 0; // 64
  uint32_t height_ = 0; // 68
  uint32_t nonce_ = 0; // 72
  uint32_t sessionId_ = 0; // 76

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
    c += (uint64_t)nonce_;
    c += (uint64_t)sessionId_;

    return ((uint32_t)c) + ((uint32_t)(c >> 32));
  }
};

class ShareSia : public sharebase::Serializable<sharebase::SiaMsg> {
public:
  const static uint32_t BYTES_VERSION =
      0x00010003u; // first 0001: bitcoin, second 0003: version 3.
  const static uint32_t CURRENT_VERSION =
      0x00010004u; // first 0001: bitcoin, second 0003: version 4.

  // Please pay attention to memory alignment when adding / removing fields.
  // Please note that changing the Share structure will be incompatible with the
  // old deployment. Also, think carefully when removing fields. Some fields are
  // not used by BTCPool itself, but are important to external statistics
  // programs.

  // TODO: Change to a data structure that is easier to upgrade, such as
  // ProtoBuf.

  ShareSia() { set_version(CURRENT_VERSION); }
  ShareSia(const ShareSia &r) = default;
  ShareSia &operator=(const ShareSia &r) = default;

  double score() const {
    if (sharediff() == 0 || blkbits() == 0) {
      return 0.0;
    }

    double networkDifficulty = 0.0;
    SiaDifficulty::BitsToDifficulty(blkbits(), &networkDifficulty);

    // Network diff may less than share diff on testnet or regression test
    // network. On regression test network, the network diff may be zero. But no
    // matter how low the network diff is, you can only dig one block at a time.
    if (networkDifficulty < (double)sharediff()) {
      return 1.0;
    }

    return (double)sharediff() / networkDifficulty;
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
    double networkDifficulty = 0.0;
    SiaDifficulty::BitsToDifficulty(blkbits(), &networkDifficulty);

    return Strings::Format(
        "share(jobId: %u, ip: %s, userId: %d, "
        "workerId: %d, time: %u/%s, height: %u, "
        "blkBits: %08x/%f, shareDiff: %u, "
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
        (version == BYTES_VERSION) && size == sizeof(ShareSiaBytesVersion)) {

      ShareSiaBytesVersion *share = (ShareSiaBytesVersion *)payload;

      if (share->checkSum() != share->checkSum_) {
        DLOG(INFO) << "checkSum mismatched! checkSum_: " << share->checkSum_
                   << ", checkSum(): " << share->checkSum();
        return false;
      }

      set_version(CURRENT_VERSION);
      set_workerhashid(share->workerHashId_);
      set_userid(share->userId_);
      set_status(share->status_);
      set_timestamp(share->timestamp_);
      set_ip(share->ip_.toString());
      set_jobid(share->jobId_);
      set_sharediff(share->shareDiff_);
      set_blkbits(share->blkBits_);
      set_height(share->height_);
      set_nonce(share->nonce_);
      set_sessionid(share->sessionId_);

    } else {

      DLOG(INFO) << "unknow share received! data size: " << size;
      return false;
    }

    return true;
  }
};

// static_assert(sizeof(ShareSia) == 80, "ShareBitcoin should be 80 bytes");

class StratumJobSia : public StratumJob {
public:
  uint32_t nTime_;
  string blockHashForMergedMining_;
  uint256 networkTarget_;

public:
  StratumJobSia();
  ~StratumJobSia();
  string serializeToJson() const override;
  bool unserializeFromJson(const char *s, size_t len) override;
  uint64_t height() const override { return 0; }
  uint32_t jobTime() const override { return nTime_; }
};

// shares submitted by this session, for duplicate share check
struct LocalShareSia {
  uint64_t exNonce2_; // extra nonce2 fixed 8 bytes

  LocalShareSia(uint64_t exNonce2)
    : exNonce2_(exNonce2) {}

  LocalShareSia &operator=(const LocalShareSia &other) {
    exNonce2_ = other.exNonce2_;
    return *this;
  }

  bool operator<(const LocalShareSia &r) const {
    if (exNonce2_ < r.exNonce2_) {
      return true;
    }
    return false;
  }
};

class ServerSia;
class StratumSessionSia;

struct StratumTraitsSia {
  using ServerType = ServerSia;
  using SessionType = StratumSessionSia;
  using JobDiffType = uint64_t;
  using LocalShareType = LocalShareSia;
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
