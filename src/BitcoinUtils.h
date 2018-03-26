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
#ifndef BITCOIN_UTILS_H_
#define BITCOIN_UTILS_H_

#include <string>

#include <core_io.h>
#include <streams.h>
#include <amount.h>
#include <chainparams.h>
#include <utilstrencodings.h>
#include <base58.h>

#ifdef CHAIN_TYPE_BCH
  // header that defined DecodeDestination & IsValidDestinationString
  #include <dstencode.h>
#endif

std::string EncodeHexBlock(const CBlock &block);
std::string EncodeHexBlockHeader(const CBlockHeader &blkHeader);

CAmount GetBlockSubsidy(int nHeight, const Consensus::Params& consensusParams);

#ifdef CHAIN_TYPE_SBTC
CTxDestination DecodeDestination(const std::string& str);
bool IsValidDestinationString(const std::string& str);
#endif // CHAIN_TYPE_SBTC

#endif // BITCOIN_UTILS_H_
