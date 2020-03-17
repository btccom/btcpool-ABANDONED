#pragma once

#include "StratumTellor.h"
#include "StratumSession.h"
#include "StratumServerTellor.h"

class StratumSessionTellor : public StratumSessionBase<StratumTraitsTellor> {
public:
  StratumSessionTellor(
      StratumServerTellor &server,
      struct bufferevent *bev,
      struct sockaddr *saddr,
      uint32_t sessionId);

  void sendSetDifficulty(LocalJob &localJob, uint64_t difficulty) override;
  void
  sendMiningNotify(shared_ptr<StratumJobEx> exJobPtr, bool isFirstJob) override;

  std::unique_ptr<StratumMiner> createMiner(
      const std::string &clientAgent,
      const std::string &workerName,
      int64_t workerId) override;

protected:
  void handleRequest(
      const std::string &idStr,
      const std::string &method,
      const JsonNode &jparams,
      const JsonNode &jroot) override;

  bool validate(
      const JsonNode &jmethod,
      const JsonNode &jparams,
      const JsonNode &jroot) override;

private:
  void
  handleRequest_Subscribe(const std::string &idStr, const JsonNode &jparams);

  void
  handleRequest_Authorize(const std::string &idStr, const JsonNode &jparams);

  void handleRequest_Extranonce_Subscribe(
      const string &idStr, const JsonNode &jparams);

  uint64_t currentDifficulty_;
};
