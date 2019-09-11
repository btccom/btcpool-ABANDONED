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

#include <string>
#include <uint256.h>
#include "beam/core/block_crypt.h"
#include "beam/core/difficulty.h"

using std::string;

const uint64_t BEAM_COIN = 100000000;

uint256 Beam_BitsToTarget(uint32_t bits);
uint32_t Beam_TargetToBits(const uint256 &target);

uint256 Beam_DiffToTarget(uint64_t diff);
uint64_t Beam_TargetToDiff(const uint256 &target);

double Beam_BitsToDiff(uint32_t bits);
uint32_t Beam_DiffToBits(uint64_t diff);

bool Beam_ComputeHash(
    const string &input,
    const uint64_t nonce,
    const string &output,
    beam::Difficulty::Raw &hash,
    uint32_t hashVersion);

uint256 Beam_Uint256Conv(const beam::Difficulty::Raw &raw);
beam::Difficulty::Raw Beam_Uint256Conv(const uint256 &target);

double Beam_GetStaticBlockReward(uint32_t height);
