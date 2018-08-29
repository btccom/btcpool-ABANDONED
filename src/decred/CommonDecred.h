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

// Decred block header (https://docs.decred.org/advanced/block-header-specifications/)
// Byte arrays are used so that the members are packed
struct BlockHeaderDecred {
  boost::endian::little_uint32_buf_t version;
  uint8_t prevBlock[32];
  uint8_t merkelRoot[32];
  uint8_t stakeRoot[32];
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
  uint8_t extraData[32];
  boost::endian::little_uint32_buf_t stakeVersion;
};

static_assert(sizeof(BlockHeaderDecred) == 180, "Decred block header type is invalid");

#endif
