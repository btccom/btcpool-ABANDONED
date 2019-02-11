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

#include "glog/logging.h"

////////////////////////////////// StatsWindow /////////////////////////////////
// none thread safe
template <typename T>
class StatsWindow {
  int64_t maxRingIdx_; // max ring idx
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
  void mapDivide(const T val);

  int32_t getWindowSize() const { return windowSize_; }
};

//////////////////////////////////  WorkerKey  /////////////////////////////////
class WorkerKey {
public:
  int32_t userId_;
  int64_t workerId_;

  WorkerKey(const int32_t userId, const int64_t workerId)
    : userId_(userId)
    , workerId_(workerId) {}

  WorkerKey &operator=(const WorkerKey &r) {
    userId_ = r.userId_;
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
template <>
struct hash<WorkerKey> {
public:
  size_t operator()(const WorkerKey &k) const {
    size_t h1 = std::hash<int32_t>()(k.userId_);
    size_t h2 = std::hash<int64_t>()(k.workerId_);
    return h1 ^ (h2 << 1);
  }
};
} // namespace std

/////////////////////////////////  ShareStats  /////////////////////////////////
class ShareStats {
public:
  uint64_t shareAccept_;
  uint64_t shareReject_;
  double rejectRate_;
  double earn_;

  ShareStats()
    : shareAccept_(0U)
    , shareReject_(0U)
    , rejectRate_(0.0)
    , earn_(0.0) {}
};

///////////////////////////////  ShareStatsDay  ////////////////////////////////
// thread-safe
template <class SHARE>
class ShareStatsDay {
public:
  // hours
  uint64_t shareAccept1h_[24] = {0};
  uint64_t shareReject1h_[24] = {0};
  double score1h_[24] = {0.0}; // For reference only, it is no longer the basis
                               // for earnings calculation
  double earn1h_[24] = {0.0};

  // daily
  uint64_t shareAccept1d_ = 0;
  uint64_t shareReject1d_ = 0;
  double score1d_ = 0; // For reference only, it is no longer the basis for
                       // earnings calculation
  double earn1d_ = 0;

  // mark which hour data has been modified: 23, 22, ...., 0
  uint32_t modifyHoursFlag_;
  mutex lock_;

  ShareStatsDay() = default;
  ShareStatsDay(const ShareStatsDay &r) = default;
  ShareStatsDay &operator=(const ShareStatsDay &r) = default;

  void processShare(uint32_t hourIdx, const SHARE &share, bool acceptStale);
  double getShareReward(const SHARE &share);
  void getShareStatsHour(uint32_t hourIdx, ShareStats *stats);
  void getShareStatsDay(ShareStats *stats);
};

///////////////////////////////  DuplicateShareCheckerT
///////////////////////////////////
// Used to detect duplicate share attacks.
// Interface
template <class SHARE>
class DuplicateShareChecker {
public:
  virtual ~DuplicateShareChecker() {}
  virtual bool addShare(const SHARE &share) = 0;
};

///////////////////////////////  DuplicateShareCheckerT
///////////////////////////////////
// Used to detect duplicate share attacks on ETH mining.
template <class SHARE, class GSHARE>
class DuplicateShareCheckerT : public DuplicateShareChecker<SHARE> {
public:
  using GShareSet = std::set<GSHARE>;

  DuplicateShareCheckerT(uint32_t trackingHeightNumber)
    : trackingHeightNumber_(trackingHeightNumber) {
    if (trackingHeightNumber == 0) {
      LOG(FATAL)
          << "DuplicateShareChecker: trackingHeightNumber should not be 0.";
    }
  }

  bool addGShare(uint32_t height, const GSHARE &gshare) {
    GShareSet &gset = gshareSetMap_[height];

    auto itr = gset.find(gshare);
    if (itr != gset.end()) {
      return false; // already exist
    }

    gset.insert(gshare);

    if (gshareSetMap_.size() > trackingHeightNumber_) {
      clearExcessGShareSet();
    }

    return true;
  }

  bool addShare(const SHARE &share) {
    return addGShare(share.height(), GSHARE(share));
  }

  size_t gshareSetMapSize() { return gshareSetMap_.size(); }

private:
  inline void clearExcessGShareSet() {
    for (auto itr = gshareSetMap_.begin();
         gshareSetMap_.size() > trackingHeightNumber_;
         itr = gshareSetMap_.erase(itr))
      ;
  }

  std::map<uint32_t /*height*/, GShareSet> gshareSetMap_;
  const uint32_t
      trackingHeightNumber_; // if set to 3, max(gshareSetMap_.size()) == 3
};

#include "Statistics.inl"

#endif
