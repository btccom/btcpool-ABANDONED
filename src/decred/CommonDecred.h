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

#ifndef COMMON_DECRED_H_
#define COMMON_DECRED_H_

#include <boost/endian/buffers.hpp>
#include <arith_uint256.h>
#include <uint256.h>
#include <string>

// Decred block header
// (https://docs.decred.org/advanced/block-header-specifications/) Byte arrays
// are used so that the members are packed
struct BlockHeaderDecred {
  boost::endian::little_uint32_buf_t version;
  uint256 prevBlock;
  uint256 merkelRoot;
  uint256 stakeRoot;
  boost::endian::little_uint16_buf_t voteBits;
  boost::endian::little_uint48_buf_t finalState;
  boost::endian::little_uint16_buf_t voters;
  boost::endian::little_uint8_buf_t freshStake;
  boost::endian::little_uint8_buf_t revocations;
  boost::endian::little_uint32_buf_t poolSize;
  boost::endian::little_uint32_buf_t nBits;
  boost::endian::little_uint64_buf_t sBits;
  boost::endian::little_uint32_buf_t height;
  boost::endian::little_uint32_buf_t size;
  boost::endian::little_uint32_buf_t timestamp;
  boost::endian::little_uint32_buf_t nonce;
  uint256 extraData;
  boost::endian::little_uint32_buf_t stakeVersion;

  uint256 getHash() const;
};

static_assert(
    sizeof(BlockHeaderDecred) == 180, "Decred block header type is invalid");

// CMD_MAGIC_NUMBER number from the network type
enum class NetworkDecred : uint32_t {
  MainNet = 0xd9b400f9,
  TestNet = 0xb194aa75,
  SimNet = 0x12141c16,
};

// Priority: mainnet > testnet > simnet > others
inline bool operator<(NetworkDecred lhs, NetworkDecred rhs) {
  switch (rhs) {
  case NetworkDecred::MainNet:
    return lhs != NetworkDecred::MainNet;
  case NetworkDecred::TestNet:
    return lhs != NetworkDecred::MainNet && lhs != NetworkDecred::TestNet;
  case NetworkDecred::SimNet:
    return lhs != NetworkDecred::MainNet && lhs != NetworkDecred::TestNet &&
        lhs != NetworkDecred::SimNet;
  }
  return false;
}

struct NetworkParamsDecred {
  arith_uint256 powLimit;
  int64_t baseSubsidy;
  int64_t mulSubsidy;
  int64_t divSubsidy;
  int64_t subsidyReductionInterval;
  int64_t workRewardProportion;
  int64_t stakeRewardProportion;
  int64_t blockTaxProportion;
  int64_t stakeValidationHeight;
  uint16_t ticketsPerBlock;

  static const NetworkParamsDecred &get(NetworkDecred network);
};

#endif
