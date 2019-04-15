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

#include "bitcoin/StratumBitcoin.h"
#include "bitcoin/StatisticsBitcoin.h"
#include "bitcoin/BitcoinUtils.h"

#include "eth/StratumEth.h"
#include "eth/StatisticsEth.h"

////////////////////////////////  StatsWindow  /////////////////////////////////
TEST(StatsWindow, clear) {
  int windowSize = 60;

  for (int j = 0; j < 10; j++) {
    StatsWindow<int64_t> sw(windowSize);
    ASSERT_EQ(sw.sum(windowSize - 1, windowSize), 0);

    int64_t val = 3;
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
  StatsWindow<int64_t> sw(windowSize);
  int64_t val = 5;

  for (int i = 0; i < windowSize; i++) {
    sw.insert(i, val);
  }

  for (int i = 0; i < windowSize; i++) {
    ASSERT_EQ(sw.sum(i, 1), val);
  }
  for (int i = 0; i < windowSize; i++) {
    ASSERT_EQ(sw.sum(windowSize - 1, i), i * val);
  }

  for (int i = windowSize; i < windowSize * 2; i++) {
    ASSERT_EQ(sw.sum(i, 1), 0);
  }
  for (int i = windowSize; i < windowSize * 2; i++) {
    ASSERT_EQ(sw.sum(i, windowSize), (windowSize - (i % windowSize + 1)) * val);
  }

  for (int i = windowSize * 2; i < windowSize * 3; i++) {
    ASSERT_EQ(sw.sum(i, windowSize), 0);
  }
}

TEST(StatsWindow, sum02) {
  int windowSize = 60;
  StatsWindow<int64_t> sw(windowSize);
  int64_t val = 5;

  for (int i = windowSize - 1; i >= 0; i--) {
    sw.insert(i, val);
  }

  for (int i = 0; i < windowSize; i++) {
    ASSERT_EQ(sw.sum(i, 1), val);
  }
  for (int i = 0; i < windowSize; i++) {
    ASSERT_EQ(sw.sum(windowSize - 1, i), i * val);
  }

  for (int i = windowSize; i < windowSize * 2; i++) {
    ASSERT_EQ(sw.sum(i, 1), 0);
  }
  for (int i = windowSize; i < windowSize * 2; i++) {
    ASSERT_EQ(sw.sum(i, windowSize), (windowSize - (i % windowSize + 1)) * val);
  }

  for (int i = windowSize * 2; i < windowSize * 3; i++) {
    ASSERT_EQ(sw.sum(i, windowSize), 0);
  }
}

TEST(StatsWindow, sum03) {
  StatsWindow<int64_t> sw(5);
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
  StatsWindow<int64_t> sw(windowSize);
  for (int i = 0; i < windowSize; i++) {
    sw.insert(i, i * 2);
  }
  ASSERT_EQ(sw.sum(windowSize - 1), sw.sum(windowSize - 1, windowSize));

  int64_t sum = sw.sum(windowSize - 1, windowSize);
  sw.mapDivide(2);
  int64_t sum2 = sw.sum(windowSize - 1, windowSize);
  ASSERT_EQ(sum / 2, sum2);

  sw.mapMultiply(2);
  int64_t sum3 = sw.sum(windowSize - 1, windowSize);
  ASSERT_EQ(sum, sum3);
}

////////////////////////////////  ShareStatsDay  ///////////////////////////////
TEST(ShareStatsDay, ShareStatsDay) {
  // using mainnet
  SelectParams(CBaseChainParams::MAIN);

  // 1
  {
    ShareStatsDay<ShareBitcoin> stats;
    ShareBitcoin share;

    share.set_height(527259);
    share.set_status(StratumStatus::ACCEPT);
    uint64_t shareValue = 1ll;

    auto reward = GetBlockReward(share.height(), Params().GetConsensus());

    // share -> socre = 1 : 1
    // https://btc.com/000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f
    share.set_blkbits(BitcoinDifficulty::GetDiffOneBits());

    // accept
    for (uint32_t i = 0; i < 24; i++) { // hour idx range: [0, 23]
      share.set_sharediff(shareValue);
      stats.processShare(i, share, false);
    }

    // reject
    share.set_status(StratumStatus::REJECT_NO_REASON);
    for (uint32_t i = 0; i < 24; i++) {
      share.set_sharediff(shareValue);
      stats.processShare(i, share, false);
    }

    ShareStats ss;
    for (uint32_t i = 0; i < 24; i++) {
      stats.getShareStatsHour(i, &ss);
      ASSERT_EQ(ss.shareAccept_, shareValue);
      ASSERT_EQ(ss.shareReject_, shareValue);
      ASSERT_EQ(ss.earn_, 1 * reward);
    }
    stats.getShareStatsDay(&ss);
    ASSERT_EQ(ss.shareAccept_, shareValue * 24);
    ASSERT_EQ(ss.shareReject_, shareValue * 24);
    ASSERT_EQ(ss.earn_, 1 * 24 * reward);
  }

  // UINT32_MAX
  {
    ShareStatsDay<ShareBitcoin> stats;
    ShareBitcoin share;

    share.set_height(527259);
    share.set_status(StratumStatus::ACCEPT);
    uint64_t shareValue = UINT32_MAX;

    // share -> socre = UINT32_MAX : 0.0197582875516673
    // https://btc.com/00000000000000000015f613f161b431acc6bbcb34533d2ca47d3cde4ec58b76
    share.set_blkbits(0x18050edcu);

    // accept
    for (uint32_t i = 0; i < 24; i++) { // hour idx range: [0, 23]
      share.set_sharediff(shareValue);
      stats.processShare(i, share, false);
      //      LOG(INFO) << score2Str(share.score());
    }

    // reject
    share.set_status(StratumStatus::REJECT_NO_REASON);
    for (uint32_t i = 0; i < 24; i++) {
      share.set_sharediff(shareValue);
      stats.processShare(i, share, false);
    }

    ShareStats ss;
    for (uint32_t i = 0; i < 24; i++) {
      stats.getShareStatsHour(i, &ss);
      ASSERT_EQ(ss.shareAccept_, shareValue);
      ASSERT_EQ(ss.shareReject_, shareValue);

#ifdef CHAIN_TYPE_LTC
      ASSERT_EQ((uint64_t)ss.earn_, 1507UL);
#elif defined(CHAIN_TYPE_ZEC)
      ASSERT_EQ((uint64_t)ss.earn_, 37ULL);
#elif defined(CHAIN_TYPE_UBTC)
      ASSERT_EQ((uint64_t)ss.earn_, 1975828UL);
#else
      ASSERT_EQ((uint64_t)ss.earn_, 24697859UL);
#endif
    }
    stats.getShareStatsDay(&ss);
    ASSERT_EQ(ss.shareAccept_, shareValue * 24);
    ASSERT_EQ(ss.shareReject_, shareValue * 24);
#ifdef CHAIN_TYPE_LTC
    ASSERT_EQ((uint64_t)ss.earn_, 36178UL);
#elif defined(CHAIN_TYPE_ZEC)
    ASSERT_EQ((uint64_t)ss.earn_, 904UL);
#elif defined(CHAIN_TYPE_UBTC)
    ASSERT_EQ((uint64_t)ss.earn_, 47419890UL);
#else
    ASSERT_EQ((uint64_t)ss.earn_, 592748626UL);
#endif
  }
}

////////////////////////////////  GlobalShare  ///////////////////////////////
TEST(GlobalShare, GlobalShareEth) {
  ShareEth share1;
  share1.set_headerhash(0x12345678);
  share1.set_nonce(0x87654321);

  ShareEth share2;
  share2.set_headerhash(0x12345678);
  share2.set_nonce(0x33333333);

  ShareEth share3;
  share3.set_headerhash(0x33333333);
  share3.set_nonce(0x55555555);

  GlobalShareEth a(share1);
  GlobalShareEth b(share2);
  GlobalShareEth c(share3);
  GlobalShareEth d(share3);

  ASSERT_EQ(a < a, false);

  ASSERT_EQ(c < d, false);
  ASSERT_EQ(d < c, false);

  ASSERT_EQ(a < b, false);
  ASSERT_EQ(b < a, true);

  ASSERT_EQ(a < c, true);
  ASSERT_EQ(c < a, false);

  ASSERT_EQ(b < c, true);
  ASSERT_EQ(c < b, false);
}

////////////////////////////////  DuplicateShareChecker
//////////////////////////////////
TEST(DuplicateShareChecker, DuplicateShareCheckerEth) {
  // same share
  {
    DuplicateShareCheckerEth dsc(3);

    ShareEth share;
    share.set_height(12345);
    share.set_headerhash(0x12345678);
    share.set_nonce(0x87654321);

    ASSERT_EQ(dsc.addShare(share), true);
    ASSERT_EQ(dsc.addShare(share), false);
    ASSERT_EQ(dsc.addShare(share), false);
  }

  // different height
  {
    DuplicateShareCheckerEth dsc(3);

    ShareEth share;
    share.set_height(12345);
    share.set_headerhash(0x12345678);
    share.set_nonce(0x87654321);

    ASSERT_EQ(dsc.addShare(share), true);

    share.set_height(12346);
    ASSERT_EQ(dsc.addShare(share), true);

    share.set_height(12347);
    ASSERT_EQ(dsc.addShare(share), true);

    share.set_height(12348);
    ASSERT_EQ(dsc.addShare(share), true);

    share.set_height(12347);
    ASSERT_EQ(dsc.addShare(share), false);

    share.set_height(12346);
    ASSERT_EQ(dsc.addShare(share), false);

    share.set_height(12349);
    ASSERT_EQ(dsc.addShare(share), true);

    ASSERT_EQ(dsc.gshareSetMapSize(), 3ull);
  }

  // different headerHash
  {
    DuplicateShareCheckerEth dsc(3);

    ShareEth share;
    share.set_height(12345);
    share.set_headerhash(0x12345678);
    share.set_nonce(0x87654321);

    ASSERT_EQ(dsc.addShare(share), true);

    share.set_headerhash(0x123);
    ASSERT_EQ(dsc.addShare(share), true);

    share.set_headerhash(0x456);
    ASSERT_EQ(dsc.addShare(share), true);

    share.set_headerhash(0x123);
    ASSERT_EQ(dsc.addShare(share), false);

    share.set_headerhash(0x456);
    ASSERT_EQ(dsc.addShare(share), false);

    share.set_headerhash(0x12345678);
    ASSERT_EQ(dsc.addShare(share), false);

    share.set_headerhash(0x87654321);
    ASSERT_EQ(dsc.addShare(share), true);
  }

  // different nonce
  {
    DuplicateShareCheckerEth dsc(3);

    ShareEth share;
    share.set_height(12345);
    share.set_headerhash(0x12345678);
    share.set_nonce(0x87654321);

    ASSERT_EQ(dsc.addShare(share), true);

    share.set_nonce(0x123);
    ASSERT_EQ(dsc.addShare(share), true);

    share.set_nonce(0x456);
    ASSERT_EQ(dsc.addShare(share), true);

    share.set_nonce(0x123);
    ASSERT_EQ(dsc.addShare(share), false);

    share.set_nonce(0x456);
    ASSERT_EQ(dsc.addShare(share), false);

    share.set_nonce(0x12345678);
    ASSERT_EQ(dsc.addShare(share), true);

    share.set_nonce(0x87654321);
    ASSERT_EQ(dsc.addShare(share), false);
  }
}
