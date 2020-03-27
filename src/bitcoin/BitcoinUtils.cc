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

#ifdef CHAIN_TYPE_ZEC
#include "equihash/zcash/equihash_zcash.hpp"
#endif

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

uint256 ComputeCoinbaseMerkleRoot(
    const std::vector<char> &coinbaseBin, const vector<uint256> &merkleBranch) {

  uint256 hashMerkleRoot = Hash(coinbaseBin.begin(), coinbaseBin.end());
  for (const uint256 &step : merkleBranch) {
    hashMerkleRoot = Hash(
        BEGIN(hashMerkleRoot), END(hashMerkleRoot), BEGIN(step), END(step));
  }

  return hashMerkleRoot;
}

#if defined(CHAIN_TYPE_BCH) || defined(CHAIN_TYPE_ZEC)
/**
 * This implements a constant-space merkle root/path calculator, limited to 2^32
 * leaves.
 */
static void MerkleComputation(
    const std::vector<uint256> &leaves,
    uint256 *proot,
    bool *pmutated,
    uint32_t branchpos,
    std::vector<uint256> *pbranch) {
  if (pbranch)
    pbranch->clear();
  if (leaves.size() == 0) {
    if (pmutated)
      *pmutated = false;
    if (proot)
      *proot = uint256();
    return;
  }
  bool mutated = false;
  // count is the number of leaves processed so far.
  uint32_t count = 0;
  // inner is an array of eagerly computed subtree hashes, indexed by tree
  // level (0 being the leaves).
  // For example, when count is 25 (11001 in binary), inner[4] is the hash of
  // the first 16 leaves, inner[3] of the next 8 leaves, and inner[0] equal to
  // the last leaf. The other inner entries are undefined.
  uint256 inner[32];
  // Which position in inner is a hash that depends on the matching leaf.
  int matchlevel = -1;
  // First process all leaves into 'inner' values.
  while (count < leaves.size()) {
    uint256 h = leaves[count];
    bool matchh = count == branchpos;
    count++;
    int level;
    // For each of the lower bits in count that are 0, do 1 step. Each
    // corresponds to an inner value that existed before processing the
    // current leaf, and each needs a hash to combine it.
    for (level = 0; !(count & (((uint32_t)1) << level)); level++) {
      if (pbranch) {
        if (matchh) {
          pbranch->push_back(inner[level]);
        } else if (matchlevel == level) {
          pbranch->push_back(h);
          matchh = true;
        }
      }
      mutated |= (inner[level] == h);
      CHash256()
          .Write(inner[level].begin(), 32)
          .Write(h.begin(), 32)
          .Finalize(h.begin());
    }
    // Store the resulting hash at inner position level.
    inner[level] = h;
    if (matchh) {
      matchlevel = level;
    }
  }
  // Do a final 'sweep' over the rightmost branch of the tree to process
  // odd levels, and reduce everything to a single top value.
  // Level is the level (counted from the bottom) up to which we've sweeped.
  int level = 0;
  // As long as bit number level in count is zero, skip it. It means there
  // is nothing left at this level.
  while (!(count & (((uint32_t)1) << level))) {
    level++;
  }
  uint256 h = inner[level];
  bool matchh = matchlevel == level;
  while (count != (((uint32_t)1) << level)) {
    // If we reach this point, h is an inner value that is not the top.
    // We combine it with itself (Bitcoin's special rule for odd levels in
    // the tree) to produce a higher level one.
    if (pbranch && matchh) {
      pbranch->push_back(h);
    }
    CHash256().Write(h.begin(), 32).Write(h.begin(), 32).Finalize(h.begin());
    // Increment count to the value it would have if two entries at this
    // level had existed.
    count += (((uint32_t)1) << level);
    level++;
    // And propagate the result upwards accordingly.
    while (!(count & (((uint32_t)1) << level))) {
      if (pbranch) {
        if (matchh) {
          pbranch->push_back(inner[level]);
        } else if (matchlevel == level) {
          pbranch->push_back(h);
          matchh = true;
        }
      }
      CHash256()
          .Write(inner[level].begin(), 32)
          .Write(h.begin(), 32)
          .Finalize(h.begin());
      level++;
    }
  }
  // Return result.
  if (pmutated)
    *pmutated = mutated;
  if (proot)
    *proot = h;
}

std::vector<uint256>
ComputeMerkleBranch(const std::vector<uint256> &leaves, uint32_t position) {
  std::vector<uint256> ret;
  MerkleComputation(leaves, nullptr, nullptr, position, &ret);
  return ret;
}

std::vector<uint256> BlockMerkleBranch(const CBlock &block, uint32_t position) {
  std::vector<uint256> leaves;
  leaves.resize(block.vtx.size());
  for (size_t s = 0; s < block.vtx.size(); s++) {
#ifdef CHAIN_TYPE_ZEC
    leaves[s] = block.vtx[s].GetHash();
#else
    leaves[s] = block.vtx[s]->GetHash();
#endif
  }
  return ComputeMerkleBranch(leaves, position);
}

#endif

#ifdef CHAIN_TYPE_ZEC

/////////////////////// Block Reward of ZCash ///////////////////////
// @see https://www.zcashcommunity.com/mining/
// The code copied and edited from
// https://github.com/zcash/zcash/blob/be1d68ef763ce405d4d04d7f4d3dfbbdd9084687/src/main.cpp#L1732
static int64_t
GetFullBlockReward(int nHeight, const Consensus::Params &consensusParams) {
  int64_t nSubsidy = 12.5 * COIN_TO_SATOSHIS;

  // Mining slow start
  // The subsidy is ramped up linearly, skipping the middle payout of
  // MAX_SUBSIDY/2 to keep the monetary curve consistent with no slow start.
  if (nHeight < consensusParams.nSubsidySlowStartInterval / 2) {
    nSubsidy /= consensusParams.nSubsidySlowStartInterval;
    nSubsidy *= nHeight;
    return nSubsidy;
  } else if (nHeight < consensusParams.nSubsidySlowStartInterval) {
    nSubsidy /= consensusParams.nSubsidySlowStartInterval;
    nSubsidy *= (nHeight + 1);
    return nSubsidy;
  }

  assert(nHeight > consensusParams.SubsidySlowStartShift());
  int halvings = (nHeight - consensusParams.SubsidySlowStartShift()) /
      consensusParams.nSubsidyHalvingInterval;
  // Force block reward to zero when right shift is undefined.
  if (halvings >= 64) {
    return 0;
  }

  // Subsidy is cut in half every 840,000 blocks which will occur approximately
  // every 4 years.
  nSubsidy >>= halvings;
  return nSubsidy;
}

// Get block rewards that don't include founders rewards
// The code copied and edited from
// https://github.com/zcash/zcash/blob/be1d68ef763ce405d4d04d7f4d3dfbbdd9084687/src/main.cpp#L3701
int64_t GetBlockReward(int nHeight, const Consensus::Params &consensusParams) {
  int64_t reward = GetFullBlockReward(nHeight, consensusParams);

  // Coinbase transaction must include an output sending 20% of
  // the block reward to a founders reward script, until the last founders
  // reward block is reached, with exception of the genesis block.
  // The last founders reward block is defined as the block just before the
  // first subsidy halving block, which occurs at halving_interval +
  // slow_start_shift
  if ((nHeight > 0) &&
      (nHeight <= consensusParams.GetLastFoundersRewardBlockHeight())) {
    reward -= reward / 5;
  }

  return reward;
}

#elif defined(CHAIN_TYPE_UBTC)

/////////////////////// Block Reward of UBTC ///////////////////////
// copied from UnitedBitcoin-v1.1.0.0
int64_t GetBlockReward(int nHeight, const Consensus::Params &consensusParams) {
  int halvings;

  if (nHeight < Params().GetConsensus().ForkV1Height) {
    halvings = nHeight / consensusParams.nSubsidyHalvingInterval;
    // Force block reward to zero when right shift is undefined.
    if (halvings >= 64)
      return 0;

    CAmount nSubsidy = 50 * COIN;
    // Subsidy is cut in half every 210,000 blocks which will occur
    // approximately every 4 years.
    nSubsidy >>= halvings;
    return nSubsidy;
  } else if (
      nHeight >= Params().GetConsensus().ForkV1Height &&
      nHeight < Params().GetConsensus().ForkV4Height) {
    int halfPeriodLeft = consensusParams.ForkV1Height - 1 -
        consensusParams.nSubsidyHalvingInterval * 2;
    int halfPeriodRight =
        (consensusParams.nSubsidyHalvingInterval - halfPeriodLeft) * 10;

    int PeriodEndHeight = consensusParams.ForkV1Height - 1 +
        (consensusParams.nSubsidyHalvingInterval - halfPeriodLeft) * 10;
    if (nHeight <= PeriodEndHeight)
      halvings = 2;
    else {
      halvings = 3 +
          (nHeight - PeriodEndHeight - 1) /
              (consensusParams.nSubsidyHalvingInterval * 10);
    }

    // Force block reward to zero when right shift is undefined.
    if (halvings >= 64)
      return 0;

    CAmount nSubsidy = 50 * COIN;
    // Subsidy is cut in half every 210,000 blocks which will occur
    // approximately every 4 years.
    nSubsidy >>= halvings;
    nSubsidy = nSubsidy / 10 * 0.8;

    return nSubsidy;
  } else {
    int halfPeriodLeft = consensusParams.ForkV1Height - 1 -
        consensusParams.nSubsidyHalvingInterval * 2;
    int halfPeriodRight =
        (consensusParams.nSubsidyHalvingInterval - halfPeriodLeft) * 10;

    int PeriodEndHeight = consensusParams.ForkV1Height - 1 +
        (consensusParams.nSubsidyHalvingInterval - halfPeriodLeft) * 10;
    if (nHeight <= PeriodEndHeight)
      halvings = 2;
    else {
      halvings = 3 +
          (nHeight - PeriodEndHeight - 1) /
              (consensusParams.nSubsidyHalvingInterval * 10);
    }

    // Force block reward to zero when right shift is undefined.
    if (halvings >= 64)
      return 0;

    CAmount nSubsidy = 50 * COIN;
    // Subsidy is cut in half every 210,000 blocks which will occur
    // approximately every 4 years.
    nSubsidy >>= halvings;
    nSubsidy = nSubsidy / 10 * 0.4;

    return nSubsidy;
  }
}

#else

/////////////////////// Block Reward of BTC, BCH ///////////////////////
int64_t GetBlockReward(int nHeight, const Consensus::Params &consensusParams) {
  int halvings = nHeight / consensusParams.nSubsidyHalvingInterval;
  // Force block reward to zero when right shift is undefined.
  if (halvings >= 64) {
    return 0;
  }

  int64_t nSubsidy = 50 * COIN_TO_SATOSHIS;

  // Block reward is cut in half every 210,000 blocks which will occur
  // approximately every 4 years.
  // this line is secure, it copied from bitcoin's validation.cpp
  nSubsidy >>= halvings;
  return nSubsidy;
}

#endif

#ifdef CHAIN_TYPE_ZEC
int32_t getSolutionVintSize() {
  //
  // put solution, see more:
  // https://en.bitcoin.it/wiki/Protocol_documentation#Variable_length_integer
  //
  const CChainParams &chainparams = Params();

  if (chainparams.EquihashN() == 200 && chainparams.EquihashK() == 9) {
    // for mainnet and testnet3, nEquihashN(200) and nEquihashK(9) is the same
    // value. the client return the solution alwasy start with: "fd4005".
    //
    // 0xFD followed by the length as uint16_t, 0x4005 -> 0x0540 = 1344
    // N = 200, K = 9, N / (K + 1) + 1 = 21
    // 21 bits * 2^9 / 8 = 1344 bytes
    //
    // solution is two parts: 3 bytes(1344_vint) + 1344 bytes
    return 3;
  } else if (chainparams.EquihashN() == 48 && chainparams.EquihashK() == 5) {
    // for Regression testnet: const size_t N = 48, K = 5;
    // N = 48, K = 5, N / (K + 1) + 1 = 9
    // 9 bits * 2^5 / 8 = 36 bytes = 0x24
    // the client return the solution alwasy start with: "24", 1 bytes
    return 1;
  }
  return 3; // default size
}

bool CheckEquihashSolution(
    const CBlockHeader *pblock, const CChainParams &params) {
  unsigned int n = params.EquihashN();
  unsigned int k = params.EquihashK();

  // Hash state
  crypto_generichash_blake2b_state state;
  EhZecInitialiseState(n, k, state);

  // I = the block header minus nonce and solution.
  CEquihashInput I{*pblock};
  // I||V
  CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
  ss << I;
  ss << pblock->nNonce;

  // H(I||V||...
  crypto_generichash_blake2b_update(&state, (unsigned char *)&ss[0], ss.size());

  bool isValid = false;
  EhZecIsValidSolution(n, k, state, pblock->nSolution, isValid);

  return isValid;
}
#endif

static bool checkBitcoinRPCGetNetworkInfo(
    const string &rpcAddr, const string &rpcUserpass) {
  string response;
  string request =
      "{\"jsonrpc\":\"1.0\",\"id\":\"1\",\"method\":\"getnetworkinfo\","
      "\"params\":[]}";
  bool res = blockchainNodeRpcCall(
      rpcAddr.c_str(), rpcUserpass.c_str(), request.c_str(), response);

  if (!res) {
    LOG(ERROR) << "rpc getnetworkinfo call failure";
    return false;
  }

  LOG(INFO) << "getnetworkinfo: " << response;

  JsonNode r;
  if (!JsonNode::parse(
          response.c_str(), response.c_str() + response.length(), r)) {
    LOG(ERROR) << "decode getnetworkinfo failure";
    return false;
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

static bool
checkBitcoinRPCGetInfo(const string &rpcAddr, const string &rpcUserpass) {
  string response;
  string request =
      "{\"jsonrpc\":\"1.0\",\"id\":\"1\",\"method\":\"getinfo\",\"params\":[]}";
  bool res = blockchainNodeRpcCall(
      rpcAddr.c_str(), rpcUserpass.c_str(), request.c_str(), response);
  if (!res) {
    LOG(ERROR) << "rpc getinfo call failure";
    return false;
  }

  LOG(INFO) << "getinfo: " << response;

  JsonNode r;
  if (!JsonNode::parse(
          response.c_str(), response.c_str() + response.length(), r)) {
    LOG(ERROR) << "decode getinfo failure";
    return false;
  }

  // check fields & connections
  if (r["result"].type() != Utilities::JS::type::Obj ||
      r["result"]["connections"].type() != Utilities::JS::type::Int) {
    LOG(ERROR) << "getinfo missing some fields";
    return false;
  }
  if (r["result"]["connections"].int32() <= 0) {
    LOG(ERROR) << "node connections is zero";
    return false;
  }

  return true;
}

bool checkBitcoinRPC(const string &rpcAddr, const string &rpcUserpass) {
  return checkBitcoinRPCGetNetworkInfo(rpcAddr, rpcUserpass) ||
      checkBitcoinRPCGetInfo(rpcAddr, rpcUserpass);
}

int32_t getBlockHeightFromCoinbase(const string &coinbase1) {
  // https://github.com/bitcoin/bips/blob/master/bip-0034.mediawiki
  const string sizeStr = coinbase1.substr(84, 2);
  auto size = (int32_t)strtol(sizeStr.c_str(), nullptr, 16);

  //  see CScript::push_int64 for the logic
  if (size == OP_0)
    return 0;
  if (size >= OP_1 && size <= OP_1 + 16)
    return size - (OP_1 - 1);

  string heightHex;
  for (int i = 0; i < size; ++i) {
    heightHex = coinbase1.substr(86 + (i * 2), 2) + heightHex;
  }

  DLOG(INFO) << "getBlockHeightFromCoinbase coinbase: " << coinbase1;
  DLOG(INFO) << "getBlockHeightFromCoinbase heightHex: " << heightHex;

  return (int32_t)strtol(heightHex.c_str(), nullptr, 16);
}

string getNotifyHashStr(const uint256 &hash) {
  // we need to convert to little-endian
  // 00000000000000000328e9fea9914ad83b7404a838aa66aefb970e5689c2f63d
  // 89c2f63dfb970e5638aa66ae3b7404a8a9914ad80328e9fe0000000000000000
  string str;
  for (int i = 0; i < 8; i++) {
    uint32_t a = *(uint32_t *)(BEGIN(hash) + i * 4);
    str += HexStr(BEGIN(a), END(a));
  }
  return str;
}

string getNotifyUint32Str(const uint32_t var) {
  string str;

  uint32_t a = *(uint32_t *)(BEGIN(var));
  str += HexStr(BEGIN(a), END(a));

  return str;
}

uint256 SwapUint(const uint256 &hash) {
  uint256 h;
  for (int i = 0; i < 8; i++) {
    uint32_t a = *(uint32_t *)(BEGIN(hash) + i * 4);
    *(uint32_t *)(BEGIN(h) + (7 - i) * 4) = SwapUint(a);
  }
  return h;
}

uint256 reverse8bit(uint256 &&hash) {
  for (char *i = BEGIN(hash), *j = i + 31; i < j; i++, j--) {
    char tmp = *i;
    *i = *j;
    *j = tmp;
  }
  return hash;
}

uint256 reverse32bit(uint256 &&hash) {
  for (uint32_t *i = (uint32_t *)BEGIN(hash), *j = i + 7; i < j; i++, j--) {
    uint32_t tmp = *i;
    *i = *j;
    *j = tmp;
  }
  return hash;
}

string reverse16bit(string &&hash) {
  for (uint16_t *i = (uint16_t *)hash.data(), *j = i + (hash.size() / 2) - 1;
       i < j;
       i++, j--) {
    uint16_t tmp = *i;
    *i = *j;
    *j = tmp;
  }
  return std::move(hash);
}

string reverse16bit(const string &hash) {
  string newHash = hash;
  return reverse16bit(std::move(newHash));
}
