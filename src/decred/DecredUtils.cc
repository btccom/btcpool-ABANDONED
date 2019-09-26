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

#include "DecredUtils.h"
#include "CommonDecred.h"

static int64_t
GetBlockRewaredDecred(uint32_t height, const NetworkParamsDecred &params) {
  int64_t iterations = height / params.subsidyReductionInterval;
  int64_t subsidy = params.baseSubsidy;
  for (int64_t i = 0; i < iterations; i++) {
    subsidy *= params.mulSubsidy;
    subsidy /= params.divSubsidy;
  }
  return subsidy;
}

int64_t GetBlockRewardDecredWork(
    uint32_t height, uint16_t voters, const NetworkParamsDecred &params) {
  int64_t totalProportion = params.workRewardProportion +
      params.stakeRewardProportion + params.blockTaxProportion;
  int64_t powSubsidy = GetBlockRewaredDecred(height, params) *
      params.workRewardProportion / totalProportion;
  if (height < params.stakeValidationHeight) {
    return powSubsidy;
  }

  // Reduce the subsidy according to the number of votes
  return powSubsidy * voters / params.ticketsPerBlock;
}
