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
#include "BitcoinUtils.h"


////////////////////////////////  Block Rewards  /////////////////////////////////
TEST(BitcoinUtils, GetBlockReward) {
  // using mainnet
  SelectParams(CBaseChainParams::MAIN);
  auto consensus = Params().GetConsensus();
  int64_t reward = 0;
  
  reward = GetBlockReward(1, consensus);
  ASSERT_EQ(reward, 5000000000); // 50 BTC

  reward = GetBlockReward(3333, consensus);
  ASSERT_EQ(reward, 5000000000); // 50 BTC

  reward = GetBlockReward(200009, consensus);
  ASSERT_EQ(reward, 5000000000); // 50 BTC

  reward = GetBlockReward(210000, consensus);
  ASSERT_EQ(reward, 2500000000); // 25 BTC

  reward = GetBlockReward(382525, consensus);
  ASSERT_EQ(reward, 2500000000); // 25 BTC

  reward = GetBlockReward(419999, consensus);
  ASSERT_EQ(reward, 2500000000); // 25 BTC

  reward = GetBlockReward(420000, consensus);
  ASSERT_EQ(reward, 1250000000); // 12.5 BTC

  reward = GetBlockReward(504031, consensus);
  ASSERT_EQ(reward, 1250000000); // 12.5 BTC

  reward = GetBlockReward(629999, consensus);
  ASSERT_EQ(reward, 1250000000); // 12.5 BTC

  reward = GetBlockReward(630000, consensus);
  ASSERT_EQ(reward, 625000000); // 6.25 BTC

  reward = GetBlockReward(700000, consensus);
  ASSERT_EQ(reward, 625000000); // 6.25 BTC

  reward = GetBlockReward(5000000, consensus);
  ASSERT_EQ(reward, 596);       // 596 satoshi

  reward = GetBlockReward(6719999, consensus);
  ASSERT_EQ(reward, 2);         // 2 satoshi

  // The 32th halvings.
  reward = GetBlockReward(6720000, consensus);
  ASSERT_EQ(reward, 1);         // 1 satoshi

  reward = GetBlockReward(6929999, consensus);
  ASSERT_EQ(reward, 1);         // 1 satoshi

  // The 33th and the lastest halvings.
  reward = GetBlockReward(6930000, consensus);
  ASSERT_EQ(reward, 0);         // 0 satoshi

  // The 63th halvings (in fact does not exist).
  // Detects if the calculation method is affected by the int64 sign bit.
  // If the method is affected by the sign bit, -2 may be returned.
  reward = GetBlockReward(13300000, consensus);
  ASSERT_EQ(reward, 0);         // 0 satoshi

  reward = GetBlockReward(70000000, consensus);
  ASSERT_EQ(reward, 0);         // 0 satoshi
}
