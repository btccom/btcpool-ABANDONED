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
#include "Utils.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <queue>

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>

#include <glog/logging.h>

#include "bitcoin/uint256.h"
#include "bitcoin/base58.h"

// TODO: update when next Halving
#define BLOCK_REWARD 1250000000ll

//
// max coinbase tx size, bytes
// WARNING: currently there is only 1 input and 1 or 2 output(if segwit has actived
//          there will be 2 outputs), so 250 bytes is enough
//
#define COINBASE_TX_MAX_SIZE   250

// default worker name
#define DEFAULT_WORKER_NAME    "__default__"


inline uint32_t jobId2Time(uint64_t jobId) {
  return (uint32_t)((jobId >> 32) & 0x00000000FFFFFFFFULL);
}

string filterWorkerName(const string &workerName);

inline string filterWorkerName(const char *workerName) {
  return filterWorkerName(std::string(workerName));
}

////////////////////////////////// FoundBlock //////////////////////////////////
class FoundBlock {
public:
  uint64_t jobId_;
  int64_t  workerId_;  // found by who
  int32_t  userId_;
  int32_t  height_;
  uint8_t  header80_[80];
  char     workerFullName_[40];  // <UserName>.<WorkerName>

  FoundBlock(): jobId_(0), workerId_(0), userId_(0), height_(0) {
    memset(header80_,       0, sizeof(header80_));
    memset(workerFullName_, 0, sizeof(workerFullName_));
  }
};

///////////////////////////////////// Share ////////////////////////////////////
class Share {
public:
  enum Result {
    // make default 0 as REJECT, so code bug is unlikely to make false ACCEPT shares
    REJECT    = 0,
    ACCEPT    = 1
  };
  
  uint64_t jobId_;
  int64_t  workerHashId_;
  uint32_t ip_;
  int32_t  userId_;
  uint64_t share_;
  uint32_t timestamp_;
  uint32_t blkBits_;
  int32_t  result_;

  Share():jobId_(0), workerHashId_(0), ip_(0), userId_(0), share_(0),
  timestamp_(0), blkBits_(0), result_(0) {}

  Share(const Share &r) {
    jobId_        = r.jobId_;
    workerHashId_ = r.workerHashId_;
    ip_           = r.ip_;
    userId_       = r.userId_;
    share_        = r.share_;
    timestamp_    = r.timestamp_;
    blkBits_      = r.blkBits_;
    result_       = r.result_;
  }

  Share& operator=(const Share &r) {
    jobId_        = r.jobId_;
    workerHashId_ = r.workerHashId_;
    ip_           = r.ip_;
    userId_       = r.userId_;
    share_        = r.share_;
    timestamp_    = r.timestamp_;
    blkBits_      = r.blkBits_;
    result_       = r.result_;
    return *this;
  }

  double score() const {
    if (share_ == 0 || blkBits_ == 0) { return 0.0; }
    double networkDifficulty = 0.0;
    BitsToDifficulty(blkBits_, &networkDifficulty);

    // Network diff may less than share diff on testnet or regression test network.
    // On regression test network, the network diff may be zero.
    // But no matter how low the network diff is, you can only dig one block at a time.
    if (networkDifficulty < (double)share_) { return 1.0; }

    return (double)share_ / networkDifficulty;
  }

  bool isValid() const {
    uint32_t jobTime = jobId2Time(jobId_);

    /* TODO: increase timestamp check before 2020-01-01 */
    if (userId_ > 0 && workerHashId_ != 0 && share_ > 0 &&
        timestamp_ > 1467816952U /* 2016-07-06 14:55:52 UTC+0 */ &&
        timestamp_ < 1577836800U /* 2020-01-01 00:00:00 UTC+0 */ &&
        jobTime    > 1467816952U /* 2016-07-06 14:55:52 UTC+0 */ &&
        jobTime    < 1577836800U /* 2020-01-01 00:00:00 UTC+0 */) {
      return true;
    }
    return false;
  }

  string toString() const {
    char ipStr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(ip_), ipStr, INET_ADDRSTRLEN);
    return Strings::Format("share(jobId: %" PRIu64", ip: %s, userId: %d, "
                           "workerId: %" PRId64", timeStamp: %u/%s, share: %" PRIu64", "
                           "blkBits: %08x, result: %d)",
                           jobId_, ipStr, userId_, workerHashId_,
                           timestamp_, date("%F %T", timestamp_).c_str(),
                           share_, blkBits_, result_);
  }
};

//////////////////////////////// StratumError ////////////////////////////////
class StratumError {
public:
  enum {
    NO_ERROR        = 0,

    UNKNOWN         = 20,
    JOB_NOT_FOUND   = 21,
    DUPLICATE_SHARE = 22,
    LOW_DIFFICULTY  = 23,
    UNAUTHORIZED    = 24,
    NOT_SUBSCRIBED  = 25,

    ILLEGAL_METHOD   = 26,
    ILLEGAL_PARARMS  = 27,
    IP_BANNED        = 28,
    INVALID_USERNAME = 29,
    INTERNAL_ERROR   = 30,
    TIME_TOO_OLD     = 31,
    TIME_TOO_NEW     = 32
  };
  static const char * toString(int err);
};



//////////////////////////////// StratumWorker ////////////////////////////////
class StratumWorker {
public:
  int32_t userId_;
  int64_t workerHashId_;  // substr(0, 8, HASH(wokerName))

  string fullName_;    // fullName = username.workername
  string userName_;
  string workerName_;  // workername, max is: 20

  void reset();

public:
  StratumWorker();
  void setUserIDAndNames(const int32_t userId, const string &fullName);
  string getUserName(const string &fullName) const ;

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
class StratumJob {
public:
  // jobId: timestamp + gbtHash, hex string, we need to make sure jobId is
  // unique in a some time, jobId can convert to uint64_t
  uint64_t jobId_;
  string   gbtHash_;        // gbt hash id
  uint256  prevHash_;
  string   prevHashBeStr_;  // little-endian hex, memory's order
  int32_t  height_;
  string   coinbase1_;
  string   coinbase2_;
  vector<uint256> merkleBranch_;

  int32_t  nVersion_;
  uint32_t nBits_;
  uint32_t nTime_;
  uint32_t minTime_;
  int64_t  coinbaseValue_;
  // if segwit is not active, it will be empty
  string   witnessCommitment_;

  uint256 networkTarget_;

  // namecoin merged mining
  uint32_t nmcAuxBits_;
  uint256  nmcAuxBlockHash_;
  uint256  nmcNetworkTarget_;
  int32_t  nmcHeight_;
  string   nmcRpcAddr_;
  string   nmcRpcUserpass_;


public:
  StratumJob();

  string serializeToJson() const;
  bool unserializeFromJson(const char *s, size_t len);

  bool initFromGbt(const char *gbt, const string &poolCoinbaseInfo,
                   const CBitcoinAddress &poolPayoutAddr,
                   const uint32_t blockVersion,
                   const string &nmcAuxBlockJson);
  bool isEmptyBlock();
};

#endif
