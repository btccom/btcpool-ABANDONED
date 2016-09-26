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
#include "Utils.h"


TEST(Common, score2Str) {
  // 10e-25
  ASSERT_EQ(score2Str(0.0000000000000000000000001), "0.0000000000000000000000001");
  ASSERT_EQ(score2Str(0.0000000000000000000000009), "0.0000000000000000000000009");
  ASSERT_EQ(score2Str(0.000000000000000000000001),  "0.0000000000000000000000010");
  ASSERT_EQ(score2Str(0.00000000000000000000001),   "0.0000000000000000000000100");
  ASSERT_EQ(score2Str(0.0000000000000000000001),    "0.0000000000000000000001000");
  ASSERT_EQ(score2Str(0.000000000000000000001), "0.0000000000000000000010000");
  ASSERT_EQ(score2Str(0.00000000000000000001),  "0.0000000000000000000100000");
  ASSERT_EQ(score2Str(0.0000000000000000001),   "0.0000000000000000001000000");
  ASSERT_EQ(score2Str(0.000000000000000001),    "0.0000000000000000010000000");
  ASSERT_EQ(score2Str(0.00000000000000001), "0.0000000000000000100000000");
  ASSERT_EQ(score2Str(0.0000000000000001),  "0.0000000000000001000000000");
  ASSERT_EQ(score2Str(0.000000000000001),   "0.0000000000000010000000000");
  ASSERT_EQ(score2Str(0.00000000000001),    "0.0000000000000100000000000");
  ASSERT_EQ(score2Str(0.0000000000001), "0.0000000000001000000000000");
  ASSERT_EQ(score2Str(0.000000000001),  "0.0000000000010000000000000");
  ASSERT_EQ(score2Str(0.00000000001),   "0.0000000000100000000000000");

  ASSERT_EQ(score2Str(0.0000000001),    "0.000000000100000000000000");
  ASSERT_EQ(score2Str(0.000000001), "0.00000000100000000000000");
  ASSERT_EQ(score2Str(0.00000001),  "0.0000000100000000000000");
  ASSERT_EQ(score2Str(0.0000001),   "0.000000100000000000000");
  ASSERT_EQ(score2Str(0.000001),    "0.00000100000000000000");
  ASSERT_EQ(score2Str(0.00001), "0.0000100000000000000");
  ASSERT_EQ(score2Str(0.0001),  "0.000100000000000000");
  ASSERT_EQ(score2Str(0.001),   "0.00100000000000000");
  ASSERT_EQ(score2Str(0.01),    "0.0100000000000000");
  ASSERT_EQ(score2Str(0.1), "0.100000000000000");
  ASSERT_EQ(score2Str(1.0), "1.00000000000000");
  ASSERT_EQ(score2Str(10.0),    "10.0000000000000");
  ASSERT_EQ(score2Str(100.0),   "100.000000000000");
  ASSERT_EQ(score2Str(1000.0),  "1000.00000000000");
  ASSERT_EQ(score2Str(10000.0), "10000.0000000000");
  ASSERT_EQ(score2Str(100000.0),    "100000.000000000");
  ASSERT_EQ(score2Str(1000000.0),   "1000000.00000000");
  ASSERT_EQ(score2Str(10000000.0),  "10000000.0000000");
  ASSERT_EQ(score2Str(100000000.0), "100000000.000000");

  ASSERT_EQ(score2Str(123412345678.0), "123412345678.00");
  ASSERT_EQ(score2Str(1234.12345678123), "1234.1234567812");
}

TEST(Common, BitsToTarget) {
  uint32 bits;
  uint256 target;

  bits = 0x1b0404cb;
  BitsToTarget(bits, target);
  ASSERT_EQ(target, uint256S("00000000000404CB000000000000000000000000000000000000000000000000"));
}

TEST(Common, TargetToBdiff) {
  // 0x00000000FFFF0000000000000000000000000000000000000000000000000000 /
  // 0x00000000000404CB000000000000000000000000000000000000000000000000
  // = 16307.420938523983 (bdiff)
  ASSERT_EQ(TargetToBdiff("0x00000000000404CB000000000000000000000000000000000000000000000000"), 16307);
}


TEST(Common, TargetToPdiff) {
  // 0x00000000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF /
  // 0x00000000000404CB000000000000000000000000000000000000000000000000
  // = 16307.669773817162 (pdiff)
  ASSERT_EQ(TargetToBdiff("0x00000000000404CB000000000000000000000000000000000000000000000000"), 16307);

//  uint256 t;
//  DiffToTarget(pow(2, 0), t);
//  printf("%s\n", t.ToString().c_str());
//  DiffToTarget(pow(2, 1), t);
//  printf("%s\n", t.ToString().c_str());
//  DiffToTarget(pow(2, 2), t);
//  printf("%s\n", t.ToString().c_str());
//  DiffToTarget(pow(2, 3), t);
//  printf("%s\n", t.ToString().c_str());
//  DiffToTarget(pow(2, 4), t);
//  printf("%s\n", t.ToString().c_str());
//  DiffToTarget(pow(2, 5), t);
//  printf("%s\n", t.ToString().c_str());
//  DiffToTarget(pow(2, 6), t);
//  printf("%s\n", t.ToString().c_str());
//  DiffToTarget(pow(2, 7), t);
//  printf("%s\n", t.ToString().c_str());
//  DiffToTarget(pow(2, 8), t);
//  printf("%s\n", t.ToString().c_str());
//  DiffToTarget(pow(2, 9), t);
//  printf("%s\n", t.ToString().c_str());
//
//  // 1024
//  DiffToTarget(pow(2, 10), t);
//  printf("%s\n", t.ToString().c_str());
//  DiffToTarget(pow(2, 11), t);
//  printf("%s\n", t.ToString().c_str());
//  DiffToTarget(pow(2, 12), t);
//  printf("%s\n", t.ToString().c_str());
//  DiffToTarget(pow(2, 13), t);
//  printf("%s\n", t.ToString().c_str());
//  DiffToTarget(pow(2, 14), t);
//  printf("%s\n", t.ToString().c_str());
//  DiffToTarget(pow(2, 15), t);
//  printf("%s\n", t.ToString().c_str());
//  DiffToTarget(pow(2, 16), t);
//  printf("%s\n", t.ToString().c_str());
//  DiffToTarget(pow(2, 17), t);
//  printf("%s\n", t.ToString().c_str());
//  DiffToTarget(pow(2, 18), t);
//  printf("%s\n", t.ToString().c_str());
//  DiffToTarget(pow(2, 19), t);
//  printf("%s\n", t.ToString().c_str());
//
//  // 1,048,576
//  DiffToTarget(pow(2, 20), t);
//  printf("%s\n", t.ToString().c_str());
//  DiffToTarget(pow(2, 21), t);
//  printf("%s\n", t.ToString().c_str());
//  DiffToTarget(pow(2, 22), t);
//  printf("%s\n", t.ToString().c_str());
//  DiffToTarget(pow(2, 23), t);
//  printf("%s\n", t.ToString().c_str());
//  DiffToTarget(pow(2, 24), t);
//  printf("%s\n", t.ToString().c_str());
//  DiffToTarget(pow(2, 25), t);
//  printf("%s\n", t.ToString().c_str());
//  DiffToTarget(pow(2, 26), t);
//  printf("%s\n", t.ToString().c_str());
//  DiffToTarget(pow(2, 27), t);
//  printf("%s\n", t.ToString().c_str());
//  DiffToTarget(pow(2, 28), t);
//  printf("%s\n", t.ToString().c_str());
//  DiffToTarget(pow(2, 29), t);
//  printf("%s\n", t.ToString().c_str());
//
//  // 1,073,741,824
//  DiffToTarget(pow(2, 30), t);
//  printf("%s\n", t.ToString().c_str());
//  DiffToTarget(pow(2, 31), t);
//  printf("%s\n", t.ToString().c_str());
//  DiffToTarget(pow(2, 32), t);
//  printf("%s\n", t.ToString().c_str());
//  DiffToTarget(pow(2, 33), t);
//  printf("%s\n", t.ToString().c_str());
//  DiffToTarget(pow(2, 34), t);
//  printf("%s\n", t.ToString().c_str());
//  DiffToTarget(pow(2, 35), t);
//  printf("%s\n", t.ToString().c_str());
//  DiffToTarget(pow(2, 36), t);
//  printf("%s\n", t.ToString().c_str());
//  DiffToTarget(pow(2, 37), t);
//  printf("%s\n", t.ToString().c_str());
//  DiffToTarget(pow(2, 38), t);
//  printf("%s\n", t.ToString().c_str());
//  DiffToTarget(pow(2, 39), t);
//  printf("%s\n", t.ToString().c_str());
//
//  // 1,099,511,627,776
//  DiffToTarget(pow(2, 40), t);
//  printf("%s\n", t.ToString().c_str());
//  DiffToTarget(pow(2, 41), t);
//  printf("%s\n", t.ToString().c_str());
//  DiffToTarget(pow(2, 42), t);
//  printf("%s\n", t.ToString().c_str());
//  DiffToTarget(pow(2, 43), t);
//  printf("%s\n", t.ToString().c_str());
//  DiffToTarget(pow(2, 44), t);
//  printf("%s\n", t.ToString().c_str());
//  DiffToTarget(pow(2, 45), t);
//  printf("%s\n", t.ToString().c_str());
//  DiffToTarget(pow(2, 46), t);
//  printf("%s\n", t.ToString().c_str());
//  DiffToTarget(pow(2, 47), t);
//  printf("%s\n", t.ToString().c_str());
//  DiffToTarget(pow(2, 48), t);
//  printf("%s\n", t.ToString().c_str());
//  DiffToTarget(pow(2, 49), t);
//  printf("%s\n", t.ToString().c_str());
//
//  DiffToTarget(pow(2, 50), t);
//  printf("%s\n", t.ToString().c_str());
//  DiffToTarget(pow(2, 51), t);
//  printf("%s\n", t.ToString().c_str());
//  DiffToTarget(pow(2, 52), t);
//  printf("%s\n", t.ToString().c_str());
//  DiffToTarget(pow(2, 53), t);
//  printf("%s\n", t.ToString().c_str());
//  DiffToTarget(pow(2, 54), t);
//  printf("%s\n", t.ToString().c_str());
//  DiffToTarget(pow(2, 55), t);
//  printf("%s\n", t.ToString().c_str());
//  DiffToTarget(pow(2, 56), t);
//  printf("%s\n", t.ToString().c_str());
//  DiffToTarget(pow(2, 57), t);
//  printf("%s\n", t.ToString().c_str());
//  DiffToTarget(pow(2, 58), t);
//  printf("%s\n", t.ToString().c_str());
//  DiffToTarget(pow(2, 59), t);
//  printf("%s\n", t.ToString().c_str());
//
//  DiffToTarget(pow(2, 60), t);
//  printf("%s\n", t.ToString().c_str());
//  DiffToTarget(pow(2, 61), t);
//  printf("%s\n", t.ToString().c_str());
//  DiffToTarget(pow(2, 62), t);
//  printf("%s\n", t.ToString().c_str());
//  DiffToTarget(pow(2, 63), t);
//  printf("%s\n", t.ToString().c_str());
}

TEST(Common, BitsToDifficulty) {
  // 0x1b0404cb: https://en.bitcoin.it/wiki/Difficulty
  double d;
  BitsToDifficulty(0x1b0404cbu, &d);  // diff = 16307.420939
  ASSERT_EQ((uint64_t)(d * 10000.0), 163074209ull);
}

