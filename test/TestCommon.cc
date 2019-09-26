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

#include "Utils.h"

#include "bitcoin/CommonBitcoin.h"
#include "eth/CommonEth.h"
#include "bytom/CommonBytom.h"

#include <uint256.h>
#include <arith_uint256.h>

TEST(Common, score2Str) {
  // 10e-25
  ASSERT_EQ(
      score2Str(0.0000000000000000000000001), "0.0000000000000000000000001");
  ASSERT_EQ(
      score2Str(0.0000000000000000000000009), "0.0000000000000000000000009");
  ASSERT_EQ(
      score2Str(0.000000000000000000000001), "0.0000000000000000000000010");
  ASSERT_EQ(
      score2Str(0.00000000000000000000001), "0.0000000000000000000000100");
  ASSERT_EQ(score2Str(0.0000000000000000000001), "0.0000000000000000000001000");
  ASSERT_EQ(score2Str(0.000000000000000000001), "0.0000000000000000000010000");
  ASSERT_EQ(score2Str(0.00000000000000000001), "0.0000000000000000000100000");
  ASSERT_EQ(score2Str(0.0000000000000000001), "0.0000000000000000001000000");
  ASSERT_EQ(score2Str(0.000000000000000001), "0.0000000000000000010000000");
  ASSERT_EQ(score2Str(0.00000000000000001), "0.0000000000000000100000000");
  ASSERT_EQ(score2Str(0.0000000000000001), "0.0000000000000001000000000");
  ASSERT_EQ(score2Str(0.000000000000001), "0.0000000000000010000000000");
  ASSERT_EQ(score2Str(0.00000000000001), "0.0000000000000100000000000");
  ASSERT_EQ(score2Str(0.0000000000001), "0.0000000000001000000000000");
  ASSERT_EQ(score2Str(0.000000000001), "0.0000000000010000000000000");
  ASSERT_EQ(score2Str(0.00000000001), "0.0000000000100000000000000");

  ASSERT_EQ(score2Str(0.0000000001), "0.000000000100000000000000");
  ASSERT_EQ(score2Str(0.000000001), "0.00000000100000000000000");
  ASSERT_EQ(score2Str(0.00000001), "0.0000000100000000000000");
  ASSERT_EQ(score2Str(0.0000001), "0.000000100000000000000");
  ASSERT_EQ(score2Str(0.000001), "0.00000100000000000000");
  ASSERT_EQ(score2Str(0.00001), "0.0000100000000000000");
  ASSERT_EQ(score2Str(0.0001), "0.000100000000000000");
  ASSERT_EQ(score2Str(0.001), "0.00100000000000000");
  ASSERT_EQ(score2Str(0.01), "0.0100000000000000");
  ASSERT_EQ(score2Str(0.1), "0.100000000000000");
  ASSERT_EQ(score2Str(1.0), "1.00000000000000");
  ASSERT_EQ(score2Str(10.0), "10.0000000000000");
  ASSERT_EQ(score2Str(100.0), "100.000000000000");
  ASSERT_EQ(score2Str(1000.0), "1000.00000000000");
  ASSERT_EQ(score2Str(10000.0), "10000.0000000000");
  ASSERT_EQ(score2Str(100000.0), "100000.000000000");
  ASSERT_EQ(score2Str(1000000.0), "1000000.00000000");
  ASSERT_EQ(score2Str(10000000.0), "10000000.0000000");
  ASSERT_EQ(score2Str(100000000.0), "100000000.000000");

  ASSERT_EQ(score2Str(123412345678.0), "123412345678.00");
  ASSERT_EQ(score2Str(1234.12345678123), "1234.1234567812");
}

TEST(Common, BitsToTarget) {
  uint32_t bits;
  uint256 target;

  bits = 0x1b0404cb;
  BitsToTarget(bits, target);
  ASSERT_EQ(
      target,
      uint256S(
          "00000000000404CB000000000000000000000000000000000000000000000000"));
}

static void TestDiffToTarget(uint64_t diff, string target) {
  uint256 targetWithoutTable, targetWithTable;
  BitcoinDifficulty::DiffToTarget(diff, targetWithoutTable, false);
  BitcoinDifficulty::DiffToTarget(diff, targetWithTable, true);
  ASSERT_EQ(targetWithoutTable.ToString(), target);
  ASSERT_EQ(targetWithTable.ToString(), target);
}

#ifdef CHAIN_TYPE_LTC
TEST(Common, DiffToTargetLitecoin) {
  TestDiffToTarget(
      1ull, "0000ffff00000000000000000000000000000000000000000000000000000000");
  TestDiffToTarget(
      2ull, "00007fff80000000000000000000000000000000000000000000000000000000");
  TestDiffToTarget(
      3ull, "0000555500000000000000000000000000000000000000000000000000000000");
  TestDiffToTarget(
      4ull, "00003fffc0000000000000000000000000000000000000000000000000000000");
  TestDiffToTarget(
      1073741831ull,
      "000000000003fffbff9000700c3ff3bea901572583da77e5941ae2e3cd0f2f15");
  TestDiffToTarget(
      1ull << 10,
      "0000003fffc00000000000000000000000000000000000000000000000000000");
  TestDiffToTarget(
      1ull << 20,
      "000000000ffff000000000000000000000000000000000000000000000000000");
  TestDiffToTarget(
      1ull << 30,
      "000000000003fffc000000000000000000000000000000000000000000000000");
  TestDiffToTarget(
      1ull << 63,
      "00000000000000000001fffe0000000000000000000000000000000000000000");
}
#elif defined(CHAIN_TYPE_ZEC)
TEST(Common, DiffToTargetZCash) {
  TestDiffToTarget(
      1ull, "0007ffff00000000000000000000000000000000000000000000000000000000");
  TestDiffToTarget(
      2ull, "0003ffff80000000000000000000000000000000000000000000000000000000");
  TestDiffToTarget(
      3ull, "0002aaaa55555555555555555555555555555555555555555555555555555555");
  TestDiffToTarget(
      4ull, "0001ffffc0000000000000000000000000000000000000000000000000000000");
  TestDiffToTarget(
      1073741831ull,
      "00000000001ffffbfc80007061fff3b54801582c1fda5b2c841e07218cb73854");
  TestDiffToTarget(
      1ull << 10,
      "000001ffffc00000000000000000000000000000000000000000000000000000");
  TestDiffToTarget(
      1ull << 20,
      "000000007ffff000000000000000000000000000000000000000000000000000");
  TestDiffToTarget(
      1ull << 30,
      "00000000001ffffc000000000000000000000000000000000000000000000000");
  TestDiffToTarget(
      1ull << 63,
      "0000000000000000000ffffe0000000000000000000000000000000000000000");
}
#else
TEST(Common, DiffToTargetBitcoin) {
  TestDiffToTarget(
      1ull, "00000000ffff0000000000000000000000000000000000000000000000000000");
  TestDiffToTarget(
      2ull, "000000007fff8000000000000000000000000000000000000000000000000000");
  TestDiffToTarget(
      3ull, "0000000055550000000000000000000000000000000000000000000000000000");
  TestDiffToTarget(
      4ull, "000000003fffc000000000000000000000000000000000000000000000000000");
  TestDiffToTarget(
      1073741831ull,
      "0000000000000003fffbff9000700c3ff3bea901572583da77e5941ae2e3cd0f");
  TestDiffToTarget(
      1ull << 10,
      "00000000003fffc0000000000000000000000000000000000000000000000000");
  TestDiffToTarget(
      1ull << 20,
      "0000000000000ffff00000000000000000000000000000000000000000000000");
  TestDiffToTarget(
      1ull << 30,
      "0000000000000003fffc00000000000000000000000000000000000000000000");
  TestDiffToTarget(
      1ull << 63,
      "000000000000000000000001fffe000000000000000000000000000000000000");
}
#endif

TEST(Common, DiffToTargetTable) {
  uint256 t1, t2;

  for (uint64_t i = 0; i < 10240; i++) {
    BitcoinDifficulty::DiffToTarget(i, t1, false);
    BitcoinDifficulty::DiffToTarget(i, t2, true);
    ASSERT_EQ(t1, t2);
  }

  for (uint32_t i = 0; i < 64; i++) {
    uint64_t diff = 1 << i;
    BitcoinDifficulty::DiffToTarget(diff, t1, false);
    BitcoinDifficulty::DiffToTarget(diff, t2, true);
    ASSERT_EQ(t1, t2);
  }
}

TEST(Common, DiffTargetDiff) {
  for (uint32_t i = 0; i < 64; i++) {
    uint64_t diff = 1 << i;
    uint256 target;
    BitcoinDifficulty::DiffToTarget(diff, target);
    ASSERT_EQ(diff, BitcoinDifficulty::TargetToDiff(target));
  }
}

TEST(Common, uint256) {
  uint256 u1, u2;

  u1 = uint256S(
      "00000000000000000392381eb1be66cd8ef9e2143a0e13488875b3e1649a3dc9");
  u2 = uint256S(
      "00000000000000000392381eb1be66cd8ef9e2143a0e13488875b3e1649a3dc9");
  ASSERT_EQ(UintToArith256(u1) == UintToArith256(u2), true);
  ASSERT_EQ(UintToArith256(u1) >= UintToArith256(u2), true);
  ASSERT_EQ(UintToArith256(u1) < UintToArith256(u2), false);

  u1 = uint256S(
      "00000000000000000392381eb1be66cd8ef9e2143a0e13488875b3e1649a3dc9");
  u2 = uint256S(
      "000000000000000000cc35a4f0ebd7b5c8165b28d73e6369f49098c1a632d1a9");
  ASSERT_EQ(UintToArith256(u1) > UintToArith256(u2), true);
}

TEST(Common, TargetToDiff) {
#ifdef CHAIN_TYPE_LTC
  uint64_t diff = 1068723138ULL;
#elif defined(CHAIN_TYPE_ZEC)
  uint64_t diff = 8549899262ULL;
#else
  // 0x00000000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF /
  // 0x00000000000404CB000000000000000000000000000000000000000000000000
  // = 16307.669773817162 (pdiff)
  uint64_t diff = 16307ULL;
#endif

  ASSERT_EQ(
      BitcoinDifficulty::TargetToDiff(
          "0x00000000000404CB000000000000000000000000000000000000000000000000"),
      diff);
}

TEST(Common, BitsToDifficulty) {
#ifdef CHAIN_TYPE_LTC
  uint64_t diff = 10687231386271ull;
#elif defined(CHAIN_TYPE_ZEC)
  uint64_t diff = 85498992627052ull;
#else
  uint64_t diff = 163074209ull;
#endif

  // 0x1b0404cb: https://en.bitcoin.it/wiki/Difficulty
  double d;
  BitcoinDifficulty::BitsToDifficulty(0x1b0404cbu, &d); // diff = 16307.420939
  ASSERT_EQ((uint64_t)(d * 10000.0), diff);
}

TEST(Common, formatDifficulty) {
  ASSERT_EQ(formatDifficulty(UINT64_MAX), 9223372036854775808ull);

  // 2^32 = UINT32_MAX + 1
  ASSERT_EQ(formatDifficulty(UINT32_MAX), (uint64_t)UINT32_MAX + 1);
  ASSERT_EQ(
      formatDifficulty((uint64_t)UINT32_MAX + 1), (uint64_t)UINT32_MAX + 1);

  ASSERT_EQ(formatDifficulty(0), 1ULL);
  ASSERT_EQ(formatDifficulty(1), 1ULL);
  ASSERT_EQ(formatDifficulty(2), 2ULL);
  ASSERT_EQ(formatDifficulty(3), 4ULL);
}

TEST(Common, Eth_TargetToDifficulty) {
  ASSERT_EQ(
      Eth_TargetToDifficulty(
          "0x000000029794e0c85b08583ac96ea15f8b6f4d6bbcd1ee76326cd948d541eac3"),
      0x62c2d313ull);
  ASSERT_EQ(
      Eth_TargetToDifficulty(
          "000000029794e0c85b08583ac96ea15f8b6f4d6bbcd1ee76326cd948d541eac3"),
      0x62c2d313ull);
}

TEST(Common, Eth_DifficultyToTarget) {
  ASSERT_EQ(
      Eth_DifficultyToTarget(0x62c2d313ull),
      "000000029794e0c85b08583ac96ea15f8b6f4d6bbcd1ee76326cd948d541eac3");
}

TEST(Common, Bytom_TargetCompactAndDifficulty) {
  {
    uint64_t targetCompact = Bytom_JobDifficultyToTargetCompact(1UL);
    std::cout << targetCompact << "\n";
    EXPECT_EQ(1UL, Bytom_TargetCompactToDifficulty(targetCompact));
  }

  {
    uint64_t targetCompact = Bytom_JobDifficultyToTargetCompact(10UL);
    std::cout << targetCompact << "\n";
    EXPECT_EQ(10UL, Bytom_TargetCompactToDifficulty(targetCompact));
  }

  {
    uint64_t targetCompact = Bytom_JobDifficultyToTargetCompact(20UL);
    std::cout << targetCompact << "\n";
    EXPECT_EQ(20UL, Bytom_TargetCompactToDifficulty(targetCompact));
  }

  {
    uint64_t targetCompact = 2305843009214532812UL;
    uint64_t diff = Bytom_TargetCompactToDifficulty(targetCompact);
    EXPECT_EQ(20UL, diff);
  }
}
