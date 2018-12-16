/*
 The MIT License (MIT)

 Copyright (c) [2018] [BTC.COM]

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

#include "CommonDecred.h"

extern "C" {

#include "libsph/sph_blake.h"
}

#include <cmath>

uint256 BlockHeaderDecred::getHash() const {
  uint256 hash;
  sph_blake256_context ctx;
  sph_blake256_init(&ctx);
  sph_blake256(&ctx, this, sizeof(BlockHeaderDecred));
  sph_blake256_close(&ctx, &hash);
  return hash;
}

static int64_t
GetBlockRewardShare(int64_t reward, const NetworkParamsDecred &params) {
  int64_t totalProportion = params.workRewardProportion +
      params.stakeRewardProportion + params.blockTaxProportion;
  return reward * params.workRewardProportion / totalProportion;
}

static int64_t GetBlockRewardWork(
    int64_t reward,
    uint32_t height,
    uint16_t voters,
    const NetworkParamsDecred &params) {
  int64_t powSubsidy = GetBlockRewardShare(reward, params);
  if (height < params.stakeValidationHeight) {
    return powSubsidy;
  }

  // Reduce the subsidy according to the number of votes
  return powSubsidy * voters / params.ticketsPerBlock;
}

const arith_uint256 NetworkTraitsDecred::Diff1Target =
    arith_uint256{}.SetCompact(0x1d00ffff);

static const NetworkParamsDecred &
GetNetworkParamsDecred(NetworkDecred network) {
  static NetworkParamsDecred mainnetParams{
      3119582664,
      100,
      101,
      6144,
      6,
      3,
      1,
      4096,
      5,
  };
  static NetworkParamsDecred testnetParams{
      2500000000,
      100,
      101,
      2048,
      6,
      3,
      1,
      768,
      5,
  };
  static NetworkParamsDecred simnetParams{
      50000000000,
      100,
      101,
      128,
      6,
      3,
      1,
      16 + (64 * 2),
      5,
  };

  if (network == NetworkDecred::SimNet) {
    return simnetParams;
  } else if (network == NetworkDecred::TestNet) {
    return testnetParams;
  } else {
    return mainnetParams;
  }
}

static int64_t
GetBlockRewardDecred(uint32_t height, const NetworkParamsDecred &params) {
  int64_t iterations = height / params.subsidyReductionInterval;
  int64_t subsidy = params.baseSubsidy;
  for (int64_t i = 0; i < iterations; i++) {
    subsidy *= params.mulSubsidy;
    subsidy /= params.divSubsidy;
  }
  return subsidy;
}

int64_t NetworkTraitsDecred::GetBlockRewardShare(
    uint32_t height, NetworkDecred network) {
  auto &params = GetNetworkParamsDecred(network);
  return ::GetBlockRewardShare(GetBlockRewardDecred(height, params), params);
}

int64_t NetworkTraitsDecred::GetBlockRewardWork(
    uint32_t height, uint16_t voters, NetworkDecred network) {
  auto &params = GetNetworkParamsDecred(network);
  return ::GetBlockRewardWork(
      GetBlockRewardDecred(height, params), height, voters, params);
}

const arith_uint256 NetworkTraitsHcash::Diff1Target =
    arith_uint256{}.SetCompact(0x1d00ffff);

static const NetworkParamsDecred &GetNetworkParamsHcash(NetworkDecred network) {
  static NetworkParamsDecred mainnetParams{
      640000000,
      999,
      1000,
      12288,
      6,
      3,
      1,
      4096,
      5,
  };
  static NetworkParamsDecred testnetParams{
      640000000,
      999,
      1000,
      2048,
      6,
      3,
      1,
      775,
      5,
  };
  static NetworkParamsDecred simnetParams{
      640000000,
      999,
      1000,
      128,
      6,
      3,
      1,
      16 + (64 * 2),
      5,
  };

  if (network == NetworkDecred::SimNet) {
    return simnetParams;
  } else if (network == NetworkDecred::TestNet) {
    return testnetParams;
  } else {
    return mainnetParams;
  }
}

static int64_t
GetBlockRewardHcash(uint32_t height, const NetworkParamsDecred &params) {
  int64_t iterations = height / params.subsidyReductionInterval;
  double q = static_cast<double>(params.mulSubsidy) / params.divSubsidy;
  double subsidy = 0;

  if (iterations < 1682) {
    subsidy = params.baseSubsidy * (1.0 - iterations * 59363.0 / 100000000.0) *
        pow(q, iterations);
  } else {
    // after 99 years
    subsidy = 100000000.0 / params.subsidyReductionInterval *
        pow(0.1, iterations - 1681);
  }
  return static_cast<int64_t>(subsidy);
}

int64_t NetworkTraitsHcash::GetBlockRewardShare(
    uint32_t height, NetworkDecred network) {
  auto &params = GetNetworkParamsHcash(network);
  return ::GetBlockRewardShare(GetBlockRewardHcash(height, params), params);
}

int64_t NetworkTraitsHcash::GetBlockRewardWork(
    uint32_t height, uint16_t voters, NetworkDecred network) {
  auto &params = GetNetworkParamsHcash(network);
  return ::GetBlockRewardWork(
      GetBlockRewardHcash(height, params), height, voters, params);
}
