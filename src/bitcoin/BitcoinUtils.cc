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
#include "Utils.h"
#include "utilities_js.hpp"

#include <streams.h>

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


bool checkBitcoinRPC(const string &rpcAddr, const string &rpcUserpass) {
  string response;
  string request = "{\"jsonrpc\":\"1.0\",\"id\":\"1\",\"method\":\"getnetworkinfo\",\"params\":[]}";
  bool res = blockchainNodeRpcCall(rpcAddr.c_str(), rpcUserpass.c_str(),
                             request.c_str(), response);
  if (!res) {
    LOG(ERROR) << "rpc call failure";
    return false;
  }

  LOG(INFO) << "getnetworkinfo: " << response;

  JsonNode r;
  if (!JsonNode::parse(response.c_str(),
                       response.c_str() + response.length(), r)) {
    LOG(ERROR) << "decode getnetworkinfo failure";
    return false;
  }

  // check if the method not found
  if (r["result"].type() != Utilities::JS::type::Obj) {
    LOG(INFO) << "node doesn't support getnetworkinfo, try getinfo";

    request = "{\"jsonrpc\":\"1.0\",\"id\":\"1\",\"method\":\"getinfo\",\"params\":[]}";
    res = blockchainNodeRpcCall(rpcAddr.c_str(), rpcUserpass.c_str(),
                          request.c_str(), response);
    if (!res) {
      LOG(ERROR) << "rpc call failure";
      return false;
    }

    LOG(INFO) << "getinfo: " << response;

    if (!JsonNode::parse(response.c_str(),
                         response.c_str() + response.length(), r)) {
      LOG(ERROR) << "decode getinfo failure";
      return false;
    }
  }

  // check fields & connections
  if (r["result"].type() != Utilities::JS::type::Obj ||
      r["result"]["connections"].type() != Utilities::JS::type::Int) {
    LOG(ERROR) << "getnetworkinfo missing some fields";
    return false;
  }
  if (r["result"]["connections"].int32() <= 0) {
    LOG(ERROR) << "node connections is zero";
    return false;
  }

  return true;
}

int32_t getBlockHeightFromCoinbase(const string &coinbase1) {
  // https://github.com/bitcoin/bips/blob/master/bip-0034.mediawiki
  const string sizeStr = coinbase1.substr(84, 2);
  auto size = (int32_t)strtol(sizeStr.c_str(), nullptr, 16);

  //  see CScript::push_int64 for the logic
  if(size == OP_0)
    return 0;
  if(size >= OP_1 && size <= OP_1 + 16)
    return size - (OP_1 - 1);

  string heightHex;
  for(int i = 0; i < size; ++i)
  {
    heightHex = coinbase1.substr(86 + (i * 2), 2) + heightHex;
  }

  DLOG(INFO) << "getBlockHeightFromCoinbase coinbase: " << coinbase1;
  DLOG(INFO) << "getBlockHeightFromCoinbase heightHex: " << heightHex;

  return (int32_t)strtol(heightHex.c_str(), nullptr, 16);
}
