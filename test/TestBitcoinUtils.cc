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

#include "bitcoin/BitcoinUtils.h"

/////////////////////////  Block Rewards /////////////////////////
void TestBitcoinBlockReward(int height, int64_t expectedReward) {
  // using mainnet
  SelectParams(CBaseChainParams::MAIN);
  auto consensus = Params().GetConsensus();
  int64_t reward = 0;

  reward = GetBlockReward(height, consensus);
  ASSERT_EQ(reward, expectedReward);
}

#ifdef CHAIN_TYPE_LTC
TEST(BitcoinUtils, GetBlockRewardLitecoin) {
  TestBitcoinBlockReward(1, 5000000000);
  TestBitcoinBlockReward(3333, 5000000000);
  TestBitcoinBlockReward(200009, 5000000000);
  TestBitcoinBlockReward(210000, 5000000000);
  TestBitcoinBlockReward(382525, 5000000000);
  TestBitcoinBlockReward(419999, 5000000000);
  TestBitcoinBlockReward(420000, 5000000000);
  TestBitcoinBlockReward(504031, 5000000000);
  TestBitcoinBlockReward(629999, 5000000000);
  TestBitcoinBlockReward(630000, 5000000000);
  TestBitcoinBlockReward(700000, 5000000000);
  TestBitcoinBlockReward(5000000, 156250000);
  TestBitcoinBlockReward(6719999, 39062500);
  TestBitcoinBlockReward(6720000, 19531250);
  TestBitcoinBlockReward(6929999, 19531250);
  TestBitcoinBlockReward(6930000, 19531250);
  TestBitcoinBlockReward(13300000, 152587);
  TestBitcoinBlockReward(70000000, 0);
}
#elif defined(CHAIN_TYPE_ZEC)
TEST(BitcoinUtils, GetBlockRewardZCash) {
  TestBitcoinBlockReward(1, 50000);
  TestBitcoinBlockReward(3333, 166650000);
  TestBitcoinBlockReward(200009, 1000000000);
  TestBitcoinBlockReward(210000, 1000000000);
  TestBitcoinBlockReward(382525, 1000000000);
  TestBitcoinBlockReward(419999, 1000000000);
  TestBitcoinBlockReward(420000, 1000000000);
  TestBitcoinBlockReward(504031, 1000000000);
  TestBitcoinBlockReward(629999, 1000000000);
  TestBitcoinBlockReward(630000, 1000000000);
  TestBitcoinBlockReward(700000, 1000000000);
  TestBitcoinBlockReward(5000000, 39062500);
  TestBitcoinBlockReward(6719999, 9765625);
  TestBitcoinBlockReward(6720000, 9765625);
  TestBitcoinBlockReward(6929999, 4882812);
  TestBitcoinBlockReward(6930000, 4882812);
  TestBitcoinBlockReward(13300000, 38146);
  TestBitcoinBlockReward(70000000, 0);
}
#else
TEST(BitcoinUtils, GetBlockRewardBitcoin) {
  TestBitcoinBlockReward(1, 5000000000); // 50 BTC
  TestBitcoinBlockReward(3333, 5000000000); // 50 BTC
  TestBitcoinBlockReward(200009, 5000000000); // 50 BTC
  TestBitcoinBlockReward(210000, 2500000000); // 25 BTC
  TestBitcoinBlockReward(382525, 2500000000); // 25 BTC
  TestBitcoinBlockReward(419999, 2500000000); // 25 BTC
  TestBitcoinBlockReward(420000, 1250000000); // 12.5 BTC
  TestBitcoinBlockReward(504031, 1250000000); // 12.5 BTC

#ifdef CHAIN_TYPE_UBTC
  TestBitcoinBlockReward(629999, 100000000); // 1 UBTC
  TestBitcoinBlockReward(630000, 100000000); // 1 UBTC
  TestBitcoinBlockReward(700000, 100000000); // 1 UBTC
  TestBitcoinBlockReward(813499, 100000000); // 1 UBTC
  TestBitcoinBlockReward(813500, 50000000); // 0.5 UBTC
  TestBitcoinBlockReward(5000000, 12500000); // 0.25 UBTC
  TestBitcoinBlockReward(6719999, 6250000); // 0.125 UBTC
  TestBitcoinBlockReward(6720000, 6250000); // 0.125 UBTC
  TestBitcoinBlockReward(6929999, 6250000); // 0.125 UBTC
  TestBitcoinBlockReward(6930000, 6250000); // 0.125 UBTC
  TestBitcoinBlockReward(13300000, 781250); // 0.015625 UBTC
#else
  TestBitcoinBlockReward(629999, 1250000000); // 12.5 BTC
  TestBitcoinBlockReward(630000, 625000000); // 6.25 BTC
  TestBitcoinBlockReward(700000, 625000000); // 6.25 BTC
  TestBitcoinBlockReward(5000000, 596); // 596 satoshi
  TestBitcoinBlockReward(6719999, 2); // 2 satoshi
  // The 32th halvings.
  TestBitcoinBlockReward(6720000, 1); // 1 satoshi
  TestBitcoinBlockReward(6929999, 1); // 1 satoshi
  // The 33th and the lastest halvings.
  TestBitcoinBlockReward(6930000, 0); // 0 satoshi
  // The 63th halvings (in fact does not exist).
  // Detects if the calculation method is affected by the int64_t sign bit.
  // If the method is affected by the sign bit, -2 may be returned.
  TestBitcoinBlockReward(13300000, 0); // 0 satoshi
#endif

  TestBitcoinBlockReward(70000000, 0); // 0 satoshi
}
#endif
