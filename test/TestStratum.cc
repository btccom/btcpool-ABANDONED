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
#include "Stratum.h"

#include <stdint.h>

TEST(Stratum, jobId2Time) {
  uint64_t jobId;

  // jobId:   timestamp    +    gbtHash
  //          ----------      ----------
  //           uint32_t        uint32_t
  //  const string jobIdStr = Strings::Format("%08x%s", (uint32_t)time(nullptr),
  //                                          gbtHash.ToString().substr(0, 8).c_str());
  jobId = (1469002809ull << 32) | 0x00000000FFFFFFFFull;
  ASSERT_EQ(jobId2Time(jobId), 1469002809u);

  jobId = (1469002809ull << 32) | 0x0000000000000000ull;
  ASSERT_EQ(jobId2Time(jobId), 1469002809u);
}

TEST(Stratum, Share) {
  Share s;

  ASSERT_EQ(s.isValid(), false);
  ASSERT_EQ(s.score(), 0);
  ASSERT_EQ(s.toString(), "share(jobId: 0, ip: 0.0.0.0, userId: 0, workerId: 0,"
            " timeStamp: 0/1970-01-01 00:00:00, share: 0, blkBits: 00000000, result: 0)");

  s.ip_ = htonl(167772161);  // 167772161 : 10.0.0.1
  ASSERT_EQ(s.toString(), "share(jobId: 0, ip: 10.0.0.1, userId: 0, workerId: 0,"
            " timeStamp: 0/1970-01-01 00:00:00, share: 0, blkBits: 00000000, result: 0)");
}

TEST(Stratum, StratumWorker) {
  StratumWorker w;
  uint64_t u;
  int64_t workerId;

  ASSERT_EQ(w.getUserName("abcd"), "abcd");
  ASSERT_EQ(w.getUserName("abcdabcdabcdabcdabcdabcdabcd"), "abcdabcdabcdabcdabcdabcdabcd");
  ASSERT_EQ(w.getUserName("abcd."), "abcd");
  ASSERT_EQ(w.getUserName("abcd.123"), "abcd");
  ASSERT_EQ(w.getUserName("abcd.123.456"), "abcd");

  //
  // echo -n '123' |openssl dgst -sha256 -binary |openssl dgst -sha256
  //
  w.setUserIDAndNames(INT32_MAX, "abcd.123");
  ASSERT_EQ(w.fullName_,   "abcd.123");
  ASSERT_EQ(w.userId_,     INT32_MAX);
  ASSERT_EQ(w.userName_,   "abcd");
  ASSERT_EQ(w.workerName_, "123");
  // '123' dsha256 : 5a77d1e9612d350b3734f6282259b7ff0a3f87d62cfef5f35e91a5604c0490a3
  //       uint256 : a390044c60a5915ef3f5fe2cd6873f0affb7592228f634370b352d61e9d1775a
  u = strtoull("a390044c60a5915e", nullptr, 16);
  memcpy((uint8_t *)&workerId, (uint8_t *)&u, 8);
  ASSERT_EQ(w.workerHashId_, workerId);


  w.setUserIDAndNames(0, "abcdefg");
  ASSERT_EQ(w.fullName_,   "abcdefg.default");
  ASSERT_EQ(w.userId_,     0);
  ASSERT_EQ(w.userName_,   "abcdefg");
  ASSERT_EQ(w.workerName_, "default");
  // 'default' dsha256 : 141ab30bfb6e9e5c81b810cf65b30c0cfea550fcb90452cdce13b634927a0fbf
  //           uint256 : bf0f7a9234b613ce....
  u = strtoull("bf0f7a9234b613ce", nullptr, 16);
  memcpy((uint8_t *)&workerId, (uint8_t *)&u, 8);
  ASSERT_EQ(w.workerHashId_, workerId);

  // check allow chars
  w.setUserIDAndNames(0, "abcdefg.azAZ09-._:|^/");
  ASSERT_EQ(w.workerName_, "azAZ09-._:|^/");
  ASSERT_EQ(w.fullName_,   "abcdefg.azAZ09-._:|^/");

  // some of them are bad chars
  w.setUserIDAndNames(0, "abcdefg.~!@#$%^&*()+={}|[]\\<>?,./");
  ASSERT_EQ(w.workerName_, "^|./");
  ASSERT_EQ(w.fullName_,   "abcdefg.^|./");

  // all bad chars
  w.setUserIDAndNames(0, "abcdefg.~!@#$%&*()+={}[]\\<>?,");
  ASSERT_EQ(w.workerName_, "default");
  ASSERT_EQ(w.fullName_,   "abcdefg.default");
}


TEST(Stratum, StratumJob) {

}
