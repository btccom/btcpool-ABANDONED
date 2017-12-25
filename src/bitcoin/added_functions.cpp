#include "added_functions.h"

std::string EncodeHexBlock(const CBlock &block) {
  CDataStream ssBlock(SER_NETWORK, PROTOCOL_VERSION);
  ssBlock << block;
  return HexStr(ssBlock.begin(), ssBlock.end());
}

CAmount GetBlockSubsidy(int nHeight, const Consensus::Params& consensusParams)
{
  int halvings = nHeight / consensusParams.nSubsidyHalvingInterval;
  // Force block reward to zero when right shift is undefined.
  if (halvings >= 64)
    return 0;

  CAmount nSubsidy = 50 * COIN;
  // Subsidy is cut in half every 210,000 blocks which will occur approximately every 4 years.
  nSubsidy >>= halvings;
  return nSubsidy;
}
