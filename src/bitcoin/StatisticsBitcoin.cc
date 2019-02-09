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

#include "StatisticsBitcoin.h"

#include "StratumBitcoin.h"
#include "BitcoinUtils.h"

template <>
void ShareStatsDay<ShareBitcoin>::processShare(
    uint32_t hourIdx, const ShareBitcoin &share) {
  ScopeLock sl(lock_);

  if (StratumStatus::isAccepted(share.status())) {
    shareAccept1h_[hourIdx] += share.sharediff();
    shareAccept1d_ += share.sharediff();

    double score = share.score();
    double reward = GetBlockReward(share.height(), Params().GetConsensus());
    double earn = score * reward;

    score1h_[hourIdx] += score;
    score1d_ += score;
    earn1h_[hourIdx] += earn;
    earn1d_ += earn;

  } else {
    shareReject1h_[hourIdx] += share.sharediff();
    shareReject1d_ += share.sharediff();
  }
  modifyHoursFlag_ |= (0x01u << hourIdx);
}

///////////////  template instantiation ///////////////
// Without this, some linking errors will issued.
// If you add a new derived class of Share, add it at the following.
template class ShareStatsDay<ShareBitcoin>;
// template class ShareStatsDay<ShareBytom>;
