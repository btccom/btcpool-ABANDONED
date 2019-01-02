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
#include "CommonBeam.h"

uint256 Beam_BitsToTarget(uint32_t bits) {
    return uint256S("000000ffff000000000000000000000000000000000000000000000000000000");
}

uint32_t Beam_TargetToBits(const uint256 &target) {
    return 0x01005678;
}

uint256 Beam_DiffToTarget(double diff) {
    return uint256S("000000ffff000000000000000000000000000000000000000000000000000000");
}

double Beam_TargetToDiff(const uint256 &target) {
    return 10.0;
}

double Beam_BitsToDiff(uint32_t bits) {
    return 10.0;
}

uint32_t Beam_DiffToBits(double diff) {
    return 0x01005678;
}

bool Beam_ComputeHash(const string &input, const uint64_t nonce, const string &output, uint256 &hash) {
    hash = uint256S("000000ffff000000000000000000000000000000000000000000000000000000");
    return true;
}
