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
#include "ShareLogger.h"

#include "Common.h"
#include "Stratum.h"
#include "Utils.h"
#include "utilities_js.hpp"

#include "bitcoin/BitcoinUtils.h"
#include "EthConsensus.h"

#include <algorithm>
#include <string>

#include <boost/algorithm/string.hpp>
#include <boost/thread.hpp>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <chainparams.h>

///////////////////////////////  ShareStatsDay  ////////////////////////////////
template <class SHARE>
void ShareStatsDay<SHARE>::getShareStatsHour(uint32_t hourIdx, ShareStats *stats) {
  ScopeLock sl(lock_);
  if (hourIdx > 23)
    return;

  stats->shareAccept_ = shareAccept1h_[hourIdx];
  stats->shareReject_ = shareReject1h_[hourIdx];
  stats->earn_        = earn1h_[hourIdx];

  if (stats->shareReject_)
  	stats->rejectRate_  = (stats->shareReject_ * 1.0 / (stats->shareAccept_ + stats->shareReject_));
  else
    stats->rejectRate_ = 0.0;
}

template <class SHARE>
void ShareStatsDay<SHARE>::getShareStatsDay(ShareStats *stats) {
  ScopeLock sl(lock_);
  stats->shareAccept_ = shareAccept1d_;
  stats->shareReject_ = shareReject1d_;
  stats->earn_        = earn1d_;

  if (stats->shareReject_)
    stats->rejectRate_  = (stats->shareReject_ * 1.0 / (stats->shareAccept_ + stats->shareReject_));
  else
    stats->rejectRate_ = 0.0;
}

template <>
void ShareStatsDay<ShareBitcoin>::processShare(uint32_t hourIdx, const ShareBitcoin &share) {
  ScopeLock sl(lock_);

  if (StratumStatus::isAccepted(share.status_)) {
    shareAccept1h_[hourIdx] += share.shareDiff_;
    shareAccept1d_          += share.shareDiff_;

    double score = share.score();
    double reward = GetBlockReward(share.height_, Params().GetConsensus());
    double earn = score * reward;

    score1h_[hourIdx] += score;
    score1d_          += score;
    earn1h_[hourIdx]  += earn;
    earn1d_           += earn;

  } else {
    shareReject1h_[hourIdx] += share.shareDiff_;
    shareReject1d_          += share.shareDiff_;
  }
  modifyHoursFlag_ |= (0x01u << hourIdx);
}

template <>
void ShareStatsDay<ShareEth>::processShare(uint32_t hourIdx, const ShareEth &share) {
  ScopeLock sl(lock_);

  if (StratumStatus::isAccepted(share.status_)) {
    shareAccept1h_[hourIdx] += share.shareDiff_;
    shareAccept1d_          += share.shareDiff_;

    double score = share.score();
    double reward = EthConsensus::getStaticBlockReward(share.height_, share.getChain());
    double earn = score * reward;

    score1h_[hourIdx] += score;
    score1d_          += score;
    earn1h_[hourIdx]  += earn;
    earn1d_           += earn;

  } else {
    shareReject1h_[hourIdx] += share.shareDiff_;
    shareReject1d_          += share.shareDiff_;
  }
  modifyHoursFlag_ |= (0x01u << hourIdx);
}

template <>
void ShareStatsDay<ShareBytom>::processShare(uint32_t hourIdx, const ShareBytom &share) {
  ScopeLock sl(lock_);

  if (StratumStatus::isAccepted(share.status_)) {
    shareAccept1h_[hourIdx] += share.shareDiff_;
    shareAccept1d_          += share.shareDiff_;

    double score = share.score();
    double reward = GetBlockRewardBytom(share.height_);
    double earn = score * reward;

    score1h_[hourIdx] += score;
    score1d_          += score;
    earn1h_[hourIdx]  += earn;
    earn1d_           += earn;

  } else {
    shareReject1h_[hourIdx] += share.shareDiff_;
    shareReject1d_          += share.shareDiff_;
  }
  modifyHoursFlag_ |= (0x01u << hourIdx);
}

///////////////  template instantiation ///////////////
// Without this, some linking errors will issued.
// If you add a new derived class of Share, add it at the following.
template class ShareStatsDay<ShareBitcoin>;
template class ShareStatsDay<ShareEth>;
template class ShareStatsDay<ShareBytom>;
