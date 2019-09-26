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
#ifndef ETH_CONSENSUS_H_
#define ETH_CONSENSUS_H_

#include <string>
#include <stdint.h>

// The consensus rules of Ethereum and Ethereum Classic
class EthConsensus {
public:
  // Chain names is references to Parity's (an Ethereum node) configuration.
  // https://paritytech.github.io/parity-config-generator/
  enum class Chain {
    CLASSIC, // Ethereum Classic Main Network
    FOUNDATION, // Ethereum Main Network
    UNKNOWN // Unknown / other chains
  };

  static Chain getChain(std::string chainStr);
  static std::string getChainStr(const Chain chain);

  // The "static" block reward for the winning block.
  // Uncle block rewards are not included.
  static int64_t getStaticBlockReward(int nHeight, Chain chain);

  inline static int64_t
  getStaticBlockReward(int nHeight, const std::string &chainStr) {
    return getStaticBlockReward(nHeight, getChain(chainStr));
  }

  // Get the ratio of the uncle block's reward and the main chain block's
  // reward. Example: the block reward of FOUNDATION uncle block is 7/8 of a
  // mainchain block's, the 0.875 will be returned.
  static double getUncleBlockRewardRatio(int nHeight, Chain chain);

  static void setHardForkConstantinopleHeight(int height);

protected:
  // static block rewards of Ethereum Classic Main Network
  static int64_t getStaticBlockRewardClassic(int nHeight);
  // static block rewards of Ethereum Main Network
  static int64_t getStaticBlockRewardFoundation(int nHeight);

  static double getUncleBlockRewardRatioClassic(int nHeight);
  static double getUncleBlockRewardRatioFoundation(int nHeight);

  // The hard fork Constantinople of Ethereum mainnet
  static int kHardForkConstantinopleHeight_;
};

#endif // ETH_CONSENSUS_H_
