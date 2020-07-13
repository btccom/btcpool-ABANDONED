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

#include "CommonGrin.h"

#include "cuckoo/cuckaroo.h"
#include "cuckoo/cuckarood.h"
#include "cuckoo/cuckaroom.h"
#include "cuckoo/cuckarooz.h"
#include "cuckoo/cuckatoo.h"
#include "cuckoo/siphash.h"
#include "libblake2/blake2.h"

#include <boost/multiprecision/cpp_int.hpp>
#include <limits>
#include <Utils.h>

static const uint8_t BASE_EDGE_BITS = 24;
static const uint8_t DEFAULT_MIN_EDGE_BITS = 31;
static const uint8_t SECOND_POW_EDGE_BITS = 29;

static const uint64_t MAX_DIFFICUTY = std::numeric_limits<uint64_t>::max();

static const uint64_t BLOCK_TIME_SEC = 60;
static const uint64_t HOUR_HEIGHT = 3600 / BLOCK_TIME_SEC;
static const uint64_t DAY_HEIGHT = 24 * HOUR_HEIGHT;
static const uint64_t WEEK_HEIGHT = 7 * DAY_HEIGHT;
static const uint64_t YEAR_HEIGHT = 52 * WEEK_HEIGHT;

static const uint64_t GRIN_BASE = 1000000000;
static const uint64_t REWARD = BLOCK_TIME_SEC * GRIN_BASE;

static uint64_t
PowDifficultyGrinScaled(uint64_t hash, uint32_t secondaryScaling) {
  boost::multiprecision::uint128_t x = secondaryScaling;
  x <<= 64;
  x /= hash;
  return x > MAX_DIFFICUTY ? MAX_DIFFICUTY : static_cast<uint64_t>(x);
}

// verify that edges are ascending and form a cycle in header-generated graph
bool VerifyPowGrinPrimary(
    const std::vector<uint64_t> &edges, siphash_keys &keys, uint32_t edgeBits) {
  return verify_cuckatoo(edges, keys, edgeBits);
}

// verify that edges are ascending and form a cycle in header-generated graph
bool VerifyPowGrinSecondary(
    const std::vector<uint64_t> &edges,
    siphash_keys &keys,
    uint32_t edgeBits,
    uint16_t version) {
  switch (version) {
  case 4:
    return verify_cuckarooz(edges, keys, edgeBits);
  case 3:
    return verify_cuckaroom(edges, keys, edgeBits);
  case 2:
    return verify_cuckarood(edges, keys, edgeBits);
  default:
    return verify_cuckaroo(edges, keys, edgeBits);
  }
}

bool VerifyPowGrin(
    const PreProofGrin &preProof,
    uint32_t edgeBits,
    const std::vector<uint64_t> &proofs) {
  if (edgeBits != SECOND_POW_EDGE_BITS && edgeBits < DEFAULT_MIN_EDGE_BITS)
    return false;

  siphash_keys siphashKeys;
  char preProofKeys[32];
  blake2b(
      preProofKeys, sizeof(preProofKeys), &preProof, sizeof(preProof), 0, 0);
  siphashKeys.setkeys(preProofKeys);
  return edgeBits == SECOND_POW_EDGE_BITS
      ? VerifyPowGrinSecondary(
            proofs, siphashKeys, edgeBits, preProof.prePow.version.value())
      : VerifyPowGrinPrimary(proofs, siphashKeys, edgeBits);
}

uint256 PowHashGrin(uint32_t edgeBits, const std::vector<uint64_t> &proofs) {
  // Compress the proofs to a bit vector
  std::vector<uint8_t> proofBits((proofs.size() * edgeBits + 7) / 8, 0);
  uint64_t edgeMask = (static_cast<uint64_t>(1) << edgeBits) - 1;
  size_t i = 0;
  for (uint64_t proof : proofs) {
    proof &= edgeMask;
    for (uint32_t j = 0; j < edgeBits; ++j) {
      if (0x1 & (proof >> j)) {
        uint32_t position = i * edgeBits + j;
        proofBits[position / 8] |= (1 << (position % 8));
      }
    }
    ++i;
  }

  // Generate the blake2b hash
  uint256 hash;
  blake2b(hash.begin(), sizeof(hash), proofBits.data(), proofBits.size(), 0, 0);
  return hash;
}

uint32_t GraphWeightGrin(uint64_t height, uint32_t edgeBits) {
  uint64_t xprEdgeBits = edgeBits;

  auto bitsOverMin =
      edgeBits <= DEFAULT_MIN_EDGE_BITS ? 0 : edgeBits - DEFAULT_MIN_EDGE_BITS;
  auto expiryHeight = (1 << bitsOverMin) * YEAR_HEIGHT;
  if (edgeBits < 32 && height >= expiryHeight) {
    auto weeks = 1 + (height - expiryHeight) / WEEK_HEIGHT;
    xprEdgeBits = xprEdgeBits > weeks ? xprEdgeBits - weeks : 0;
  }

  return ((2 << (edgeBits - BASE_EDGE_BITS)) * xprEdgeBits);
}

uint32_t
PowScalingGrin(uint64_t height, uint32_t edgeBits, uint32_t secondaryScaling) {
  return edgeBits == SECOND_POW_EDGE_BITS ? secondaryScaling
                                          : GraphWeightGrin(height, edgeBits);
}

uint64_t PowDifficultyGrin(
    uint64_t height,
    uint32_t edgeBits,
    uint32_t secondaryScaling,
    const std::vector<uint64_t> &proofs) {
  // Compress the proofs to a bit vector
  std::vector<uint8_t> proofBits((proofs.size() * edgeBits + 7) / 8, 0);
  uint64_t edgeMask = (static_cast<uint64_t>(1) << edgeBits) - 1;
  size_t i = 0;
  for (uint64_t proof : proofs) {
    proof &= edgeMask;
    for (uint32_t j = 0; j < edgeBits; ++j) {
      if (0x1 & (proof >> j)) {
        uint32_t position = i * edgeBits + j;
        proofBits[position / 8] |= (1 << (position % 8));
      }
    }
    ++i;
  }

  // Generate the blake2b hash
  boost::endian::big_uint64_buf_t hash[4];
  blake2b(hash, sizeof(hash), proofBits.data(), proofBits.size(), 0, 0);

  // Scale the difficulty
  return PowDifficultyGrinScaled(
      hash[0].value(), PowScalingGrin(height, edgeBits, secondaryScaling));
}

uint64_t GetBlockRewardGrin(uint64_t height) {
  return REWARD;
}