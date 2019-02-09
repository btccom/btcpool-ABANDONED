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
// #include <streams.h>
#include <amount.h>
#include <chainparams.h>
#include <base58.h>

#include "CommonBitcoin.h"

#if defined(CHAIN_TYPE_BCH) || defined(CHAIN_TYPE_BSV)
// header that defined DecodeDestination & IsValidDestinationString
#include <dstencode.h>
#ifdef CHAIN_TYPE_BCH
#define AMOUNT_TYPE(x) Amount(x *SATOSHI)
#define COIN_TO_SATOSHIS (COIN / SATOSHI)
#define AMOUNT_SATOSHIS(amt) (amt / SATOSHI)

std::vector<uint256>
ComputeMerkleBranch(const std::vector<uint256> &leaves, uint32_t position);
std::vector<uint256> BlockMerkleBranch(const CBlock &block, uint32_t position);
#else
#define AMOUNT_TYPE Amount
#define COIN_TO_SATOSHIS COIN.GetSatoshis()
#define AMOUNT_SATOSHIS(amt) amt.GetSatoshis()
#endif

namespace BitcoinUtils {
inline bool IsValidDestinationString(const std::string &addr) {
  return ::IsValidDestinationString(addr, Params());
}
inline CTxDestination DecodeDestination(const std::string &str) {
  return ::DecodeDestination(str, Params());
}
} // namespace BitcoinUtils
#elif defined(CHAIN_TYPE_SBTC)
#define AMOUNT_TYPE CAmount
#define COIN_TO_SATOSHIS COIN
#define AMOUNT_SATOSHIS(amt) amt

namespace BitcoinUtils {
CTxDestination DecodeDestination(const std::string &str);
bool IsValidDestinationString(const std::string &str);
} // namespace BitcoinUtils
#else
#define AMOUNT_TYPE CAmount
#define COIN_TO_SATOSHIS COIN
#define AMOUNT_SATOSHIS(amt) amt

namespace BitcoinUtils {
inline bool IsValidDestinationString(const std::string &addr) {
  return ::IsValidDestinationString(addr);
}
inline CTxDestination DecodeDestination(const std::string &str) {
  return ::DecodeDestination(str);
}
} // namespace BitcoinUtils
#endif

std::string EncodeHexBlock(const CBlock &block);
std::string EncodeHexBlockHeader(const CBlockHeader &blkHeader);

int64_t GetBlockReward(int nHeight, const Consensus::Params &consensusParams);

bool checkBitcoinRPC(const string &rpcAddr, const string &rpcUserpass);

int32_t getBlockHeightFromCoinbase(const string &coinbase1);

#endif // BITCOIN_UTILS_H_
