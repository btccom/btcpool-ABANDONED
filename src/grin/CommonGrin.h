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

#pragma once

#include "uint256.h"

#include <boost/endian/buffers.hpp>

struct PrePowGrin {
  /// Version of the block
  boost::endian::big_uint16_buf_t version;
  /// Height of this block since the genesis block (height 0)
  boost::endian::big_uint64_buf_t height;
  /// Timestamp at which the block was built.
  boost::endian::big_int64_buf_t timestamp;
  /// Hash of the block previous to this in the chain.
  uint256 prevHash;
  /// Root hash of the header MMR at the previous header.
  uint256 prevRoot;
  /// Merklish root of all the commitments in the TxHashSet
  uint256 outputRoot;
  /// Merklish root of all range proofs in the TxHashSet
  uint256 rangeProofRoot;
  /// Merklish root of all transaction kernels in the TxHashSet
  uint256 kernelRoot;
  /// Total accumulated sum of kernel offsets since genesis block.
  /// We can derive the kernel offset sum for *this* block from
  /// the total kernel offset of the previous block header.
  uint256 totalKernelOffset;
  /// Total size of the output MMR after applying this block
  boost::endian::big_uint64_buf_t outputMmrSize;
  /// Total size of the kernel MMR after applying this block
  boost::endian::big_uint64_buf_t kernelMmrSize;
  /// Total accumulated difficulty since genesis block
  boost::endian::big_uint64_buf_t totoalDifficulty;
  /// Variable difficulty scaling factor fo secondary proof of work
  boost::endian::big_uint32_buf_t secondaryScaling;
};

struct PreProofGrin {
  /// Field before nonce
  PrePowGrin prePow;
  /// Nonce increment used to mine this block.
  boost::endian::big_uint64_buf_t nonce;
};

class siphash_keys;
bool VerifyPowGrinPrimary(
    const std::vector<uint64_t> &edges, siphash_keys &keys, uint32_t edgeBits);
bool VerifyPowGrinSecondary(
    const std::vector<uint64_t> &edges,
    siphash_keys &keys,
    uint32_t edgeBits,
    uint16_t version);
bool VerifyPowGrin(
    const PreProofGrin &preProof,
    uint32_t edgeBits,
    const std::vector<uint64_t> &proofs);

uint256 PowHashGrin(uint32_t edgeBits, const std::vector<uint64_t> &proofs);
uint32_t GraphWeightGrin(uint64_t height, uint32_t edgeBits);
uint32_t
PowScalingGrin(uint64_t height, uint32_t edgeBits, uint32_t secondaryScaling);
uint64_t PowDifficultyGrin(
    uint64_t height,
    uint32_t edgeBits,
    uint32_t secondaryScaling,
    const std::vector<uint64_t> &proofs);

uint64_t GetBlockRewardGrin(uint64_t height);

inline uint64_t DiffToShift(uint64_t diff) {
  uint64_t shift = 0;
  while (diff >>= 1) {
    ++shift;
  }
  return shift;
}
