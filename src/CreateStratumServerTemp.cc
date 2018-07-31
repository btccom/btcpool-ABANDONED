#include "CreateStratumServerTemp.h"

#include "StratumServer.h"
#include "bitcoin/StratumServerBitcoin.h"
#include "eth/StratumServerEth.h"
#include "bytom/StratumServerBytom.h"
#include "sia/StratumServerSia.h"

Server* createStratumServer(std::string type, const int32_t shareAvgSeconds) {
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
    return new ServerBitcoin(shareAvgSeconds);
  else if ("ETH" == type)
    return new ServerEth(shareAvgSeconds);
  else if ("SIA" == type)
    return new ServerSia(shareAvgSeconds);
  else if ("BTM" == type) 
    return new ServerBytom (shareAvgSeconds);
  return nullptr;
}

