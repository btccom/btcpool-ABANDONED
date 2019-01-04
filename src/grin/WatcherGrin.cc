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

#include "WatcherGrin.h"

#include "StratumGrin.h"

#include "utilities_js.hpp"

ClientContainerGrin::ClientContainerGrin(const libconfig::Config &config) : ClientContainer(config) {}

PoolWatchClient* ClientContainerGrin::createPoolWatchClient(const libconfig::Setting &config) {
  return new PoolWatchClientGrin{base_, this, config};
}

bool ClientContainerGrin::initInternal() {
  return true;
}

bool ClientContainerGrin::sendJobToKafka(const StratumJobGrin &job, PoolWatchClientGrin *client) {
  // Find the client for the job
  std::weak_ptr<PoolWatchClient> clientPtr;
  for (auto &p : clients_) {
    if (p.get() == client) {
      clientPtr = p;
      break;
    }
  }

  if (clientPtr.expired()) {
    LOG(ERROR) << "discard a job that its client has been destroyed: " << job.serializeToJson();
    return false;
  }

  // Submit to Kafka
  string jobStr = job.serializeToJson();
  kafkaProducer_.produce(jobStr.c_str(), jobStr.size());
  LOG(INFO) << "sumbit job to Kafka: " << jobStr;

  // Job cache management
  std::lock_guard<std::mutex> lock{jobCacheLock_};
  jobCache_.push(job.jobId_);
  jobClients_[job.jobId_] = clientPtr;
  while (jobClients_.size() > kMaxJobCacheSize_) {
    auto itr = jobClients_.find(jobCache_.front());
    jobCache_.pop();
    if (itr != jobClients_.end()) {
      jobClients_.erase(itr);
    }
  }

  return true;
}

PoolWatchClientGrin::PoolWatchClientGrin(
  struct event_base *base,
  ClientContainer *container,
  const libconfig::Setting &config) : PoolWatchClient(base, container, config) {}

void PoolWatchClientGrin::onConnected() {
  // Grin node does not yet implement login method and jobs are pushed automatically once connected
  state_ = SUBSCRIBED;
}

void PoolWatchClientGrin::handleStratumMessage(const string &line) {
  DLOG(INFO) << "<" << poolName_ << "> recv(" << line.size() << "): " << line;

  JsonNode jnode;
  if (!JsonNode::parse(line.data(), line.data() + line.size(), jnode)) {
    LOG(ERROR) << "decode line fail, not a json string";
    return;
  }
  auto jmethod = jnode["method"];
  auto jparams = jnode["params"];

  if (jmethod.type() == Utilities::JS::type::Str) {
    if (jmethod.str() == "job") {
      StratumJobGrin sjob;
      if (!sjob.initFromRawJob(jparams)) {
        LOG(ERROR) << "<" << poolName_ << "> init stratum job failed, " << "raw job: " << line;
      } else {
        static_cast<ClientContainerGrin *>(container_)->sendJobToKafka(sjob, this);
      }
    }
  }
}