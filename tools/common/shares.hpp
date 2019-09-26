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
#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>

#include <string>

#include "Network.h"
#include "utils.hpp"

struct ShareBitcoinV1 {
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
  uint64_t share_ = 0;
  uint32_t timestamp_ = 0;
  uint32_t blkBits_ = 0;
  int32_t result_ = 0;
  // Even if the field does not exist,
  // gcc will add the field as a padding
  // under the default memory alignment parameter.
  int32_t padding_ = 0;

  std::string toString() const {
    char ipStr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(ip_), ipStr, INET_ADDRSTRLEN);
    return StringFormat(
        "share(jobId: %u, ip: %s, userId: %d, "
        "workerId: %d, timeStamp: %u/%s, share: %u, "
        "blkBits: %08x, result: %d)",
        jobId_,
        ipStr,
        userId_,
        workerHashId_,
        timestamp_,
        date("%F %T", timestamp_).c_str(),
        share_,
        blkBits_,
        result_);
  }
};

static_assert(
    sizeof(ShareBitcoinV1) == 48, "ShareBitcoinV1 should be 48 bytes");

class StratumStatusV2 {
public:
  enum {
    // make ACCEPT and SOLVED be two singular value,
    // so code bug is unlikely to make false ACCEPT shares

    // share reached the job target (but may not reached the network target)
    ACCEPT = 1798084231, // bin(01101011 00101100 10010110 10000111)

    // share reached the job target but the job is stale
    // if uncle block is allowed in the chain, share can be accept as this
    // status
    ACCEPT_STALE = 950395421, // bin(00111000 10100101 11100010 00011101)

    // share reached the network target
    SOLVED = 1422486894, // bin(‭01010100 11001001 01101101 01101110‬)

    // share reached the network target but the job is stale
    // if uncle block is allowed in the chain, share can be accept as this
    // status
    SOLVED_STALE = 1713984938, // bin(01100110 00101001 01010101 10101010)
  };

  inline static bool isAccepted(int status) {
    return (status == ACCEPT) || (status == ACCEPT_STALE) ||
        (status == SOLVED) || (status == SOLVED_STALE);
  }

  inline static bool isStale(int status) {
    return (status == ACCEPT_STALE) || (status == SOLVED_STALE);
  }

  inline static bool isSolved(int status) {
    return (status == SOLVED) || (status == SOLVED_STALE);
  }

  inline static std::string toString(int status) {
    return isAccepted(status) ? "accept" : "reject";
  }
};

struct ShareBitcoinV2 {
  const static uint32_t CURRENT_VERSION =
      0x00010003u; // first 0001: bitcoin, second 0003: version 3.

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

  bool isValid() const {
    if (version_ != CURRENT_VERSION) {
      LOG(INFO) << "version mismatched! CURRENT_VERSION: " << CURRENT_VERSION
                << ", version_: " << version_;
      return false;
    }

    if (checkSum_ != checkSum()) {
      LOG(INFO) << "checkSum mismatched! checkSum_: " << checkSum_
                << ", checkSum(): " << checkSum();
      return false;
    }

    if (jobId_ == 0 || userId_ == 0 || workerHashId_ == 0 || height_ == 0 ||
        blkBits_ == 0 || shareDiff_ == 0) {
      LOG(INFO) << "some field of the share is empty!";
      return false;
    }

    return true;
  }

  std::string toString() const {
    double networkDifficulty = 0.0;
    BitsToDifficulty(blkBits_, &networkDifficulty);

    return StringFormat(
        "share(jobId: %u, ip: %s, userId: %d, "
        "workerId: %d, time: %u/%s, height: %u, "
        "blkBits: %08x/%f, shareDiff: %u, "
        "status: %d/%s)",
        jobId_,
        ip_.toString().c_str(),
        userId_,
        workerHashId_,
        timestamp_,
        date("%F %T", timestamp_).c_str(),
        height_,
        blkBits_,
        networkDifficulty,
        shareDiff_,
        status_,
        StratumStatusV2::toString(status_).c_str());
  }

  bool toShareBitcoinV1(ShareBitcoinV1 &shareV1) {
    if (!isValid()) {
      LOG(INFO) << "invalid share: " << toString();
      return false;
    }

    shareV1.jobId_ = jobId_;
    shareV1.workerHashId_ = workerHashId_;
    shareV1.ip_ = ip_.toIpv4Int();
    shareV1.userId_ = userId_;
    shareV1.share_ = shareDiff_;
    shareV1.timestamp_ = (uint32_t)timestamp_;
    shareV1.blkBits_ = blkBits_;
    shareV1.result_ = (StratumStatusV2::isAccepted(status_))
        ? ShareBitcoinV1::ACCEPT
        : ShareBitcoinV1::REJECT;
    return true;
  }
};

static_assert(
    sizeof(ShareBitcoinV2) == 80, "ShareBitcoinV2 should be 80 bytes");
