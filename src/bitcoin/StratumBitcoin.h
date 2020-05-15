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
#ifndef STRATUM_BITCOIN_H_
#define STRATUM_BITCOIN_H_

#include "Stratum.h"
#include "CommonBitcoin.h"

#if defined(CHAIN_TYPE_ZEC) && defined(NDEBUG)
// fix "Zcash cannot be compiled without assertions."
#undef NDEBUG
#include <crypto/common.h>
#define NDEBUG
#endif

#include <uint256.h>
#include <pubkey.h>
#include <primitives/block.h>

#include "rsk/RskWork.h"
#include "vcash/VcashWork.h"
#include "script/standard.h"
#include "bitcoin/bitcoin.pb.h"

//
// max coinbase tx size, bytes
// Tips: currently there is only 1 input and 1, 2 or 3 output (reward, segwit
// and RSK outputs),
//       so 500 bytes may enough.
#define COINBASE_TX_MAX_SIZE 500

// ZCash's nonce is 256bits, others are 32bits.
#ifdef CHAIN_TYPE_ZEC
struct BitcoinNonceType {
  uint256 nonce;
  string solution;
};
// For mainnet & testnet:
// n=200, k=9, 2^9 = 512
// 21 bits * 512 / 8 = 1344
// 140 + 3 bytes(1344_vint) + 1344 = 1487 Bytes
// Set to 1488 bytes for memory align
const size_t BitcoinHeaderSize = 1488;
#else
using BitcoinNonceType = uint32_t;
const size_t BitcoinHeaderSize = 80;
#endif

/////////////////////////// BitcoinBlockHeaderData ///////////////////////////
class BitcoinHeaderData {
public:
  uint8_t headerData_[BitcoinHeaderSize];

  BitcoinHeaderData() { memset(headerData_, 0, sizeof(headerData_)); }
  void set(const CBlockHeader &header);
  bool get(CBlockHeader &header);
};
static_assert(
    sizeof(BitcoinHeaderData) == BitcoinHeaderSize,
    "sizeof(BitcoinHeaderData) should equal with BitcoinHeaderSize");

////////////////////////////////// FoundBlock //////////////////////////////////
class FoundBlock {
public:
  uint64_t jobId_;
  int64_t workerId_; // found by who
  int32_t userId_;
  int32_t height_;
  BitcoinHeaderData headerData_;
  char workerFullName_[40]; // <UserName>.<WorkerName>

  FoundBlock()
    : jobId_(0)
    , workerId_(0)
    , userId_(0)
    , height_(0) {
    memset(workerFullName_, 0, sizeof(workerFullName_));
  }
};

struct ShareBitcoinBytesV1 {
public:
  enum Result {
    // make default 0 as REJECT, so code bug is unlikely to make false ACCEPT
    // shares
    REJECT = 0,
    ACCEPT = 1
  };

  uint64_t jobId_ = 0;
  int64_t workerHashId_ = 0;
  uint32_t ip_ = 0;
  int32_t userId_ = 0;
  uint64_t shareDiff_ = 0;
  uint32_t timestamp_ = 0;
  uint32_t blkBits_ = 0;
  int32_t result_ = 0;
  // Even if the field does not exist,
  // gcc will add the field as a padding
  // under the default memory alignment parameter.
  int32_t padding_ = 0;
};

static_assert(
    sizeof(ShareBitcoinBytesV1) == 48,
    "ShareBitcoinBytesV1 should be 48 bytes");

struct ShareBitcoinBytesV2 {
  uint32_t version_ = 0;
  uint32_t checkSum_ = 0;

  int64_t workerHashId_ = 0;
  int32_t userId_ = 0;
  int32_t status_ = 0;
  int64_t timestamp_ = 0;
  IpAddress ip_ = 0;

  uint64_t jobId_ = 0;
  uint64_t shareDiff_ = 0;
  uint32_t blkBits_ = 0;
  uint32_t height_ = 0;
  uint32_t nonce_ = 0;
  uint32_t sessionId_ = 0;

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

class ShareBitcoin : public sharebase::Serializable<sharebase::BitcoinMsg> {
public:
  ShareBitcoin() {
    set_version(CURRENT_VERSION);
    set_workerhashid(0);
    set_userid(0);
    set_status(0);
    set_timestamp(0);
    set_ip("0.0.0.0");
    set_jobid(0);
    set_sharediff(0);
    set_blkbits(0);
    set_height(0);
    set_nonce(0);
    set_sessionid(0);
    set_versionmask(0);
  }

  ShareBitcoin(const ShareBitcoin &r) = default;
  ShareBitcoin &operator=(const ShareBitcoin &r) = default;

  double score() const {

    if (sharediff() == 0 || blkbits() == 0) {
      return 0.0;
    }

    double networkDifficulty = 1.0; // 0.0;
    BitcoinDifficulty::BitsToDifficulty(blkbits(), &networkDifficulty);

    if (networkDifficulty < (double)sharediff()) {
      return 1.0;
    }

    return (double)sharediff() / networkDifficulty;
  }

  bool isValid() const {

    if (version() != CURRENT_VERSION) {
      DLOG(INFO) << "share  version " << version();
      return false;
    }

    if (jobid() == 0 || userid() == 0 || workerhashid() == 0 || height() == 0 ||
        blkbits() == 0 || sharediff() == 0) {
      DLOG(INFO) << "share  jobid : " << jobid() << "\n"
                 << "share  userid : " << userid() << "\n"
                 << "share  workerhashid : " << workerhashid() << "\n"
                 << "share  height : " << height() << "\n"
                 << "share  blkbits : " << blkbits() << "\n"
                 << "share  sharediff : " << sharediff() << "\n";
      return false;
    }

    return true;
  }

  std::string toString() const {

    double networkDifficulty = 0.0;
    BitcoinDifficulty::BitsToDifficulty(blkbits(), &networkDifficulty);

    return Strings::Format(
        "share(jobId: %u, ip: %s, userId: %d, "
        "workerId: %d, time: %u/%s, height: %u, "
        "blkBits: %08x/%f, shareDiff: %u, "
        "nonce: %08x, sessionId: %08x, "
        "versionMask: %08x, "
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
        nonce(),
        sessionid(),
        versionmask(),
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
        version == BYTES_VERSION && size == sizeof(ShareBitcoinBytesV2)) {

      ShareBitcoinBytesV2 *share = (ShareBitcoinBytesV2 *)payload;

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

    } else if (size == sizeof(ShareBitcoinBytesV1)) {
      ShareBitcoinBytesV1 *share = (ShareBitcoinBytesV1 *)payload;

      char ipStr[INET_ADDRSTRLEN];
      inet_ntop(AF_INET, &(share->ip_), ipStr, INET_ADDRSTRLEN);

      set_version(CURRENT_VERSION);
      set_workerhashid(share->workerHashId_);
      set_userid(share->userId_);
      set_status(
          share->result_ == ShareBitcoinBytesV1::ACCEPT
              ? StratumStatus::ACCEPT
              : StratumStatus::REJECT_NO_REASON);
      set_timestamp(share->timestamp_);
      set_ip(ipStr);
      set_jobid(share->jobId_);
      set_sharediff(share->shareDiff_);
      set_blkbits(share->blkBits_);

      // There is no height in ShareBitcoinBytesV1, so it can only be assumed.

#ifdef CHAIN_TYPE_UBTC
      // UBTC's height and block rewards differ greatly from other SHA256
      // blockchains (like BTC, BCH, BSV, ...)
      set_height(758000);
#else
      // The block reward should be 12.5 on this height
      set_height(570000);
#endif

    } else {
      DLOG(INFO) << "unknow share received!";
      return false;
    }

    return true;
  }

public:
  const static uint32_t BYTES_VERSION = 0x00010003u;
  const static uint32_t CURRENT_VERSION = 0x00010004u;
};

class SubPoolInfo {
public:
  string name_;
  string zkUpdatePath_;
  string coinbaseInfo_;
  CTxDestination payoutAddr_;
};

class SubPoolJobBitcoin {
public:
  string name_;
  string coinbase1_;
  string coinbase2_;
  string grandCoinbase1_;
};

class StratumJobBitcoin : public StratumJob {
public:
  string gbtHash_; // gbt hash id
  uint256 prevHash_;
  string prevHashBeStr_; // little-endian hex, memory's order
  int32_t height_ = 0;
  string coinbase1_; // bitcoin: coinbase1, zcash: full coinbase tx
  string grandCoinbase1_;
  string coinbase2_; // bitcoin: coinbase2, zcash: empty
  vector<uint256> merkleBranch_;

  map<string, SubPoolJobBitcoin> subPool_;

  int32_t nVersion_ = 0;
  uint32_t nBits_ = 0;
  uint32_t nTime_ = 0;
  uint32_t minTime_ = 0;
  int64_t coinbaseValue_ = 0;
  // if segwit is not active, it will be empty
  string witnessCommitment_;
#ifdef CHAIN_TYPE_UBTC
  // if UB smart contract is not active, it will be empty
  string rootStateHash_;
#endif

#ifdef CHAIN_TYPE_ZEC
  uint256 merkleRoot_;
  uint256 finalSaplingRoot_;
#endif

  uint256 networkTarget_;

  // proxy stratum job
  uint32_t proxyExtraNonce2Size_ = 0;
  uint64_t proxyJobDifficulty_ = 0;

  // namecoin merged mining
  uint32_t nmcAuxBits_ = 0;
  uint256 nmcAuxBlockHash_;
  int32_t nmcAuxMerkleSize_ = 0;
  int32_t nmcAuxMerkleNonce_ = 0;
  uint256 nmcNetworkTarget_;
  int32_t nmcHeight_ = 0;
  string nmcRpcAddr_;
  string nmcRpcUserpass_;

  // rsk merged mining
  string blockHashForMergedMining_;
  uint256 rskNetworkTarget_;
  string rskdRpcAddress_;
  string rskdRpcUserPwd_;
  string feesForMiner_;
  bool isMergedMiningCleanJob_ = false;

  // vcash merged mining
  string vcashBlockHashForMergedMining_;
  uint256 vcashNetworkTarget_;
  uint64_t baserewards_;
  uint64_t transactionsfee_;
  uint64_t vcashHeight_;
  string vcashdRpcAddress_;
  string vcashdRpcUserPwd_;

public:
  StratumJobBitcoin();
  bool initFromGbt(
      const char *gbt,
      const string &poolCoinbaseInfo,
      const CTxDestination &poolPayoutAddr,
      const vector<SubPoolInfo> &subPool,
      const uint32_t blockVersion,
      const string &nmcAuxBlockJson,
      const RskWork &latestRskBlockJson,
      const VcashWork &latestVcashBlockJson,
      const bool isMergedMiningUpdate,
      const bool grandPoolEnabled);
  bool initFromStratumJob(
      vector<JsonNode> &jparamsArr,
      uint64_t currentDifficulty,
      const string &extraNonce1,
      uint32_t extraNonce2Size);
  string serializeToJson() const override;
  bool unserializeFromJson(const char *s, size_t len) override;
  bool isEmptyBlock();
  uint64_t height() const override { return height_; }
};

struct LocalShareBitcoin {
  uint64_t exNonce2_; // extra nonce2 fixed 8 bytes
  uint32_t nonce_; // nonce in block header
  uint32_t time_; // nTime in block header
  uint32_t versionMask_; // block version mask

  LocalShareBitcoin(
      uint64_t exNonce2, uint32_t nonce, uint32_t time, uint32_t versionMask)
    : exNonce2_(exNonce2)
    , nonce_(nonce)
    , time_(time)
    , versionMask_(versionMask) {}

  LocalShareBitcoin(uint64_t exNonce2, uint32_t nonce, uint32_t time)
    : exNonce2_(exNonce2)
    , nonce_(nonce)
    , time_(time)
    , versionMask_(0) {}

  LocalShareBitcoin &operator=(const LocalShareBitcoin &other) {
    exNonce2_ = other.exNonce2_;
    nonce_ = other.nonce_;
    time_ = other.time_;
    versionMask_ = other.versionMask_;
    return *this;
  }

  bool operator<(const LocalShareBitcoin &r) const {
    if (exNonce2_ < r.exNonce2_ ||
        (exNonce2_ == r.exNonce2_ && nonce_ < r.nonce_) ||
        (exNonce2_ == r.exNonce2_ && nonce_ == r.nonce_ && time_ < r.time_) ||
        (exNonce2_ == r.exNonce2_ && nonce_ == r.nonce_ && time_ == r.time_ &&
         versionMask_ < r.versionMask_)) {
      return true;
    }
    return false;
  }
};

struct LocalShareBitcoinGrand : public LocalShareBitcoin {
  uint32_t exGrandNonce1_; // extra grand nonce1 fixed 4bytes

  LocalShareBitcoinGrand(
      uint64_t exNonce2,
      uint32_t nonce,
      uint32_t time,
      uint32_t versionMask,
      uint32_t exGrandNonce1)
    : LocalShareBitcoin(exNonce2, nonce, time, versionMask)
    , exGrandNonce1_(exGrandNonce1) {}

  LocalShareBitcoinGrand(
      uint64_t exNonce2, uint32_t nonce, uint32_t time, uint32_t versionMask)
    : LocalShareBitcoin(exNonce2, nonce, time, versionMask)
    , exGrandNonce1_(0) {}

  LocalShareBitcoinGrand &operator=(const LocalShareBitcoinGrand &other) {
    exNonce2_ = other.exNonce2_;
    nonce_ = other.nonce_;
    time_ = other.time_;
    versionMask_ = other.versionMask_;
    exGrandNonce1_ = other.exGrandNonce1_;
    return *this;
  }

  bool operator<(const LocalShareBitcoinGrand &r) const {
    if (exNonce2_ < r.exNonce2_ ||
        (exNonce2_ == r.exNonce2_ && nonce_ < r.nonce_) ||
        (exNonce2_ == r.exNonce2_ && nonce_ == r.nonce_ && time_ < r.time_) ||
        (exNonce2_ == r.exNonce2_ && nonce_ == r.nonce_ && time_ == r.time_ &&
         versionMask_ < r.versionMask_) ||
        (exNonce2_ == r.exNonce2_ && nonce_ == r.nonce_ && time_ == r.time_ &&
         versionMask_ == r.versionMask_ && exGrandNonce1_ < r.exGrandNonce1_)) {
      return true;
    }
    return false;
  }
};

class ServerBitcoin;
class StratumSessionBitcoin;

struct StratumTraitsBitcoin {
  using ServerType = ServerBitcoin;
  using SessionType = StratumSessionBitcoin;
  using JobDiffType = uint64_t;

  using LocalShareType = LocalShareBitcoinGrand;

  struct LocalJobType : public LocalJobBase<LocalShareType> {
    LocalJobType(
        size_t chainId, uint64_t jobId, uint8_t shortJobId, uint32_t blkBits)
      : LocalJobBase<LocalShareType>(chainId, jobId)
      , shortJobId_(shortJobId)
      , blkBits_(blkBits) {}
    bool operator==(uint8_t shortJobId) const {
      return shortJobId_ == shortJobId;
    }
    uint8_t shortJobId_;
    uint32_t blkBits_;
  };
};

#endif
