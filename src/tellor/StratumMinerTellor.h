#pragma once

#include "StratumTellor.h"
#include "StratumMiner.h"

class StratumMinerTellor : public StratumMinerBase<StratumTraitsTellor> {
public:
  using StratumMiner::kExtraNonce2Size_;
  StratumMinerTellor(
      StratumSessionTellor &session,
      const DiffController &diffController,
      const std::string &clientAgent,
      const std::string &workerName,
      int64_t workerId);

  void handleRequest(
      const std::string &idStr,
      const std::string &method,
      const JsonNode &jparams,
      const JsonNode &jroot) override;
  //[TO DO]
  uint64_t calcCurDiff();

private:
  void handleRequest_Submit(const string &idStr, const JsonNode &jparams);
};