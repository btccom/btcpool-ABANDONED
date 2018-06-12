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
#include <glog/logging.h>

#include "gtest/gtest.h"
#include "Common.h"
#include "Utils.h"

TEST(Utils, Strings_Format) {
  for (int i = 1; i < 1024; i++) {
    string s;
    for (int j = 0; j < i; j++) {
      s += "A";
    }
    string s1 = Strings::Format("%s", s.c_str());
    ASSERT_EQ(s1, s);
  }
}

TEST(Utils, Strings_Append) {
  for (int i = 1; i < 1024; i++) {
    string s;
    for (int j = 0; j < i; j++) {
      s += "A";
    }
    string s1;
    Strings::Append(s1, "%s", s.c_str());
    ASSERT_EQ(s1, s);
  }
}

TEST(Utils, date) {
  // 1418051254 -> 2014-12-08 15:07:34 GMT
  ASSERT_EQ(date("%F %T", 1418051254), "2014-12-08 15:07:34");
}

TEST(Utils, share2HashrateG) {
  double h;

  // if minser's hashrate is 15 Ehs, the diff will be around 32,000,000,000.
  // if diff = 32,000,000,000, every 10 secons per share,
  // hashrate will 1.37439e+10 Ghs ~ 13743895347 Ghs
  h = share2HashrateG(32000000000, 10);
  ASSERT_EQ((int64_t)h, 13743895347);

  // if diff = 1, every 10 secons per share,
  // hashrate will 0.429497 Ghs ~ 429 Mhs
  h = share2HashrateG(1, 10);
  ASSERT_EQ((int64_t)(h*1000), 429);
}

TEST(Utils, Bin2Hex)
{
    const vector<char> bin = {(char)0xF0, (char)0xFA, (char)0x6E, (char)0xCD, (char)0xCD, (char)0xCD, (char)0xCD, (char)0xCD};
    const string rightHex = "f0fa6ecdcdcdcdcd";
    const string rightHexReverse = "cdcdcdcdcd6efaf0";

    string result1;
    Bin2Hex(bin, result1);
    EXPECT_EQ(result1, rightHex);
    EXPECT_NE(result1, rightHexReverse);

    string result2;
    Bin2HexR(bin, result2);
    EXPECT_EQ(result2, rightHexReverse);
    EXPECT_NE(result2, rightHex);


}