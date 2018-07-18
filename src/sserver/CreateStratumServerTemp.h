#ifndef _CREATE_STRATUM_SERVER_TEMP_H_
#define _CREATE_STRATUM_SERVER_TEMP_H_

#include "common/StratumServer.h"
#include "eth/StratumServerEth.h"
#include "bytom/StratumServerBytom.h"

inline std::shared_ptr<Server> createStratumServer(string type, const int32_t shareAvgSeconds) {
  LOG(INFO) << "createServer type: " << type << ", shareAvgSeconds: " << shareAvgSeconds;
  if ("BTC" == type)
    return make_shared<Server> (shareAvgSeconds);
  else if ("ETH" == type)
    return make_shared<ServerEth> (shareAvgSeconds);
  else if ("SIA" == type)
    return make_shared<ServerSia> (shareAvgSeconds);
  else if ("BYTOM" == type) 
    return make_shared<ServerBytom> (shareAvgSeconds);
  return std::shared_ptr<Server>();
}


#endif