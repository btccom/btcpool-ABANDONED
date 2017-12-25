#include <string>

#include "core_io.h"
#include "streams.h"
#include "amount.h"
#include "chainparams.h"
#include "utilstrencodings.h"

/* These functions has missing from bitcoinABC, so copied them 
   from btcpool and pasted to here. */
   
std::string EncodeHexBlock(const CBlock &block);

CAmount GetBlockSubsidy(int nHeight, const Consensus::Params& consensusParams);
