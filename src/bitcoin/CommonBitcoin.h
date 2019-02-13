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
#ifndef POOL_COMMON_BITCOIN_H_
#define POOL_COMMON_BITCOIN_H_

#include "Common.h"

#include <uint256.h>

////////////////////////////// for Bitcoin //////////////////////////////
uint64_t TargetToDiff(uint256 &target);
uint64_t TargetToDiff(const string &str);

void BitsToTarget(uint32_t bits, uint256 &target);
void DiffToTarget(uint64_t diff, uint256 &target, bool useTable = true);
void BitsToDifficulty(uint32_t bits, double *difficulty);
void BitsToDifficulty(uint32_t bits, uint64_t *difficulty);

////////////////////////////// for Bitcoin //////////////////////////////

#endif
