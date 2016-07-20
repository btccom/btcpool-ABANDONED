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

#include "gtest/gtest.h"
#include "Common.h"
#include "Statistics.h"

TEST(StatsWindow, clear) {
  int windowSize = 60;

  for (int j = 0; j < 10; j++) {
    StatsWindow<int64> sw(windowSize);
    ASSERT_EQ(sw.sum(windowSize - 1, windowSize), 0);

    int64 val = 3;
    for (int i = 0; i < windowSize; i++) {
      sw.insert(i, val);
    }
    ASSERT_EQ(sw.sum(windowSize - 1, windowSize), windowSize * val);

    sw.clear();
    ASSERT_EQ(sw.sum(windowSize - 1, windowSize), 0);
  }
}


TEST(StatsWindow, sum01) {
  int windowSize = 60;
  StatsWindow<int64> sw(windowSize);
  int64 val = 5;

  for (int i = 0; i < windowSize; i++) {
    sw.insert(i, val);
  }

  for (int i = 0; i < windowSize; i++) {
    ASSERT_EQ(sw.sum(i, 1), val);
  }
  for (int i = 0; i < windowSize; i++) {
    ASSERT_EQ(sw.sum(windowSize - 1, i), i * val);
  }

  for (int i = windowSize; i < windowSize*2; i++) {
    ASSERT_EQ(sw.sum(i, 1), 0);
  }
  for (int i = windowSize; i < windowSize*2; i++) {
    ASSERT_EQ(sw.sum(i, windowSize), (windowSize - (i % windowSize + 1)) * val);
  }

  for (int i = windowSize*2; i < windowSize*3; i++) {
    ASSERT_EQ(sw.sum(i, windowSize), 0);
  }
}

TEST(StatsWindow, sum02) {
  int windowSize = 60;
  StatsWindow<int64> sw(windowSize);
  int64 val = 5;

  for (int i = windowSize - 1; i >= 0; i--) {
    sw.insert(i, val);
  }

  for (int i = 0; i < windowSize; i++) {
    ASSERT_EQ(sw.sum(i, 1), val);
  }
  for (int i = 0; i < windowSize; i++) {
    ASSERT_EQ(sw.sum(windowSize - 1, i), i * val);
  }

  for (int i = windowSize; i < windowSize*2; i++) {
    ASSERT_EQ(sw.sum(i, 1), 0);
  }
  for (int i = windowSize; i < windowSize*2; i++) {
    ASSERT_EQ(sw.sum(i, windowSize), (windowSize - (i % windowSize + 1)) * val);
  }

  for (int i = windowSize*2; i < windowSize*3; i++) {
    ASSERT_EQ(sw.sum(i, windowSize), 0);
  }
}


TEST(StatsWindow, sum03) {
  StatsWindow<int64> sw(5);
  sw.insert(0, 1);
  ASSERT_EQ(sw.sum(0, 1), 1);
  sw.clear();
  ASSERT_EQ(sw.sum(0, 1), 0);

  sw.insert(0, 1);
  sw.insert(5, 5);
  ASSERT_EQ(sw.sum(5, 1), 5);
  ASSERT_EQ(sw.sum(5, 5), 5);
  sw.clear();

  sw.insert(0, 1);
  sw.insert(1, 2);
  sw.insert(2, 3);
  sw.insert(3, 4);
  sw.insert(4, 5);
  ASSERT_EQ(sw.sum(4, 1), 5);
  ASSERT_EQ(sw.sum(4, 2), 9);
  ASSERT_EQ(sw.sum(4, 3), 12);
  ASSERT_EQ(sw.sum(4, 4), 14);
  ASSERT_EQ(sw.sum(4, 5), 15);

  sw.insert(8, 9);
  ASSERT_EQ(sw.sum(8, 5), 14);
  sw.insert(7, 8);
  ASSERT_EQ(sw.sum(8, 5), 22);
  sw.insert(6, 7);
  ASSERT_EQ(sw.sum(8, 5), 29);
  sw.insert(5, 6);
  ASSERT_EQ(sw.sum(8, 5), 35);
}


TEST(StatsWindow, map) {
  int windowSize = 10;
  StatsWindow<int64> sw(windowSize);
  for (int i = 0; i < windowSize; i++) {
    sw.insert(i, i * 2);
  }
  ASSERT_EQ(sw.sum(windowSize-1), sw.sum(windowSize-1, windowSize));

  int64 sum = sw.sum(windowSize-1, windowSize);
  sw.mapDivide(2);
  int64 sum2 = sw.sum(windowSize-1, windowSize);
  ASSERT_EQ(sum/2, sum2);

  sw.mapMultiply(2);
  int64 sum3 = sw.sum(windowSize-1, windowSize);
  ASSERT_EQ(sum, sum3);
}
