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
};

//----------------------

template <typename T>
StatsWindow<T>::StatsWindow(const int windowSize)
:maxRingIdx_(-1), windowSize_(windowSize), elements_(windowSize) {
}

template <typename T>
void StatsWindow<T>::mapMultiply(const T val) {
  for (size_t i = 0; i < windowSize_; i++) {
    elements_[i] *= val;
  }
}

template <typename T>
void StatsWindow<T>::mapDivide(const T val) {
  for (size_t i = 0; i < windowSize_; i++) {
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

#endif
