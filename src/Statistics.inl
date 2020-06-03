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

#include "Stratum.h"

////////////////////////////////// StatsWindow /////////////////////////////////
template <typename T>
StatsWindow<T>::StatsWindow(const int windowSize)
  : maxRingIdx_(-1)
  , windowSize_(windowSize)
  , elements_(windowSize) {
}

template <typename T>
void StatsWindow<T>::mapMultiply(const T val) {
  for (int32_t i = 0; i < windowSize_; i++) {
    elements_[i] *= val;
  }
}

template <typename T>
void StatsWindow<T>::mapDivide(const T val) {
  for (int32_t i = 0; i < windowSize_; i++) {
    elements_[i] /= val;
  }
}

template <typename T>
void StatsWindow<T>::clear() {
  maxRingIdx_ = -1;
  elements_.clear();
  elements_.resize(windowSize_);
}

template <typename T>
bool StatsWindow<T>::insert(const int64_t curRingIdx, const T val) {
  if (maxRingIdx_ > curRingIdx + windowSize_) { // too small index, drop it
    return false;
  }

  if (maxRingIdx_ == -1 /* first insert */ ||
      curRingIdx - maxRingIdx_ > windowSize_ /* all data expired */) {
    clear();
    maxRingIdx_ = curRingIdx;
  }

  while (maxRingIdx_ < curRingIdx) {
    maxRingIdx_++;
    elements_[maxRingIdx_ % windowSize_] = 0; // reset
  }

  elements_[curRingIdx % windowSize_] += val;
  return true;
}

template <typename T>
T StatsWindow<T>::sum(int64_t beginRingIdx, int len) {
  T sum = 0;
  len = std::min(len, windowSize_);
  if (len <= 0 || beginRingIdx - len >= maxRingIdx_) {
    return 0;
  }
  int64_t endRingIdx = beginRingIdx - len;
  if (beginRingIdx > maxRingIdx_) {
    beginRingIdx = maxRingIdx_;
  }
  while (beginRingIdx > endRingIdx) {
    sum += elements_[beginRingIdx % windowSize_];
    beginRingIdx--;
  }
  return sum;
}

template <typename T>
T StatsWindow<T>::sum(int64_t beginRingIdx) {
  return sum(beginRingIdx, windowSize_);
}

///////////////////////////////  ShareStatsDay  ////////////////////////////////
template <class SHARE>
ShareStatsDay<SHARE>::ShareStatsDay(const string rpcurl) {
  rpcUrl_ = rpcurl;
}

template <class SHARE>
void ShareStatsDay<SHARE>::processShare(
    uint32_t hourIdx, SHARE &share, bool acceptStale) {
  ScopeLock sl(lock_);

  if (StratumStatus::isAccepted(share.status()) &&
      (acceptStale || !StratumStatus::isAcceptedStale(share.status()))) {
    shareAccept1h_[hourIdx] += share.sharediff();
    shareAccept1d_ += share.sharediff();
    updateAcceptDiff(share.sharediff());

    double score = share.score();
    double reward = getShareReward(share);
    double earn = score * reward;

    score1h_[hourIdx] += score;
    score1d_ += score;
    earn1h_[hourIdx] += earn;
    earn1d_ += earn;

  } else if (StratumStatus::isAnyStale(share.status())) {
    updateRejectDiff(share);
    shareStale1h_[hourIdx] += share.sharediff();
    shareStale1d_ += share.sharediff();
  } else {
    updateRejectDiff(share);
    shareRejects1h_[hourIdx][share.status()] += share.sharediff();
    shareRejects1d_[share.status()] += share.sharediff();
  }
  modifyHoursFlag_ |= (0x01u << hourIdx);
}

template <class SHARE>
void ShareStatsDay<SHARE>::getShareStatsHour(
    uint32_t hourIdx, ShareStats *stats) {
  ScopeLock sl(lock_);
  if (hourIdx > 23)
    return;

  stats->shareAccept_ = shareAccept1h_[hourIdx];
  stats->shareStale_ = shareStale1h_[hourIdx];
  stats->shareReject_ = sumRejectShares(shareRejects1h_[hourIdx]);
  stats->rejectDetail_ = generateRejectDetail(shareRejects1h_[hourIdx]);
  stats->earn_ = earn1h_[hourIdx];

  if (stats->shareReject_)
    stats->rejectRate_ =
        (stats->shareReject_ * 1.0 /
         (stats->shareAccept_ + stats->shareReject_));
  else
    stats->rejectRate_ = 0.0;
}

template <class SHARE>
void ShareStatsDay<SHARE>::getShareStatsDay(ShareStats *stats) {
  ScopeLock sl(lock_);
  stats->shareAccept_ = shareAccept1d_;
  stats->shareStale_ = shareStale1d_;
  stats->shareReject_ = sumRejectShares(shareRejects1d_);
  stats->rejectDetail_ = generateRejectDetail(shareRejects1d_);
  stats->earn_ = earn1d_;

  if (stats->shareReject_)
    stats->rejectRate_ =
        (stats->shareReject_ * 1.0 /
         (stats->shareAccept_ + stats->shareReject_));
  else
    stats->rejectRate_ = 0.0;
}

template <class SHARE>
ShareStatsDayNormalized<SHARE>::ShareStatsDayNormalized(const string rpcurl)
  : ShareStatsDay<SHARE>(rpcurl) {
}

template <class SHARE>
void ShareStatsDayNormalized<SHARE>::updateAcceptDiff(uint64_t diff) {
  if (diff > 0) {
    lastAcceptDiff_ = diff;
  }
}

template <class SHARE>
void ShareStatsDayNormalized<SHARE>::updateRejectDiff(SHARE &share) const {
  if (share.sharediff() > lastAcceptDiff_ * 4) {
    share.set_sharediff(lastAcceptDiff_ * 4);
  }
}
