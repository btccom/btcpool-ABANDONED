#include "CommonCkb.h"
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
#include "Utils.h"
#include "eaglesong/eaglesong.h"
#include "utilstrencodings.h"
#include <iostream>
arith_uint256 CKB::GetEaglesongHash(uint256 pow_hash, uint64_t nonce) {
  std::reverse(pow_hash.begin(), pow_hash.end());
  boost::endian::little_uint64_buf_t nonce_t(nonce);
  uint8_t input[40] = {0};
  uint8_t output[32] = {0};
  std::string hash_s;
  memcpy(input, (uint8_t *)(&nonce_t), sizeof(nonce_t));
  memcpy(input + 8, pow_hash.begin(), 32);
  EaglesongHash(output, input, 40);
  Bin2Hex(output, 32, hash_s);
  return UintToArith256(uint256S(hash_s.c_str()));
}

arith_uint256
CKB::GetEaglesongHash2(uint256 pow_hash, uint64_t nonce) { // 3e29d5eaf71970c0
  string nonce_s;
  Bin2Hex((uint8_t *)&nonce, 8, nonce_s);
  uint8_t output[32] = {0};
  std::string hash_s = nonce_s + pow_hash.GetHex();
  std::vector<char> hashvec;
  Hex2Bin(hash_s.c_str(), hashvec);
  EaglesongHash(output, (uint8_t *)(hashvec.data()), 40);
  Bin2Hex(output, 32, hash_s);
  return UintToArith256(uint256S(hash_s.c_str()));
}
