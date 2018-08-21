#include "CreateStratumServerTemp.h"

#include "StratumServer.h"
#include "bitcoin/StratumServerBitcoin.h"
#include "eth/StratumServerEth.h"
#include "bytom/StratumServerBytom.h"
#include "sia/StratumServerSia.h"

Server* createStratumServer(const std::string &type, const int32_t shareAvgSeconds,
                            const std::string &auxPowSolvedShareTopic, /*bitcoin only. TODO: refactor this*/
                            const std::string &rskSolvedShareTopic     /*bitcoin only. TODO: refactor this*/) {
  LOG(INFO) << "createServer type: " << type << ", shareAvgSeconds: " << shareAvgSeconds;
#if defined(CHAIN_TYPE_BTC)
  if ("BTC" == type)
#elif defined(CHAIN_TYPE_BCH)
  if ("BCH" == type)
#elif defined(CHAIN_TYPE_UBTC)
  if ("UBTC" == type)
#elif defined(CHAIN_TYPE_SBTC)
  if ("SBTC" == type)
#else 
  if (false)
#endif
    return new ServerBitcoin(shareAvgSeconds, auxPowSolvedShareTopic, rskSolvedShareTopic);
  else if ("ETH" == type)
    return new ServerEth(shareAvgSeconds);
  else if ("SIA" == type)
    return new ServerSia(shareAvgSeconds);
  else if ("BTM" == type) 
    return new ServerBytom (shareAvgSeconds);
  return nullptr;
}

