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
#ifndef STATISTICS_H_
#define STATISTICS_H_

#include "Common.h"
#include "Kafka.h"
#include "MySQLConnection.h"
#include "Stratum.h"

#include <event2/event.h>
#include <event2/http.h>
#include <event2/buffer.h>
#include <event2/util.h>
#include <event2/keyvalq_struct.h>

#include <string.h>
#include <pthread.h>
#include <memory>

#include <uint256.h>


////////////////////////////////// StatsWindow /////////////////////////////////
// none thread safe
template <typename T>
class StatsWindow {
  int64_t maxRingIdx_;  // max ring idx
  int32_t windowSize_;
  std::vector<T> elements_;

public:
  StatsWindow(const int windowSize);
  // TODO
//  bool unserialize(const ...);
//  void serialize(...);

  void clear();

  bool insert(const int64_t ringIdx, const T val);

  T sum(int64_t beginRingIdx, int len);
  T sum(int64_t beginRingIdx);

  void mapMultiply(const T val);
  void mapDivide  (const T val);

  int32_t getWindowSize() const { return windowSize_; }
};

//----------------------

template <typename T>
StatsWindow<T>::StatsWindow(const int windowSize)
:maxRingIdx_(-1), windowSize_(windowSize), elements_(windowSize) {
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
  if (maxRingIdx_ > curRingIdx + windowSize_) {  // too small index, drop it
    return false;
  }

  if (maxRingIdx_ == -1/* first insert */ ||
      curRingIdx - maxRingIdx_ > windowSize_/* all data expired */) {
    clear();
    maxRingIdx_ = curRingIdx;
  }

  while (maxRingIdx_ < curRingIdx) {
    maxRingIdx_++;
    elements_[maxRingIdx_ % windowSize_] = 0;  // reset
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


//////////////////////////////////  WorkerKey  /////////////////////////////////
class WorkerKey {
public:
  int32_t userId_;
  int64_t workerId_;

  WorkerKey(const int32_t userId, const int64_t workerId):
  userId_(userId), workerId_(workerId) {}

  WorkerKey& operator=(const WorkerKey &r) {
    userId_   = r.userId_;
    workerId_ = r.workerId_;
    return *this;
  }

  bool operator==(const WorkerKey &r) const {
    if (userId_ == r.userId_ && workerId_ == r.workerId_) {
      return true;
    }
    return false;
  }
};

// we use WorkerKey in std::unordered_map, so need to write it's hash func
namespace std {
template<>
class hash<WorkerKey> {
public:
  size_t operator()(const WorkerKey &k) const
  {
    size_t h1 = std::hash<int32_t>()(k.userId_);
    size_t h2 = std::hash<int64_t>()(k.workerId_);
    return h1 ^ ( h2 << 1 );
  }
};
}


/////////////////////////////////  ShareStats  /////////////////////////////////
class ShareStats {
public:
  uint64_t shareAccept_;
  uint64_t shareReject_;
  double   rejectRate_;
  double   earn_;

  ShareStats(): shareAccept_(0U), shareReject_(0U), rejectRate_(0.0), earn_(0.0) {}
};


///////////////////////////////  ShareStatsDay  ////////////////////////////////
// thread-safe
template <class SHARE>
class ShareStatsDay {
public:
  // hours
  uint64_t shareAccept1h_[24] = {0};
  uint64_t shareReject1h_[24] = {0};
  double   score1h_[24] = {0.0}; // For reference only, it is no longer the basis for earnings calculation
  double   earn1h_[24] = {0.0};

  // daily
  uint64_t shareAccept1d_ = 0;
  uint64_t shareReject1d_ = 0;
  double   score1d_ = 0; // For reference only, it is no longer the basis for earnings calculation
  double   earn1d_ = 0;

  // mark which hour data has been modified: 23, 22, ...., 0
  uint32_t modifyHoursFlag_;
  mutex lock_;

  ShareStatsDay() = default;
  ShareStatsDay(const ShareStatsDay &r) = default;
  ShareStatsDay &operator=(const ShareStatsDay &r) = default;

  void processShare(uint32_t hourIdx, const SHARE &share);
  void getShareStatsHour(uint32_t hourIdx, ShareStats *stats);
  void getShareStatsDay(ShareStats *stats);
};


///////////////////////////////  GlobalShareEth  ////////////////////////////////
// Used to detect duplicate share attacks on ETH mining.
struct GlobalShareEth {
  uint64_t headerHash_;
  uint64_t nonce_;

  GlobalShareEth() = default;

  GlobalShareEth(const ShareEth &share)
    : headerHash_(share.headerHash_)
    , nonce_(share.nonce_)
  {}

  GlobalShareEth& operator=(const GlobalShareEth &r) = default;

  bool operator<(const GlobalShareEth &r) const {
    if (headerHash_ < r.headerHash_ ||
        (headerHash_ == r.headerHash_ && nonce_ < r.nonce_)) {
      return true;
    }
    return false;
  }
};
///////////////////////////////  GlobalShareBytom  ////////////////////////////////
// Used to detect duplicate share attacks on Bytom mining.
struct GlobalShareBytom {
  BytomCombinedHeader combinedHeader_;

  GlobalShareBytom() = delete;

  GlobalShareBytom(const ShareBytom &share)
    : combinedHeader_(share.combinedHeader_)
  {}

  GlobalShareBytom& operator=(const GlobalShareBytom &r) = default;

  bool operator<(const GlobalShareBytom &r) const {
    return std::memcmp(&combinedHeader_, &r.combinedHeader_, sizeof(BytomCombinedHeader)) < 0;
  }
};

///////////////////////////////  DuplicateShareCheckerT  ////////////////////////////////
// Used to detect duplicate share attacks.
// Interface
template <class SHARE>
class DuplicateShareChecker {
public:
  virtual ~DuplicateShareChecker() {}
  virtual bool addShare(const SHARE &share) = 0;
};

///////////////////////////////  DuplicateShareCheckerT  ////////////////////////////////
// Used to detect duplicate share attacks on ETH mining.
template <class SHARE, class GSHARE>
class DuplicateShareCheckerT : public DuplicateShareChecker<SHARE> {
public:
  using GShareSet = std::set<GSHARE>;

  DuplicateShareCheckerT(uint32_t trackingHeightNumber)
    : trackingHeightNumber_(trackingHeightNumber)
  {
    if (trackingHeightNumber == 0) {
      LOG(FATAL) << "DuplicateShareChecker: trackingHeightNumber should not be 0.";
    }
  }

  bool addGShare(uint32_t height, const GSHARE &gshare) {
    GShareSet &gset = gshareSetMap_[height];

    auto itr = gset.find(gshare);
    if (itr != gset.end()) {
      return false;  // already exist
    }

    gset.insert(gshare);

    if (gshareSetMap_.size() > trackingHeightNumber_) {
      clearExcessGShareSet();
    }

    return true;
  }

  bool addShare(const SHARE &share) {
    return addGShare(share.height_, GSHARE(share));
  }

  size_t gshareSetMapSize() {
    return gshareSetMap_.size();
  }

private:
  inline void clearExcessGShareSet() {
    for (
      auto itr = gshareSetMap_.begin();
      gshareSetMap_.size() > trackingHeightNumber_;
      itr = gshareSetMap_.erase(itr)
    );
  }

  std::map<uint32_t /*height*/, GShareSet> gshareSetMap_;
  const uint32_t trackingHeightNumber_; // if set to 3, max(gshareSetMap_.size()) == 3
};

////////////////////////////  Alias  ////////////////////////////
using DuplicateShareCheckerEth = DuplicateShareCheckerT<ShareEth, GlobalShareEth>;
using DuplicateShareCheckerBytom = DuplicateShareCheckerT<ShareBytom, GlobalShareBytom>;

#endif
