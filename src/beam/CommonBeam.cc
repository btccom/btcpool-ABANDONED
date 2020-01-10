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
#include <boost/endian/arithmetic.hpp>
#include <vector>
#include <arith_uint256.h>
#include "CommonBeam.h"
#include "Utils.h"

using std::vector;

static arith_uint256 kMaxUint256(
    "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
static uint64_t kMaxUint64 = 0xffffffffffffffffull;

/*
 * Core data types used in BEAM:
 * <https://github.com/BeamMW/beam/wiki/Core-transaction-elements>
 *
 *   Height - 64-bit unsigned integer
 *   HeightRange - a pair of Height
 *   Timestamp - 64-bit unsigned integer
 *   Amount - used to denote the value of a single UTXO. 64-bit unsigned integer
 *   AmountBig - used to denote the value of an arbitrary number of UTXOs.
 * Consists of 2 64-bit unsigned integer (i.e. equivalent to 128-bit integer)
 *   Difficulty - 32-bit encoding of a floating-point number. 8 bits for order,
 * 24 bits for mantissa. Difficulty::Raw - 256-bit unsigned integer. Represents
 * an "unpacked" Difficulty on a linear scale. Used to represent the chainwork
 * (sum of difficulties).
 */

// BEAM's block bits are compression for difficulty, not for target.

uint256 Beam_BitsToTarget(uint32_t bits) {
  return Beam_DiffToTarget(Beam_BitsToDiff(bits));
}

uint32_t Beam_TargetToBits(const uint256 &target) {
  return Beam_DiffToBits(Beam_TargetToDiff(target));
}

uint256 Beam_DiffToTarget(uint64_t diff) {
  if (0 == diff) {
    return ArithToUint256(kMaxUint256);
  }

  arith_uint256 target = kMaxUint256 / diff;
  return ArithToUint256(target);
}

uint64_t Beam_TargetToDiff(const uint256 &targetBin) {
  arith_uint256 target = UintToArith256(targetBin);

  if (target == 0) {
    return kMaxUint64;
  }

  arith_uint256 diff = kMaxUint256 / target;
  return diff.GetLow64();
}

double Beam_BitsToDiff(uint32_t bits) {
  beam::Difficulty diff(bits);
  return diff.ToFloat();
}

uint32_t Beam_DiffToBits(uint64_t diff) {
  beam::Difficulty beamDiff;
  beamDiff.Pack(diff);
  return beamDiff.m_Packed;
}

bool Beam_ComputeHash(
    const string &input,
    const uint64_t nonce,
    const string &output,
    beam::Difficulty::Raw &hash,
    uint32_t hashVersion) {
  boost::endian::big_uint64_t nonceBigEndian = nonce;

  vector<char> inputBin, outputBin;
  if (!Hex2Bin(input.data(), input.size(), inputBin) ||
      !Hex2Bin(output.data(), output.size(), outputBin)) {
    return false;
  }

  beam::Block::PoW pow;

  if (outputBin.size() != beam::Block::PoW::nSolutionBytes) {
    return false;
  }
  memcpy(
      pow.m_Indices.data(), outputBin.data(), beam::Block::PoW::nSolutionBytes);

  if (sizeof(nonceBigEndian) != beam::Block::PoW::NonceType::nBytes) {
    return false;
  }
  memcpy(
      pow.m_Nonce.m_pData,
      (const char *)&nonceBigEndian,
      beam::Block::PoW::NonceType::nBytes);

  if (!pow.ComputeHash(inputBin.data(), inputBin.size(), hash, hashVersion)) {
    return false;
  }

  return true;
}

uint256 Beam_Uint256Conv(const beam::Difficulty::Raw &raw) {
  uint256 res;
  memcpy((char *)res.begin(), raw.m_pData, raw.nBytes);
  return res;
}

beam::Difficulty::Raw Beam_Uint256Conv(const uint256 &target) {
  beam::Difficulty::Raw raw;
  memcpy(raw.m_pData, target.begin(), raw.nBytes);
  return raw;
}

double Beam_GetStaticBlockReward(uint32_t height) {
  // During the first year of Beam existence, miner reward will be 80 coins per
  // block. https://github.com/BeamMW/beam/wiki/BEAM-Mining

  return height > 525600 ? 40.0 * BEAM_COIN : 80.0 * BEAM_COIN;
}
