/*
 The MIT License (MIT)

 Copyright (c) [2016] [BTC.COM]

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
 */
#ifndef STRATUM_MINER_H_
#define STRATUM_MINER_H_

#include "Statistics.h"
#include "utilities_js.hpp"

#include <cstdint>
#include <map>
#include <memory>

class DiffController;
struct LocalJob;
class IStratumSession;

//////////////////////////////// StratumMiner ////////////////////////////////
class StratumMiner {
protected:
  static const int INVALID_SHARE_SLIDING_WINDOWS_SIZE = 60; // unit: seconds
  static const int64_t INVALID_SHARE_SLIDING_WINDOWS_MAX_LIMIT =
      20; // max number
  StratumMiner(
      IStratumSession &session,
      const DiffController &diffController,
      const std::string &clientAgent,
      const std::string &workerName,
      int64_t workerId);

public:
  static const size_t kExtraNonce1Size_ = 4;
  static const size_t kExtraGrandNonce1Size_ = 4;
  static const size_t kExtraNonce2Size_ = 8;

  virtual ~StratumMiner() = default;
  virtual void handleRequest(
      const std::string &idStr,
      const std::string &method,
      const JsonNode &jparams,
      const JsonNode &jroot) = 0;
  virtual void handleExMessage(
      const std::string &exMessage){}; // No agent support by default
  void setMinDiff(uint64_t minDiff);
  void resetCurDiff(uint64_t curDiff);
  uint64_t getCurDiff() const { return curDiff_; };
  uint64_t calcCurDiff();
  virtual uint64_t addLocalJob(LocalJob &localJob) = 0;
  virtual void removeLocalJob(LocalJob &localJob) = 0;

  const int64_t workerId() { return workerId_; }
  const std::string &workerName() { return workerName_; }
  const std::string &clientAgent() { return clientAgent_; }

protected:
  bool handleShare(
      const std::string &idStr,
      int32_t status,
      uint64_t shareDiff,
      size_t chainId);

  IStratumSession &session_;
  std::unique_ptr<DiffController> diffController_;
  uint64_t curDiff_;
  std::string clientAgent_;
  bool isNiceHashClient_;
  bool overrideDifficulty_;
  std::string workerName_;
  int64_t workerId_;
  // invalid share counter
  StatsWindow<int64_t> invalidSharesCounter_;
  shared_ptr<bool> alive_;
};

template <typename StratumTraits>
class StratumMinerBase : public StratumMiner {
  using SessionType = typename StratumTraits::SessionType;
  using JobDiffType = typename StratumTraits::JobDiffType;

public:
  using LocalShareType = typename StratumTraits::LocalShareType;

protected:
  StratumMinerBase(
      SessionType &session,
      const DiffController &diffController,
      const std::string &clientAgent,
      const std::string &workerName,
      int64_t workerId)
    : StratumMiner(session, diffController, clientAgent, workerName, workerId) {
    for (auto &localJob : session.getLocalJobs()) {
      addLocalJob(localJob);
    }
  }

public:
  SessionType &getSession() const {
    return static_cast<SessionType &>(session_);
  }

  uint64_t addLocalJob(LocalJob &localJob) override {
    uint64_t curDiff = calcCurDiff();
    // Overload the assignment operator of JobDiffType to add customizations
    jobDiffs_[&localJob] = curDiff;
    return curDiff;
  }

  void removeLocalJob(LocalJob &localJob) override {
    jobDiffs_.erase(&localJob);
  }

protected:
  std::map<const LocalJob *, JobDiffType> jobDiffs_;
};

#endif // #define STRATUM_MINER_H_
