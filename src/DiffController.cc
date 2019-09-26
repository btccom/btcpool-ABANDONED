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
#include "DiffController.h"

//////////////////////////////// DiffController ////////////////////////////////
void DiffController::setMinDiff(uint64_t minDiff) {
  if (minDiff < kMinDiff_) {
    minDiff = kMinDiff_;
  } else if (minDiff > kMaxDiff_) {
    minDiff = kMaxDiff_;
  }

  minDiff_ = minDiff;
}

void DiffController::setCurDiff(uint64_t curDiff) {
  if (curDiff < kMinDiff_) {
    curDiff = kMinDiff_;
  } else if (curDiff > kMaxDiff_) {
    curDiff = kMaxDiff_;
  }

  curDiff_ = curDiff;
}

void DiffController::resetCurDiff(uint64_t curDiff) {
  setCurDiff(curDiff);

  // set to zero
  sharesNum_.mapMultiply(0);
  shares_.mapMultiply(0);
}

void DiffController::addShare(const uint64_t share) {
  const int64_t k = time(nullptr) / kRecordSeconds_;
  sharesNum_.insert(k, 1.0);
  shares_.insert(k, share);
}

//
// level:  min ~ max, coefficient
//
// 0 :    0 ~    4 T,  1.0
// 1 :    4 ~    8 T,  1.0
// 2 :    8 ~   16 T,  1.0
// 3 :   16 ~   32 T,  1.2
// 4 :   32 ~   64 T,  1.5
// 5 :   64 ~  128 T,  2.0
// 6 :  128 ~  256 T,  3.0
// 7 :  256 ~  512 T,  4.0
// 8 :  512 ~  ... T,  6.0
//

static int __hashRateDown(int level) {
  const int levels[] = {0, 4, 8, 16, 32, 64, 128, 256};
  if (level >= 8) {
    return 512;
  }
  assert(level >= 0 && level <= 7);
  return levels[level];
}

static int __hashRateUp(int level) {
  const int levels[] = {4, 8, 16, 32, 64, 128, 256, 512};
  assert(level >= 0 && level <= 7);
  if (level >= 8) {
    return 0x7fffffffL; // INT32_MAX
  }
  return levels[level];
}

// TODO: test case
int DiffController::adjustHashRateLevel(const double hashRateT) {
  // hashrate is always danceing,
  // so need to use rate high and low to check it's level
  const double rateHigh = 1.50;
  const double rateLow = 0.75;

  // reduce level
  if (curHashRateLevel_ > 0 && hashRateT < __hashRateDown(curHashRateLevel_)) {
    while (curHashRateLevel_ > 0 &&
           hashRateT <= __hashRateDown(curHashRateLevel_) * rateLow) {
      curHashRateLevel_--;
    }
    return curHashRateLevel_;
  }

  // increase level
  if (curHashRateLevel_ <= 7 && hashRateT > __hashRateUp(curHashRateLevel_)) {
    while (curHashRateLevel_ <= 7 &&
           hashRateT >= __hashRateUp(curHashRateLevel_) * rateHigh) {
      curHashRateLevel_++;
    }
    return curHashRateLevel_;
  }

  return curHashRateLevel_;
}

double DiffController::minerCoefficient(const time_t now, const int64_t idx) {
  if (now <= startTime_) {
    return 1.0;
  }
  uint64_t shares = shares_.sum(idx);
  time_t shareWindow = isFullWindow(now) ? kDiffWindow_ : (now - startTime_);
  double hashRateT = (double)shares * pow(2, 32) / shareWindow / pow(10, 12);
  adjustHashRateLevel(hashRateT);
  assert(curHashRateLevel_ >= 0 && curHashRateLevel_ <= 8);

  const double c[] = {1.0, 1.0, 1.0, 1.2, 1.5, 2.0, 3.0, 4.0, 6.0};
  assert(sizeof(c) / sizeof(c[0]) == 9);
  return c[curHashRateLevel_];
}

uint64_t DiffController::calcCurDiff() {
  uint64_t diff = _calcCurDiff();
  if (diff < minDiff_) {
    diff = minDiff_;
  }
  return diff;
}

uint64_t DiffController::_calcCurDiff() {
  const time_t now = time(nullptr);
  const int64_t k = now / kRecordSeconds_;
  const double sharesCount = (double)sharesNum_.sum(k);
  if (startTime_ == 0) { // first time, we set the start time
    startTime_ = time(nullptr);
  }

  const double kRateHigh = 1.40;
  const double kRateLow = 0.40;
  double expectedCount = round(kDiffWindow_ / (double)kRecordSeconds_);

  if (isFullWindow(now)) { /* have a full window now */
    // big miner have big expected share count to make it looks more smooth.
    expectedCount *= minerCoefficient(now, k);
  }
  if (expectedCount > kDiffWindow_) {
    expectedCount = kDiffWindow_; // one second per share is enough
  }

  // this is for very low hashrate miner, eg. USB miners
  // should received at least one share every 60 seconds
  if (!isFullWindow(now) && now >= startTime_ + 60 &&
      sharesCount <= (int32_t)((now - startTime_) / 60.0) &&
      curDiff_ >= minDiff_ * 2) {
    setCurDiff(curDiff_ / 2);
    sharesNum_.mapMultiply(2.0);
    return curDiff_;
  }

  // too fast
  if (sharesCount > expectedCount * kRateHigh) {
    while (sharesNum_.sum(k) > expectedCount && curDiff_ < kMaxDiff_) {
      setCurDiff(curDiff_ * 2);
      sharesNum_.mapDivide(2.0);
    }
    return curDiff_;
  }

  // too slow
  if (isFullWindow(now) && curDiff_ >= minDiff_ * 2) {
    while (sharesNum_.sum(k) < expectedCount * kRateLow &&
           curDiff_ >= minDiff_ * 2) {
      setCurDiff(curDiff_ / 2);
      sharesNum_.mapMultiply(2.0);
    }
    assert(curDiff_ >= minDiff_);
    return curDiff_;
  }

  return curDiff_;
}
