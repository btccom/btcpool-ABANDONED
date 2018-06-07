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
#ifndef STRATUM_H_
#define STRATUM_H_

#include "Common.h"
#include "utilities_js.hpp"
#include "Utils.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <queue>

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>

#include <glog/logging.h>

#include <uint256.h>
#include <base58.h>
#include "rsk/RskWork.h"
#include "script/standard.h"


//
// max coinbase tx size, bytes
// Tips: currently there is only 1 input and 1, 2 or 3 output (reward, segwit and RSK outputs),
//       so 500 bytes may enough.
#define COINBASE_TX_MAX_SIZE   500

// default worker name
#define DEFAULT_WORKER_NAME "__default__"

inline uint32_t jobId2Time(uint64_t jobId)
{
  return (uint32_t)((jobId >> 32) & 0x00000000FFFFFFFFULL);
}

string filterWorkerName(const string &workerName);

inline string filterWorkerName(const char *workerName)
{
  return filterWorkerName(std::string(workerName));
}

////////////////////////////////// FoundBlock //////////////////////////////////
class FoundBlock
{
public:
  uint64_t jobId_;
  int64_t workerId_; // found by who
  int32_t userId_;
  int32_t height_;
  uint8_t header80_[80];
  char workerFullName_[40]; // <UserName>.<WorkerName>

  FoundBlock() : jobId_(0), workerId_(0), userId_(0), height_(0)
  {
    memset(header80_, 0, sizeof(header80_));
    memset(workerFullName_, 0, sizeof(workerFullName_));
  }
};

///////////////////////////////////// IPv4/IPv6 compatible address structure ////////////////////////////////////
union IpAddress {
  // all datas are big endian
  uint8_t  addrUint8[16];
  uint16_t addrUint16[8];
  uint32_t addrUint32[4];
  uint64_t addrUint64[2];
  // use addrIpv4[3] to store the IPv4 addr
  struct in_addr addrIpv4[4];
  struct in6_addr addrIpv6;

  // memory mapping:
  // addrUint8  | 0| 1| 2| 3| 4| 5| 6| 7| 8| 9|10|11|12|13|14|15|
  // addrUint16 |  0  |  1  |  2  |  3  |  4  |  5  |  6  |  7  |
  // addrUint32 |     0     |     1     |     2     |     3     |
  // addrUint64 |           0           |           1           |
  // addrIpv4   | don't use | don't use | don't use |     3     |
  // addrIpv6   |                      all                      |

  IpAddress() {
    addrUint64[0] = 0;
    addrUint64[1] = 0;
  }

  IpAddress(uint64_t initNum) {
    addrUint64[0] = initNum;
    addrUint64[1] = initNum;
  }

  bool fromString(const string &ipStr) {
    if (isIpv4(ipStr)) {
      addrUint32[0] = 0;
      addrUint32[1] = 0;
      addrUint32[2] = 0;
      return inet_pton(AF_INET, ipStr.c_str(), (void *)&addrIpv4[3]);
    }
    else {
      return inet_pton(AF_INET, ipStr.c_str(), (void *)&addrIpv6);
    }
  }

  string toString() const {
    const char *pStr;

    if (isIpv4()) {
      char str[INET_ADDRSTRLEN];  
      pStr = inet_ntop(AF_INET, (void *)&(addrIpv4[3]), str, sizeof(str));
    }
    else {
      char str[INET6_ADDRSTRLEN];  
      pStr = inet_ntop(AF_INET6, &addrIpv6, str, sizeof(str));
    }

    return string(pStr);
  }

  void fromInAddr(const struct in_addr &inAddr) {
    addrUint32[0] = 0;
    addrUint32[1] = 0;
    addrUint32[2] = 0;
    addrIpv4[3] = inAddr;
  }

  void fromInAddr(const struct in6_addr &inAddr) {
    addrIpv6 = inAddr;
  }

  void fromIpv4Int(const uint32_t ipv4Int) {
    addrUint32[0] = 0;
    addrUint32[1] = 0;
    addrUint32[2] = 0;
    addrUint32[3] = ipv4Int;
  }

  bool isIpv4() const {
    if (addrUint32[0] == 0 && addrUint32[1] == 0) {
      // IPv4 compatible address
      // ::w.x.y.z
      if (addrUint32[2] == 0) {
        return true;
      }
      // IPv4 mapping address
      // ::ffff:w.x.y.z
      if (addrUint16[4] == 0 && addrUint16[5] == 0xffff) {
        return true;
      }
    }
    return false;
  }

  static bool isIpv4(const string &ipStr) {
    if (ipStr.find(':') == ipStr.npos) {
      return true;
    }
    return false;
  }
};

// IpAddress should be 16 bytes
static_assert(sizeof(IpAddress) == 16, "union IpAddress should not large than 16 bytes");

//////////////////////////////// StratumError ////////////////////////////////
class StratumStatus
{
public:
  enum
  {
    // make ACCEPT and SOLVED be two singular value,
    // so code bug is unlikely to make false ACCEPT shares

    // share reached the job target (but may not reached the network target)
    ACCEPT = 1798084231, // bin(01101011 00101100 10010110 10000111)

    // share reached the network target (only ServerEth::checkShare use it at current)
    SOLVED = 1422486894, // bin(‭01010100 11001001 01101101 01101110‬)

    REJECT_NO_REASON = 0,

    JOB_NOT_FOUND = 21,
    DUPLICATE_SHARE = 22,
    LOW_DIFFICULTY = 23,
    UNAUTHORIZED = 24,
    NOT_SUBSCRIBED = 25,

    ILLEGAL_METHOD = 26,
    ILLEGAL_PARARMS = 27,
    IP_BANNED = 28,
    INVALID_USERNAME = 29,
    INTERNAL_ERROR = 30,
    TIME_TOO_OLD = 31,
    TIME_TOO_NEW = 32,

    UNKNOWN = 2147483647 // bin(01111111 11111111 11111111 11111111)
  };
  static const char *toString(int err);
};

///////////////////////////////////// Share ////////////////////////////////////
// Class Share should not be used directly.
// Use a derived class of class Share (example: ShareBitcoin, ShareEth).
// Also, keep class Share plain, don't add any virtual functions.
// Otherwise, derived classes will not be able to use byte-based
// object serialization (because of the addition of a virtual function table).
class Share
{
public:
  
  uint32_t  version_      = 0;
  uint32_t  checkSum_     = 0;
  int64_t   workerHashId_ = 0;
  int32_t   userId_       = 0;
  int32_t   status_       = 0;
  int64_t   timestamp_    = 0;
  IpAddress ip_           = 0;

  Share() = default;
  Share(const Share &r) = default;
  Share &operator=(const Share &r) = default;
};

class ShareBitcoin : public Share
{
public:

  const uint32_t CURRENT_VERSION = 0x00010002u; // first 0001: bitcoin, second 0002: version 2.

  uint64_t jobId_     = 0;
  uint64_t shareDiff_ = 0;
  uint32_t blkBits_   = 0;
  uint32_t height_    = 0;
  uint32_t nonce_     = 0;
  uint32_t sessionId_ = 0;

  ShareBitcoin() = default;
  ShareBitcoin(const ShareBitcoin &r) = default;
  ShareBitcoin &operator=(const ShareBitcoin &r) = default;

  double score() const
  {
    if (shareDiff_ == 0 || blkBits_ == 0)
    {
      return 0.0;
    }

    double networkDifficulty = 0.0;
    BitsToDifficulty(blkBits_, &networkDifficulty);

    // Network diff may less than share diff on testnet or regression test network.
    // On regression test network, the network diff may be zero.
    // But no matter how low the network diff is, you can only dig one block at a time.
    if (networkDifficulty < (double)shareDiff_)
    {
      return 1.0;
    }

    return (double)shareDiff_ / networkDifficulty;
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

    return ((uint32_t) c) + ((uint32_t) (c >> 32));
  }

  bool isValid() const
  {
    if (version_ != CURRENT_VERSION) {
      return false;
    }

    if (checkSum_ != checkSum()) {
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
    double networkDifficulty = 0.0;
    BitsToDifficulty(blkBits_, &networkDifficulty);

    return Strings::Format("share(jobId: %" PRIu64 ", ip: %s, userId: %d, "
                           "workerId: %" PRId64 ", time: %u/%s, height: %u, "
                           "blkBits: %08x/%lf, nonce: %08x, sessionId: %08x, shareDiff: %" PRIu64 ", "
                           "status: %d/%s)",
                           jobId_, ip_.toString().c_str(), userId_,
                           workerHashId_, timestamp_, date("%F %T", timestamp_).c_str(), height_,
                           blkBits_, networkDifficulty, nonce_, sessionId_, shareDiff_,
                           status_, StratumStatus::toString(status_));
  }
};

class ShareEth : public Share
{
public:

  const uint32_t CURRENT_VERSION = 0x00110002u; // first 0011: ETH, second 0002: version 2

  uint64_t jobId_       = 0;
  uint64_t shareDiff_   = 0;
  uint64_t networkDiff_ = 0;
  uint64_t nonce_       = 0;
  uint32_t sessionId_   = 0;
  uint32_t height_      = 0;

  ShareEth() = default;
  ShareEth(const ShareEth &r) = default;
  ShareEth &operator=(const ShareEth &r) = default;

  double score() const
  {
    if (shareDiff_ == 0 || networkDiff_ == 0)
    {
      return 0.0;
    }

    // Network diff may less than share diff on testnet or regression test network.
    // On regression test network, the network diff may be zero.
    // But no matter how low the network diff is, you can only dig one block at a time.
    if (networkDiff_ < shareDiff_)
    {
      return 1.0;
    }

    return (double)shareDiff_ / (double)networkDiff_;
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
    c += (uint64_t) networkDiff_;
    c += (uint64_t) nonce_;
    c += (uint64_t) sessionId_;
    c += (uint64_t) height_;

    return ((uint32_t) c) + ((uint32_t) (c >> 32));
  }

  bool isValid() const
  {
    if (version_ != CURRENT_VERSION) {
      return false;
    }

    if (checkSum_ != checkSum()) {
      return false;
    }

    if (jobId_ == 0 || userId_ == 0 || workerHashId_ == 0 ||
        height_ == 0 || networkDiff_ == 0 || shareDiff_ == 0)
    {
      return false;
    }
    
    return true;
  }

  string toString() const
  {
    return Strings::Format("share(jobId: %" PRIu64 ", ip: %s, userId: %d, "
                           "workerId: %" PRId64 ", time: %u/%s, height: %u, "
                           "shareDiff: %" PRIu64 ", networkDiff: %" PRIu64 ", nonce: %016" PRIx64 ", sessionId: %08x, "
                           "status: %d/%s)",
                           jobId_, ip_.toString().c_str(), userId_,
                           workerHashId_, timestamp_, date("%F %T", timestamp_).c_str(), height_,
                           shareDiff_, networkDiff_, nonce_, sessionId_,
                           status_, StratumStatus::toString(status_));
  }
};

//////////////////////////////// StratumWorker ////////////////////////////////
class StratumWorker
{
public:
  int32_t userId_;
  int64_t workerHashId_; // substr(0, 8, HASH(wokerName))

  string fullName_; // fullName = username.workername
  string userName_;
  string workerName_; // workername, max is: 20

  void reset();

public:
  StratumWorker();
  void setUserIDAndNames(const int32_t userId, const string &fullName);
  string getUserName(const string &fullName) const;

  static int64_t calcWorkerId(const string &workerName);
};

////////////////////////////////// StratumJob //////////////////////////////////
//
// Stratum Job
//
// https://slushpool.com/help/#!/manual/stratum-protocol
//
// "mining.notify"
//
// job_id   - ID of the job. Use this ID while submitting share generated
//             from this job.
// prevhash - Hash of previous block.
// coinb1   - Initial part of coinbase transaction.
// coinb2   - Final part of coinbase transaction.
// merkle_branch - List of hashes, will be used for calculation of merkle root.
//                 This is not a list of all transactions, it only contains
//                 prepared hashes of steps of merkle tree algorithm.
// version    - Bitcoin block version.
// nbits      - Encoded current network difficulty
// ntime      - Current ntime
// clean_jobs - When true, server indicates that submitting shares from previous
//              jobs don't have a sense and such shares will be rejected. When
//              this flag is set, miner should also drop all previous jobs,
//              so job_ids can be eventually rotated.
//
//
class StratumJob
{
public:
  // jobId: timestamp + gbtHash, hex string, we need to make sure jobId is
  // unique in a some time, jobId can convert to uint64_t
  uint64_t jobId_;
  string gbtHash_; // gbt hash id
  uint256 prevHash_;
  string prevHashBeStr_; // little-endian hex, memory's order
  int32_t height_;
  string coinbase1_;
  string coinbase2_;
  vector<uint256> merkleBranch_;

  int32_t nVersion_;
  uint32_t nBits_;
  uint32_t nTime_;
  uint32_t minTime_;
  int64_t coinbaseValue_;
  // if segwit is not active, it will be empty
  string witnessCommitment_;

  uint256 networkTarget_;

  // namecoin merged mining
  uint32_t nmcAuxBits_;
  uint256 nmcAuxBlockHash_;
  int32_t nmcAuxMerkleSize_;
  int32_t nmcAuxMerkleNonce_;
  uint256 nmcNetworkTarget_;
  int32_t nmcHeight_;
  string nmcRpcAddr_;
  string nmcRpcUserpass_;

  // rsk merged mining
  string blockHashForMergedMining_;
  string rskdRpcAddress_;
  string rskdRpcUserPwd_;
  string feesForMiner_;
  uint256 rskNetworkTarget_;
  bool isRskCleanJob_;

public:
  StratumJob();
  virtual ~StratumJob(){};

  virtual string serializeToJson() const;
  virtual bool unserializeFromJson(const char *s, size_t len);
  virtual uint32 jobTime() const { return jobId2Time(jobId_); }

  bool initFromGbt(const char *gbt, const string &poolCoinbaseInfo,
                   const CTxDestination &poolPayoutAddr,
                   const uint32_t blockVersion,
                   const string &nmcAuxBlockJson,
                   const RskWork &latestRskBlockJson);
  bool isEmptyBlock();
};

class StratumJobEth : public StratumJob
{
public:
  StratumJobEth();
  string serializeToJson() const override;
  bool unserializeFromJson(const char *s, size_t len) override;
  bool initFromGw(const RskWorkEth &latestRskBlockJson,
                  const string &blockJson);

  string seedHash_;
};

class StratumJobSia : public StratumJob
{
public:
  bool unserializeFromJson(const char *s, size_t len) override;
  uint32 jobTime() const override { return nTime_; }
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
  bool unserializeFromJson(const char *s, size_t len) override;
  BlockHeaderBytom blockHeader_;
  string seed_;
};
#endif
