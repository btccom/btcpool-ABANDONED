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
#include "BitcoinUtils.h"

std::string EncodeHexBlock(const CBlock &block) {
  CDataStream ssBlock(SER_NETWORK, PROTOCOL_VERSION);
  ssBlock << block;
  return HexStr(ssBlock.begin(), ssBlock.end());
}
std::string EncodeHexBlockHeader(const CBlockHeader &blkHeader) {
  CDataStream ssBlkHeader(SER_NETWORK, PROTOCOL_VERSION);
  ssBlkHeader << blkHeader;
  return HexStr(ssBlkHeader.begin(), ssBlkHeader.end());
}

CAmount GetBlockSubsidy(int nHeight, const Consensus::Params& consensusParams)
{
  int halvings = nHeight / consensusParams.nSubsidyHalvingInterval;
  // Force block reward to zero when right shift is undefined.
  if (halvings >= 64)
    return 0;

#ifdef CHAIN_TYPE_BCH
  CAmount nSubsidy = 50 * COIN.GetSatoshis();
#else
  CAmount nSubsidy = 50 * COIN;
#endif

  // Subsidy is cut in half every 210,000 blocks which will occur approximately every 4 years.
  nSubsidy >>= halvings;
  return nSubsidy;
}

#ifdef CHAIN_TYPE_SBTC

CTxDestination DecodeDestination(const std::string& str) {
  CBitcoinAddress addr(str);
  return addr.Get();
}

bool IsValidDestinationString(const std::string& str) {
  CBitcoinAddress addr(str);
  return addr.IsValid();
}

#endif // CHAIN_TYPE_SBTC