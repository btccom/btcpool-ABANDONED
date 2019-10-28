/*
 The MIT License (MIT)

 Copyright (c) [2019] [BTC.COM]

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

#include "StratumServerStats.h"

#include "StratumServer.h"

#include "prometheus/Metric.h"
#include "StratumSession.h"

#include <boost/algorithm/string/case_conv.hpp>
#include <algorithm>

static std::string FormatStratumStatus(int32_t status) {
  std::string result = StratumStatus::toString(status);
  std::replace(result.begin(), result.end(), ' ', '_');
  boost::algorithm::to_lower(result);
  return filterWorkerName(result);
}

static const char *FormatSessionStatus(int status) {
  switch (status) {
  case StratumSession::CONNECTED:
    return "connected";
  case StratumSession::SUBSCRIBED:
    return "subscribed";
  case StratumSession::AUTO_REGISTING:
    return "registering";
  case StratumSession::AUTHENTICATED:
    return "authenticated";
  default:
    return "unknown";
  }
}

StratumServerStats::StratumServerStats(StratumServer &server)
  : server_{server}
  , lastScrape_{std::chrono::steady_clock::now()} {
  metrics_.push_back(prometheus::CreateMetricFn(
      "sserver_identity",
      prometheus::Metric::Type::Gauge,
      "Identity number of sserver",
      {},
      [this]() { return server_.serverId_; }));

  for (auto &chain : server_.chains_) {
    metrics_.push_back(prometheus::CreateMetricFn(
        "sserver_idle_since_last_job_broadcast_seconds",
        prometheus::Metric::Type::Gauge,
        "Idle time since last sserver job broadcast in seconds",
        {{"chain", chain.name_}},
        [&chain]() {
          return time(nullptr) - chain.jobRepository_->lastJobSendTime_;
        }));
    metrics_.push_back(prometheus::CreateMetricFn(
        "sserver_last_job_broadcast_height",
        prometheus::Metric::Type::Gauge,
        "Block height of last sserver job broadcast",
        {{"chain", chain.name_}},
        [&chain]() { return chain.jobRepository_->lastJobHeight_; }));
  }
}

std::vector<std::shared_ptr<prometheus::Metric>>
StratumServerStats::collectMetrics() {
  auto scrape = std::chrono::steady_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::seconds>(scrape - lastScrape_)
          .count();
  lastScrape_ = scrape;

  std::vector<std::shared_ptr<prometheus::Metric>> metrics = metrics_;
  for (size_t i = 0; i < server_.chains_.size(); i++) {
    auto const &chain = server_.chains_[i];
    auto &lastStats = lastShareStats_[i];
    for (auto p : chain.shareStats_) {
      metrics.push_back(prometheus::CreateMetricValue(
          "sserver_shares_per_second_since_last_scrape",
          prometheus::Metric::Type::Gauge,
          "Shares processed by sserver per second since last scrape",
          {{"chain", chain.name_}, {"status", FormatStratumStatus(p.first)}},
          static_cast<double>(p.second - lastStats[p.first]) / duration));
    }
    lastShareStats_[i] = chain.shareStats_;
  }

  std::map<std::pair<size_t, StratumSession::State>, size_t> sessions;
  for (auto &session : server_.connections_) {
    ++sessions[{session->getChainId(), session->getState()}];
  }
  for (auto &s : sessions) {
    metrics.push_back(prometheus::CreateMetricValue(
        "sserver_sessions_total",
        prometheus::Metric::Type::Gauge,
        "The number of sserver sessions per chain and status",
        {{"chain", server_.chains_[s.first.first].name_},
         {"status", FormatSessionStatus(s.first.second)}},
        s.second));
  }

  return metrics;
}
