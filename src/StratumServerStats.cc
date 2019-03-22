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

StratumServerStats::StratumServerStats(StratumServer &server)
  : server_{server} {
  metrics_.push_back(prometheus::CreateMetric(
      "sserver_identity",
      prometheus::Metric::Type::Gauge,
      "Identity number of sserver",
      {},
      [this]() { return server_.serverId_; }));
  metrics_.push_back(prometheus::CreateMetric(
      "sserver_sessions_total",
      prometheus::Metric::Type::Gauge,
      "Total number of sserver sessions",
      {},
      [this]() { return server_.connections_.size(); }));

  for (auto &chain : server_.chains_) {
    metrics_.push_back(prometheus::CreateMetric(
        "sserver_idle_since_last_job_broadcast_seconds",
        prometheus::Metric::Type::Gauge,
        "Idle time since last sserver job broadcast in seconds",
        {{"chain", chain.name_}},
        [&chain]() {
          return time(nullptr) - chain.jobRepository_->lastJobSendTime_;
        }));
    metrics_.push_back(prometheus::CreateMetric(
        "sserver_last_job_broadcast_height",
        prometheus::Metric::Type::Gauge,
        "Block height of last sserver job broadcast",
        {{"chain", chain.name_}},
        [&chain]() { return chain.jobRepository_->lastJobHeight_; }));
  }
}

std::vector<std::shared_ptr<prometheus::Metric>>
StratumServerStats::collectMetrics() {
  return metrics_;
}
