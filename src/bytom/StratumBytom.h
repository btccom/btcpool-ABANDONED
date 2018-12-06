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
#include "share.pro.pb.h"
union BytomCombinedHeader
{
  struct
  {
    uint64_t blockCommitmentMerkleRootCheapHash_;
    uint64_t blockCommitmentStatusHashCheapHash_;
    uint64_t timestamp_;
    uint64_t nonce_;      
  };
  uint8_t bytes[32];
};

class ShareBytom  :public sharebase::BytomMsg
{
public:

  const static uint32_t CURRENT_VERSION = 0x00030001u; // first 0003: bytom, second 0002: version 1.


  ShareBytom() {
    set_version(CURRENT_VERSION);
  }
  ShareBytom(const ShareBytom &r) = default;
  ShareBytom &operator=(const ShareBytom &r) = default;

  double score() const
  {
    if (sharediff() == 0 || blkbits() == 0)
    {
      return 0.0;
    }

    uint64_t difficulty = Bytom_TargetCompactToDifficulty(blkbits());

    // Network diff may less than share diff on testnet or regression test network.
    // On regression test network, the network diff may be zero.
    // But no matter how low the network diff is, you can only dig one block at a time.
    if (difficulty < sharediff())
    {
      return 1.0;
    }

    return (double)sharediff() / (double)difficulty;
  }

  bool isValid() const
  {
    if (version() != CURRENT_VERSION) {
      return false;
    }

    if (jobid() == 0 || userid() == 0 || workerhashid() == 0 ||
        height() == 0 || blkbits() == 0 || sharediff() == 0)
    {
      return false;
    }
    
    return true;
  }

  string toString() const
  {
    uint64_t networkDifficulty = Bytom_TargetCompactToDifficulty(blkbits());
    
    BytomCombinedHeader combinedHeader;
    memcpy(&combinedHeader, combinedheader().data(), sizeof(BytomCombinedHeader));

    return Strings::Format("share(jobId: %" PRIu64 ", ip: %s, userId: %d, "
                           "workerId: %" PRId64 ", time: %u/%s, height: %u, "
                           "blkBits: %08x/%" PRId64 ", nonce: %08x, shareDiff: %" PRIu64 ", "
                           "status: %d/%s)",
                           jobid(), ip().c_str(), userid(),
                           workerhashid(), timestamp(), date("%F %T", timestamp()).c_str(), height(),
                           blkbits(), networkDifficulty, combinedHeader.nonce_, sharediff(),
                           status(), StratumStatus::toString(status()));
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

  uint32_t getsharelength() {
      return IsInitialized() ? ByteSize() : 0;
  }
};

struct BlockHeaderBytom
{
  uint64 version;           // The version of the block.
  uint64 height;            // The height of the block.
  string previousBlockHash; // The hash of the previous block.
  uint64 timestamp;         // The time of the block in seconds.
  uint64 bits;              // Difficulty target for the block.
  string transactionsMerkleRoot;
  string transactionStatusHash;
  string serializeToJson() const;
};

class StratumJobBytom : public StratumJob
{
public:
  StratumJobBytom();
  ~StratumJobBytom();
  string serializeToJson() const override;
  bool unserializeFromJson(const char *s, size_t len) override;
  BlockHeaderBytom blockHeader_;
  string seed_;
  string hHash_;

  void updateBlockHeaderFromHash();

  uint32_t nTime_;

};

class ServerBytom;
class StratumSessionBytom;

struct StratumTraitsBytom {
  using ServerType = ServerBytom;
  using SessionType = StratumSessionBytom;
  using JobDiffType = uint64_t;
  struct LocalJobType : public LocalJob {
    LocalJobType(uint64_t jobId, uint8_t shortJobId)
        : LocalJob(jobId), shortJobId_(shortJobId), jobDifficulty_(0) {}
    bool operator==(uint8_t shortJobId) const { return shortJobId_ == shortJobId; }
    uint8_t shortJobId_;
    uint64_t jobDifficulty_;
  };
};

#endif
