#include "CreateStratumServerTemp.h"

#include "StratumServer.h"
#include "bitcoin/StratumServerBitcoin.h"
#include "eth/StratumServerEth.h"
#include "bytom/StratumServerBytom.h"
#include "sia/StratumServerSia.h"

Server* createStratumServer(std::string type, const int32_t shareAvgSeconds) {
  LOG(INFO) << "createServer type: " << type << ", shareAvgSeconds: " << shareAvgSeconds;
  if ("BTC" == type)
    return new ServerBitcoin(shareAvgSeconds);
  else if ("ETH" == type)
    return new ServerEth(shareAvgSeconds);
  else if ("SIA" == type)
    return new ServerSia(shareAvgSeconds);
  else if ("BYTOM" == type) 
    return new ServerBytom (shareAvgSeconds);
  return nullptr;
}

