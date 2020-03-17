#pragma once

#include "StratumClient.h"

class StratumClientTellor : public StratumClient {
public:
  StratumClientTellor(
      bool enableTLS,
      struct event_base *base,
      const string &workerFullName,
      const string &workerPasswd,
      const libconfig::Config &config);

  string constructShare();
  string challenge_;
  string public_address_;
  uint64_t nonce_;
  uint64_t difficulty_;

protected:
  virtual void handleLine(const string &line);
};