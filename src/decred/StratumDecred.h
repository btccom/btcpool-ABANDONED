/*
 The MIT License (MIT)

 Copyright (c) [2018] [BTC.COM]

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

#ifndef STRATUM_DECRED_H_
#define STRATUM_DECRED_H_

#include "Stratum.h"
#include "CommonDecred.h"

class FoundBlockDecred
{
public:
  uint64_t jobId_;
  int64_t workerId_; // found by who
  int32_t userId_;
  char workerFullName_[40]; // <UserName>.<WorkerName>
  BlockHeaderDecred header_;
  NetworkDecred network_;

  FoundBlockDecred(uint64_t jobId, int64_t workerId, int32_t userId, const string &workerFullName, const BlockHeaderDecred& header, NetworkDecred network)
    : jobId_(jobId), workerId_(workerId), userId_(userId), header_(header), network_(network)
  {
    snprintf(workerFullName_, sizeof(workerFullName_), "%s", workerFullName.c_str());
  }
};

// [[[[ IMPORTANT REMINDER! ]]]]
// Please keep the Share structure forward compatible.
// That is: don't change it unless you add code so that
// both the modified and non-modified Shares can be processed.
// Please note that in the usual upgrade, the old version of Share
// and the new version will coexist for a while.
// If there is no forward compatibility, one of the versions of Share
// will be considered invalid, resulting in loss of users' hashrate.
class ShareDecred
{
public:

  const static uint32_t CURRENT_VERSION = 0x00200001u; // first 0020: DCR, second 0001: version 1

  uint32_t  version_;
  uint32_t  checkSum_;

  int64_t   workerHashId_;
  int32_t   userId_;
  int32_t   status_;
  int64_t   timestamp_;
  IpAddress ip_;

  uint64_t jobId_;
  uint64_t shareDiff_;
  uint32_t blkBits_;
  uint32_t height_;
  uint32_t nonce_;
  uint32_t sessionId_;
  NetworkDecred network_;
  uint16_t voters_;

  ShareDecred()
    : version_(ShareDecred::CURRENT_VERSION)
    , checkSum_(0)
    , workerHashId_(0)
    , userId_(0)
    , status_(StratumStatus::REJECT_NO_REASON)
    , timestamp_(0)
    , jobId_(0)
    , shareDiff_(0)
    , blkBits_(0)
    , height_(0)
    , nonce_(0)
    , sessionId_(0)
    , network_(NetworkDecred::MainNet)
    , voters_(0)
  {
  }

  ShareDecred(
      int64_t workerHashId,
      int32_t userId,
      uint32_t clientIpInt,
      uint64_t jobId,
      uint64_t jobDifficulty,
      uint32_t blkBits,
      uint32_t height,
      uint32_t nonce,
      uint32_t extraNonce1)
    : version_(ShareDecred::CURRENT_VERSION)
    , checkSum_(0)
    , workerHashId_(workerHashId)
    , userId_(userId)
    , status_(StratumStatus::REJECT_NO_REASON)
    , timestamp_(time(nullptr))
    , jobId_(jobId)
    , shareDiff_(jobDifficulty)
    , blkBits_(blkBits)
    , height_(height)
    , nonce_(nonce)
    , sessionId_(extraNonce1)
    , network_(NetworkDecred::MainNet)
    , voters_(0)
  {
    ip_.fromIpv4Int(clientIpInt);
  }

  double score() const
  {
    if (shareDiff_ == 0 || blkBits_ == 0)
    {
      return 0.0;
    }

    double networkDifficulty = NetworkParamsDecred::get(network_).powLimit.getdouble() / arith_uint256().SetCompact(blkBits_).getdouble();

    // Network diff may less than share diff on testnet or regression test network.
    // On regression test network, the network diff may be zero.
    // But no matter how low the network diff is, you can only dig one block at a time.
    if (networkDifficulty < shareDiff_)
    {
      return 1.0;
    }

    return shareDiff_ / networkDifficulty;
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
    c += (uint64_t) jobId_;
    c += (uint64_t) shareDiff_;
    c += (uint64_t) blkBits_;
    c += (uint64_t) height_;
    c += (uint64_t) nonce_;
    c += (uint64_t) sessionId_;
    c += (uint64_t) network_;
    c += (uint64_t) voters_;

    return ((uint32_t) c) + ((uint32_t) (c >> 32));
  }

  bool isValid() const
  {
    if (version_ != CURRENT_VERSION) {
      return false;
    }

    if (checkSum_ != checkSum()) {
      DLOG(INFO) << "checkSum mismatched! checkSum_: " << checkSum_ << ", checkSum(): " << checkSum();
      return false;
    }

    if (jobId_ == 0 || userId_ == 0 || workerHashId_ == 0 ||
        height_ == 0 || blkBits_ == 0 || shareDiff_ == 0)
    {
      return false;
    }

    return true;
  }

  string toString() const
  {
    double networkDifficulty = NetworkParamsDecred::get(network_).powLimit.getdouble() / arith_uint256().SetCompact(blkBits_).getdouble();
    return Strings::Format("share(jobId: %" PRIu64 ", ip: %s, userId: %d, "
                           "workerId: %" PRId64 ", time: %u/%s, height: %u, "
                           "blkBits: %08x/%lf, shareDiff: %" PRIu64 ", status: %d/%s)",
                           jobId_, ip_.toString().c_str(), userId_,
                           workerHashId_, timestamp_, date("%F %T", timestamp_).c_str(), height_,
                           blkBits_, networkDifficulty, shareDiff_, status_, StratumStatus::toString(status_));
  }
};

class StratumJobDecred : public StratumJob {
public:
  static const size_t CoinBase1Size = offsetof(BlockHeaderDecred, extraData) - offsetof(BlockHeaderDecred, merkelRoot);

  BlockHeaderDecred header_;
  uint256 target_;
  NetworkDecred network_;

  StratumJobDecred();
  string serializeToJson() const override;
  bool unserializeFromJson(const char *s, size_t len) override;
  string getPrevHash() const;
  string getCoinBase1() const;
};

class StratumProtocolDecred {
public:
  virtual ~StratumProtocolDecred() = default;
  virtual string getExtraNonce1String(uint32_t extraNonce1) const = 0;
  virtual void setExtraNonces(BlockHeaderDecred &header, uint32_t extraNonce1, const vector<uint8_t> &extraNonce2) = 0;
};

class ServerDecred;
class StratumSessionDecred;

struct StratumTraitsDecred {
  using ServerType = ServerDecred;
  using SessionType = StratumSessionDecred;
  using JobDiffType = uint64_t;
  struct LocalJobType : public LocalJob {
    LocalJobType(uint64_t jobId, uint8_t shortJobId, uint32_t blkBits)
        : LocalJob(jobId), shortJobId_(shortJobId), blkBits_(blkBits) {}
    bool operator==(uint8_t shortJobId) const { return shortJobId_ == shortJobId; }
    uint8_t shortJobId_;
    uint32_t blkBits_;
  };
};

#endif
