/*
 The MIT License (MIT)

 Copyright (C) 2017 RSK Labs Ltd.

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

/**
  File: RskSolvedShareData.h
  Purpose: Object that represents RSK data needed to build a solution block

  @author Martin Medina
  @copyright RSK Labs Ltd.
*/

#ifndef RSK_SOLVED_SHARE_DATA_H_
#define RSK_SOLVED_SHARE_DATA_H_

#include "bitcoin/StratumBitcoin.h"

class RskSolvedShareData {
public:
  uint64_t jobId_;
  int64_t workerId_; // found by who
  int32_t userId_;
  int32_t height_;
  BitcoinHeaderData headerData_;
  char workerFullName_[40]; // <UserName>.<WorkerName>
  char feesForMiner_[80];
  char rpcAddress_[80];
  char rpcUserPwd_[80];

  RskSolvedShareData()
    : jobId_(0)
    , workerId_(0)
    , userId_(0)
    , height_(0) {
    memset(workerFullName_, 0, sizeof(workerFullName_));
    memset(feesForMiner_, 0, sizeof(feesForMiner_));
    memset(rpcAddress_, 0, sizeof(rpcAddress_));
    memset(rpcUserPwd_, 0, sizeof(rpcUserPwd_));
  }
};

#endif