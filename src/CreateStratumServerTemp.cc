#include "CreateStratumServerTemp.h"

#include "StratumServer.h"
#include "bitcoin/StratumServerBitcoin.h"
#include "eth/StratumServerEth.h"
#include "bytom/StratumServerBytom.h"
#include "sia/StratumServerSia.h"
#include "decred/StratumServerDecred.h"

Server *createStratumServer(
    const std::string &type,
    const int32_t shareAvgSeconds,
    const libconfig::Config &config) {
  LOG(INFO) << "createServer type: " << type
            << ", shareAvgSeconds: " << shareAvgSeconds;
#if defined(CHAIN_TYPE_STR)
  if (CHAIN_TYPE_STR == type)
#else
  if (false)
#endif
    return new ServerBitcoin(shareAvgSeconds, config);
  else if ("ETH" == type)
    return new ServerEth(shareAvgSeconds);
  else if ("SIA" == type)
    return new ServerSia(shareAvgSeconds);
  else if ("BTM" == type)
    return new ServerBytom(shareAvgSeconds);
  else if ("DCR" == type)
    return new ServerDecred(shareAvgSeconds, config);
  return nullptr;
}
