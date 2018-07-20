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

#ifndef CHAIN_TYPE_UBTC

/////////////////////// Block Reward of BTC, BCH, SBTC ///////////////////////
int64_t GetBlockReward(int nHeight, const Consensus::Params& consensusParams)
{
  int halvings = nHeight / consensusParams.nSubsidyHalvingInterval;
  // Force block reward to zero when right shift is undefined.
  if (halvings >= 64)
    return 0;

  int64_t nSubsidy = 50 * COIN_TO_SATOSHIS;

  // Block reward is cut in half every 210,000 blocks which will occur approximately every 4 years.
  nSubsidy >>= halvings; // this line is secure, it copied from bitcoin's validation.cpp
  return nSubsidy;
}

#else

/////////////////////// Block Reward of UBTC ///////////////////////
// copied from UnitedBitcoin-v1.1.0.0
int64_t GetBlockReward(int nHeight, const Consensus::Params& consensusParams)
{
	int halvings;

	if (nHeight < Params().GetConsensus().ForkV1Height)
	{
	    halvings = nHeight / consensusParams.nSubsidyHalvingInterval;
	    // Force block reward to zero when right shift is undefined.
	    if (halvings >= 64)
	        return 0;

	    int64_t nSubsidy = 50 * COIN_TO_SATOSHIS;
	    // Subsidy is cut in half every 210,000 blocks which will occur approximately every 4 years.
	    nSubsidy >>= halvings;
	    return nSubsidy;
	}
	else {
		int halfPeriodLeft = consensusParams.ForkV1Height - 1 - consensusParams.nSubsidyHalvingInterval * 2;
		int halfPeriodRight = (consensusParams.nSubsidyHalvingInterval - halfPeriodLeft) * 10;

		int PeriodEndHeight = consensusParams.ForkV1Height -1 + (consensusParams.nSubsidyHalvingInterval - halfPeriodLeft) * 10;
		if (nHeight <= PeriodEndHeight)
			halvings = 2;
		else
		{
			halvings = 3 + (nHeight - PeriodEndHeight - 1) / (consensusParams.nSubsidyHalvingInterval * 10);
		}

		// Force block reward to zero when right shift is undefined.
	    if (halvings >= 64)
	        return 0;

	    int64_t nSubsidy = 50 * COIN_TO_SATOSHIS;
	    // Subsidy is cut in half every 210,000 blocks which will occur approximately every 4 years.
	    nSubsidy >>= halvings;
		nSubsidy = nSubsidy / 10 * 0.8;
		
	    return nSubsidy;	
	}
}

#endif


uint64_t GetBlockRewardBytom(uint64_t nHeight)
{
	//	based on bytom's mining.go (BlockSubsidy function)
	const uint64_t initialBlockSubsidy = 140700041250000000UL;
	const uint64_t baseSubsidy = 41250000000UL;
	const uint64_t subsidyReductionInterval = 840000UL;
	if(nHeight == 0)
	{
		return initialBlockSubsidy;
	}
	return baseSubsidy >> (nHeight/subsidyReductionInterval);
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