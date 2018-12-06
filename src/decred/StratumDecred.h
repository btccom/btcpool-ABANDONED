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
#include "share.pro.pb.h"
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
class ShareDecred : public sharebase::DecredMsg 
{
public:

  const static uint32_t CURRENT_VERSION = 0x00200001u; // first 0020: DCR, second 0001: version 1



  ShareDecred() {
    set_version(ShareDecred::CURRENT_VERSION);
    set_workerhashid(0);
    set_userid(0);
    set_status(StratumStatus::REJECT_NO_REASON);
    set_timestamp(0);
    set_jobid(0);
    set_sharediff(0);
    set_blkbits(0);
    set_height(0);
    set_nonce(0);
    set_sessionid(0);
    set_network((uint32_t)NetworkDecred::MainNet);
    set_voters(0);
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
      uint32_t extraNonce1) {
    set_version(ShareDecred::CURRENT_VERSION);
    set_workerhashid(workerHashId);
    set_userid(userId);
    set_status(StratumStatus::REJECT_NO_REASON);
    set_timestamp(time(nullptr));
    set_jobid(jobId);
    set_sharediff(jobDifficulty);
    set_blkbits(blkBits);
    set_height(height);
    set_nonce(nonce);
    set_sessionid(extraNonce1);
    set_network((uint32_t)NetworkDecred::MainNet);
    set_voters(0);
    IpAddress ip;
    ip.fromIpv4Int(clientIpInt);
    set_ip(ip.toString());
  }

  double score() const
  {
    if (sharediff() == 0 || blkbits() == 0)
    {
      return 0.0;
    }

    double networkDifficulty = NetworkParamsDecred::get((NetworkDecred)network()).powLimit.getdouble() / arith_uint256().SetCompact(blkbits()).getdouble();

    // Network diff may less than share diff on testnet or regression test network.
    // On regression test network, the network diff may be zero.
    // But no matter how low the network diff is, you can only dig one block at a time.
    if (networkDifficulty < sharediff())
    {
      return 1.0;
    }

    return sharediff() / networkDifficulty;
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
    double networkDifficulty = NetworkParamsDecred::get((NetworkDecred)network()).powLimit.getdouble() / arith_uint256().SetCompact(blkbits()).getdouble();
    return Strings::Format("share(jobId: %" PRIu64 ", ip: %s, userId: %d, "
                           "workerId: %" PRId64 ", time: %u/%s, height: %u, "
                           "blkBits: %08x/%lf, shareDiff: %" PRIu64 ", status: %d/%s)",
                           jobid(), ip().c_str(), userid(),
                           workerhashid(), timestamp(), date("%F %T", timestamp()).c_str(), height(),
                           blkbits(), networkDifficulty, sharediff(), status(), StratumStatus::toString(status()));
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
