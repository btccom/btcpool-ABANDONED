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
class DiffController {
public:
  // max diff, cannot large than 2^62.
  const uint64_t kMaxDiff_;
  // min diff
  const uint64_t kMinDiff_;

  const time_t kDiffWindow_; // time window, seconds, 60*N
  const time_t kRecordSeconds_; // every N seconds as a record

  time_t startTime_; // first job send time
  uint64_t minDiff_;
  uint64_t curDiff_;
  int32_t curHashRateLevel_;
  StatsWindow<double> sharesNum_; // share count
  StatsWindow<uint64_t> shares_; // share

  void setCurDiff(uint64_t curDiff); // set current diff with bounds checking
  virtual uint64_t _calcCurDiff();
  int adjustHashRateLevel(const double hashRateT);

  inline bool isFullWindow(const time_t now) {
    return now >= startTime_ + kDiffWindow_;
  }

private:
  double minerCoefficient(const time_t now, const int64_t idx);

public:
  DiffController(
      const uint64_t defaultDifficulty,
      const uint64_t maxDifficulty,
      const uint64_t minDifficulty,
      const uint32_t shareAvgSeconds,
      const uint32_t diffAdjustPeriod)
    : kMaxDiff_(maxDifficulty)
    , kMinDiff_(minDifficulty)
    , kDiffWindow_(diffAdjustPeriod)
    , kRecordSeconds_(shareAvgSeconds)
    , startTime_(0)
    , curHashRateLevel_(0)
    , sharesNum_(kDiffWindow_ / kRecordSeconds_)
    , /* every N seconds as a record */
    shares_(kDiffWindow_ / kRecordSeconds_) {
    // Cannot large than 2^62.
    // If `kMaxDiff_` be 2^63, user can set `kMinDiff_` equals 2^63,
    // then `kMinDiff_*2` will be zero when next difficulty decrease and
    // DiffController::_calcCurDiff() will infinite loop.
    if (kMaxDiff_ > 0x4000000000000000ull) {
      LOG(FATAL)
          << "too large max_difficulty, it should <= 0x4000000000000000.";
    }

    if (kMinDiff_ < 1) {
      LOG(FATAL) << "too small min_difficulty, it should >= 1.";
    }

    if (kMinDiff_ > kMaxDiff_) {
      LOG(FATAL) << "min_difficulty cannot large than max_difficulty";
    }

    if (kDiffWindow_ < kRecordSeconds_) {
      LOG(FATAL) << "share_avg_seconds cannot large than diff_adjust_period";
    }

    setMinDiff(minDifficulty);
    resetCurDiff(defaultDifficulty);
  }

  DiffController(const DiffController &other)
    : kMaxDiff_(other.kMaxDiff_)
    , kMinDiff_(other.kMinDiff_)
    , kDiffWindow_(other.kDiffWindow_)
    , kRecordSeconds_(other.kRecordSeconds_)
    , startTime_(0)
    , minDiff_(other.minDiff_)
    , curDiff_(other.curDiff_)
    , curHashRateLevel_(other.curHashRateLevel_)
    , sharesNum_(
          other.kDiffWindow_ /
          other.kRecordSeconds_) /* every N seconds as a record */
    , shares_(other.kDiffWindow_ / other.kRecordSeconds_) {}

  virtual ~DiffController() {}

  // recalc miner's diff before send an new stratum job
  uint64_t calcCurDiff();

  // we need to add every share, so we can calc worker's hashrate
  void addShare(const uint64_t share);

  // maybe worker has it's own min diff
  void setMinDiff(uint64_t minDiff);

  // use when handle cmd: mining.suggest_difficulty & mining.suggest_target
  void resetCurDiff(uint64_t curDiff);
};
#endif
