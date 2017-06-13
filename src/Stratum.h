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

#include "zcash/uint256.h"
#include "zcash/base58.h"

// TODO: update when next halving, about 2020-10.
// current height: 127716 (2017-06-07), next halving about 40 months from now.
// 25 ZEC * 80% = 10 ZEC.
#define BLOCK_REWARD 1000000000ll

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
// https://github.com/str4d/zips/blob/77-zip-stratum/drafts/str4d-stratum/draft1.rst
//
// "mining.notify"
//
// {"id": null,
//  "method": "mining.notify",
//  "params": ["JOB_ID", "VERSION", "PREVHASH", "MERKLEROOT",
//             "RESERVED", "TIME", "BITS", CLEAN_JOBS
//            ]
// }
//
// JOB_ID     - The id of this job.
// VERSION    - The block header version, encoded as in a block header (little-endian int32_t).
// PREVHASH   - The 32-byte hash of the previous block, encoded as in a block header.
// MERKLEROOT - The 32-byte Merkle root of the transactions in this block, encoded as in a block header.
// RESERVED   - A 32-byte reserved field, encoded as in a block header.
// TIME       - The block time suggested by the server, encoded as in a block header.
// BITS       - The current network difficulty target, represented in compact format, encoded as in a block header.
// CLEAN_JOBS - If true, a new block has arrived. The miner SHOULD abandon all previous jobs.
//
//
class StratumJob {
public:
  // jobId: timestamp + originalBlockHash, hex string, we need to make sure jobId is
  // unique in a some time, jobId can convert to uint64_t
  uint64_t jobId_;
  string   originalHash_;  // gbt hash

  int32_t  height_;
  int32_t  txCount_;  // how many txs in this block, include coinbase
  uint32_t minTime_;
  uint32_t maxTime_;

  CBlockHeader header_;

  void SetNull();

public:
  StratumJob();

  string serializeToJson() const;
  bool unserializeFromJson(const char *s, size_t len);

  bool initFromGbt(const char *gbt);
  bool isEmptyBlock() const;
};

#endif
