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

#include <deque>

#include "Common.h"
#include "Statistics.h"

class DiffController {
  static const int32_t kMinDiff_       = 1;     // min diff
  static const int32_t kDefaultDiff_   = 1024;  // default diff, 2^N
  static const int32_t kDiffWindow_    = 900;   // time window, seconds, 60*N
  static const int32_t kRecordSeconds_ = 10;    // every N seconds as a record

  time_t startTime_;  // first job send time
  StatsWindow<double> sharesNum_;  // share count
  StatsWindow<uint64> shares_;     // share

  uint64  minDiff_;
  uint64  curDiff_;
  int32_t shareAvgSeconds_;
  int32_t curHashRateLevel_;

  uint64 _calcCurDiff();
  int adjustHashRateLevel(const double hashRateT);
  double minerCoefficient(const time_t now, const int64_t idx);

  inline bool isFullWindow(const time_t now) {
    return now >= startTime_ + kDiffWindow_;
  }

public:
  DiffController(int32_t shareAvgSeconds) :
    minDiff_(kMinDiff_),
    curDiff_(kDefaultDiff_), startTime_(0),
    sharesNum_(kDiffWindow_/kRecordSeconds_), /* every N seconds as a record */
    shares_   (kDiffWindow_/kRecordSeconds_),
    curHashRateLevel_(0)
  {
    if (shareAvgSeconds >= 1 && shareAvgSeconds <= 60) {
      shareAvgSeconds_ = shareAvgSeconds;
    } else {
      shareAvgSeconds_ = 8;
    }
  }

  ~DiffController() {}

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
