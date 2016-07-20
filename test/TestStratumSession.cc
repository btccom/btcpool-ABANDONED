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

#include "StratumSession.h"

TEST(StratumSession, LocalShare) {
  StratumSession::LocalShare ls1(0xFFFFFFFFFFFFFFFFULL,
                                 0xFFFFFFFFU, 0xFFFFFFFFU);

  {
    StratumSession::LocalShare ls2(0xFFFFFFFFFFFFFFFEULL,
                                   0xFFFFFFFFU, 0xFFFFFFFFU);
    ASSERT_EQ(ls2 < ls1, true);
  }
  {
    StratumSession::LocalShare ls2(0xFFFFFFFFFFFFFFFFULL,
                                   0xFFFFFFFEU, 0xFFFFFFFFU);
    ASSERT_EQ(ls2 < ls1, true);
  }
  {
    StratumSession::LocalShare ls2(0xFFFFFFFFFFFFFFFFULL,
                                   0xFFFFFFFFU, 0xFFFFFFFEU);
    ASSERT_EQ(ls2 < ls1, true);
  }
  {
    StratumSession::LocalShare ls2(0xFFFFFFFFFFFFFFFFULL,
                                   0xFFFFFFFFU, 0xFFFFFFFFU);
    ASSERT_EQ(ls2 < ls1, false);
    ASSERT_EQ(ls2 < ls2, false);
  }
  {
    StratumSession::LocalShare ls2(0x0ULL, 0x0U, 0x0U);
    ls2 = ls1;
    ASSERT_EQ(ls2 < ls1, false);
    ASSERT_EQ(ls2 < ls2, false);
  }
}


TEST(StratumSession, LocalJob) {
  StratumSession::LocalJob lj;

  {
    StratumSession::LocalShare ls1(0xFFFFFFFFFFFFFFFFULL,
                                   0xFFFFFFFFU, 0xFFFFFFFFU);
    ASSERT_EQ(lj.addLocalShare(ls1), true);
  }
  {
    StratumSession::LocalShare ls1(0xFFFFFFFFFFFFFFFFULL,
                                   0xFFFFFFFFU, 0xFFFFFFFFU);
    ASSERT_EQ(lj.addLocalShare(ls1), false);
  }
  {
    StratumSession::LocalShare ls2(0x0ULL, 0x0U, 0x0U);
    ASSERT_EQ(lj.addLocalShare(ls2), true);
  }
  {
    StratumSession::LocalShare ls2(0x0ULL, 0x0U, 0x0U);
    ASSERT_EQ(lj.addLocalShare(ls2), false);
  }
}

