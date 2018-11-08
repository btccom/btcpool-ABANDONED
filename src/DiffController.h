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
#ifndef DIFF_CONTROLLER_H_
#define DIFF_CONTROLLER_H_

#include "Common.h"
#include "Statistics.h"


//////////////////////////////// DiffController ////////////////////////////////
class DiffController
{
public:
  //
  // max diff: 2^62
  //
  // Cannot large than 2^62.
  // If `kMaxDiff_` be 2^63, user can set `kMinDiff_` equals 2^63,
  // then `kMinDiff_*2` will be zero when next difficulty decrease and
  // DiffController::_calcCurDiff() will infinite loop.
  //static const uint64 kMaxDiff_ = 4611686018427387904ull;
  // min diff
  //static const uint64 kMinDiff_ = 64;

  //static const time_t kDiffWindow_    = 900;   // time window, seconds, 60*N
  //static const time_t kRecordSeconds_ = 20;    // every N seconds as a record
#ifdef NDEBUG
  // If not debugging, set default to 16384
  static const uint64 kDefaultDiff_ = 16384; // default diff, 2^N
#else
  // debugging enabled
  static const uint64 kDefaultDiff_ = 128; // default diff, 2^N
#endif /* NDEBUG */

  time_t startTime_; // first job send time
  const uint64 kMinDiff_;
  uint64 minDiff_;
  const uint64 kMaxDiff_;
  uint64 curDiff_;
  int32_t curHashRateLevel_;
  const time_t kRecordSeconds_;
  int32_t shareAvgSeconds_;
  time_t kDiffWindow_;
  StatsWindow<double> sharesNum_; // share count
  StatsWindow<uint64> shares_;    // share

  void setCurDiff(uint64 curDiff); // set current diff with bounds checking
  virtual uint64 _calcCurDiff();
  int adjustHashRateLevel(const double hashRateT);

  inline bool isFullWindow(const time_t now)
  {
    return now >= startTime_ + kDiffWindow_;
  }
private:
  double minerCoefficient(const time_t now, const int64_t idx);

public:
  DiffController(const uint64 defaultDifficulty,
                 const uint64 maxDifficulty,
                 const uint64 minDifficulty,
                 const uint32 shareAvgSeconds,
                 const uint32 diffAdjustPeriod) : startTime_(0),
                                              kMinDiff_(minDifficulty),
                                              minDiff_(minDifficulty),
                                              kMaxDiff_(maxDifficulty),
                                              curDiff_(defaultDifficulty),
                                              curHashRateLevel_(0),
                                              kRecordSeconds_(shareAvgSeconds),
                                              kDiffWindow_(diffAdjustPeriod),
                                              sharesNum_(kDiffWindow_ / kRecordSeconds_), /* every N seconds as a record */
                                              shares_(kDiffWindow_ / kRecordSeconds_)
  {
    assert(curDiff_ <= kMaxDiff_);
    assert(kMinDiff_ <= curDiff_);
    assert(sharesNum_.getWindowSize() > 0);
    assert(shares_.getWindowSize() > 0);

    if (shareAvgSeconds >= 1 && shareAvgSeconds <= 60)
    {
      shareAvgSeconds_ = shareAvgSeconds;
    }
    else
    {
      shareAvgSeconds_ = 8;
    }
  }

  DiffController(const DiffController& other)
    : startTime_(0)
    , kMinDiff_(other.kMinDiff_)
    , minDiff_(other.minDiff_)
    , kMaxDiff_(other.kMaxDiff_)
    , curDiff_(other.curDiff_)
    , curHashRateLevel_(other.curHashRateLevel_)
    , kRecordSeconds_(other.kRecordSeconds_)
    , shareAvgSeconds_(other.shareAvgSeconds_)
    , kDiffWindow_(other.kDiffWindow_)
    , sharesNum_(other.kDiffWindow_ / other.kRecordSeconds_) /* every N seconds as a record */
    , shares_(other.kDiffWindow_  / other.kRecordSeconds_) {
  }

  virtual ~DiffController() {}

  // recalc miner's diff before send an new stratum job
  uint64 calcCurDiff();

  // we need to add every share, so we can calc worker's hashrate
  void addAcceptedShare(const uint64 share);

  // maybe worker has it's own min diff
  void setMinDiff(uint64 minDiff);

  // use when handle cmd: mining.suggest_difficulty & mining.suggest_target
  void resetCurDiff(uint64 curDiff);
};
#endif
