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
#include "EthConsensus.h"

#include <algorithm>
#include <glog/logging.h>

// The hard fork Constantinople of Ethereum mainnet
int EthConsensus::kHardForkConstantinopleHeight_ = 7280000;

void EthConsensus::setHardForkConstantinopleHeight(int height) {
  kHardForkConstantinopleHeight_ = height;
  LOG(INFO) << "Height of Ethereum Constantinople Hard Fork: "
            << kHardForkConstantinopleHeight_;
}

EthConsensus::Chain EthConsensus::getChain(std::string chainStr) {
  // toupper
  std::transform(chainStr.begin(), chainStr.end(), chainStr.begin(), ::toupper);

  if (chainStr == "CLASSIC") {
    return Chain::CLASSIC;
  } else if (chainStr == "FOUNDATION") {
    return Chain::FOUNDATION;
  } else {
    return Chain::UNKNOWN;
  }
}

std::string EthConsensus::getChainStr(const Chain chain) {
  switch (chain) {
  case Chain::CLASSIC:
    return "CLASSIC";
  case Chain::FOUNDATION:
    return "FOUNDATION";
  case Chain::UNKNOWN:
    return "UNKNOWN";
  }
  // should not be here
  return "UNKNOWN";
}

// The "static" block reward for the winning block.
// Uncle block rewards are not included.
int64_t EthConsensus::getStaticBlockReward(int nHeight, Chain chain) {
  switch (chain) {
  case Chain::CLASSIC:
    return getStaticBlockRewardClassic(nHeight);
  case Chain::FOUNDATION:
    return getStaticBlockRewardFoundation(nHeight);
  case Chain::UNKNOWN:
    return 0;
  }
  // should not be here
  return 0;
}

// static block rewards of Ethereum Classic Main Network
// The implementation followed ECIP-1017:
// https://github.com/ethereumproject/ECIPs/blob/master/ECIPs/ECIP-1017.md
int64_t EthConsensus::getStaticBlockRewardClassic(int nHeight) {
  const int64_t blockEra = (nHeight - 1) / 5000000 + 1;

  // The blockEra is 2 in 2018.
  // Avoid calculations by giving the result directly.
  if (blockEra == 2) {
    return 4e+18;
  }

  int64_t reward = 5e+18;

  for (int i = 1; i < blockEra; i++) {
    // ECIP-1017: all rewards will be reduced at a constant rate of 20% upon
    // entering a new Era. reward *= 0.8 (avoid converts to float)
    reward = reward * 0.8;
  }

  return reward;
}

// static block rewards of Ethereum Main Network
int64_t EthConsensus::getStaticBlockRewardFoundation(int nHeight) {
  // Constantinople fork at block 7080000 on Mainnet.
  if (nHeight >= kHardForkConstantinopleHeight_) {
    return 2e+18;
  }
  // Ethereum Main Network has a static block reward (3 Ether) before height
  // 7080000.
  return 3e+18;
}

double EthConsensus::getUncleBlockRewardRatio(int nHeight, Chain chain) {
  switch (chain) {
  case Chain::CLASSIC:
    return getUncleBlockRewardRatioClassic(nHeight);
  case Chain::FOUNDATION:
    return getUncleBlockRewardRatioFoundation(nHeight);
  case Chain::UNKNOWN:
    return 0.0;
  }
  // should not be here
  return 0.0;
}

// uncle block reward radio of Ethereum Classic Main Network
// The implementation followed ECIP-1017:
// https://github.com/ethereumproject/ECIPs/blob/master/ECIPs/ECIP-1017.md
double EthConsensus::getUncleBlockRewardRatioClassic(int nHeight) {
  // Assume that there is only one height lower than the main chain block

  const int64_t blockEra = (nHeight - 1) / 5000000 + 1;

  if (blockEra == 1) {
    // The blockEra 1 is special
    return 7.0 / 8.0;
  } else if (blockEra == 2) {
    // The blockEra is 2 in 2018.
    // Avoid calculations by giving the result directly.
    return 1.0 / 32.0;
  }

  double radio = 1.0 / 32.0;

  for (int i = 2; i < blockEra; i++) {
    // ECIP-1017: all rewards will be reduced at a constant rate of 20% upon
    // entering a new Era.
    radio *= 0.8;
  }

  return radio;
}

double EthConsensus::getUncleBlockRewardRatioFoundation(int /*nHeight*/) {
  // Assume that there is only one height lower than the main chain block
  return 7.0 / 8.0;
}
