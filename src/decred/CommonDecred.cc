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

uint256 BlockHeaderDecred::getHash() const {
  uint256 hash;
  sph_blake256_context ctx;
  sph_blake256_init(&ctx);
  sph_blake256(&ctx, this, sizeof(BlockHeaderDecred));
  sph_blake256_close(&ctx, &hash);
  return hash;
}

const NetworkParamsDecred &NetworkParamsDecred::get(NetworkDecred network) {
  static NetworkParamsDecred mainnetParams{
      arith_uint256{}.SetCompact(0x1d00ffff),
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
      arith_uint256{}.SetCompact(0x1e00ffff),
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
      arith_uint256{}.SetCompact(0x207fffff),
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

  switch (network) {
  case NetworkDecred::MainNet:
    return mainnetParams;
  case NetworkDecred::TestNet:
    return testnetParams;
  case NetworkDecred::SimNet:
    return simnetParams;
  }
  return testnetParams;
}
