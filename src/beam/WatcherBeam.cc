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
#include "WatcherBeam.h"
#include "utilities_js.hpp"

#include <libconfig.h++>

///////////////////////////////// ClientContainer //////////////////////////////
ClientContainerBeam::ClientContainerBeam(const libconfig::Config &config)
  : ClientContainer(config)
{
  submitRpcBindAddr_ = config.lookup("poolwatcher.submit_rpc_bind_addr").c_str();
  submitRpcBindPort_ = (int)config.lookup("poolwatcher.submit_rpc_bind_port");
  exposedSubmitRpc_  = config.lookup("poolwatcher.exposed_submit_rpc").c_str();
}

ClientContainerBeam::~ClientContainerBeam() 
{
}

bool ClientContainerBeam::initInternal() {
  return true;
}

PoolWatchClient* ClientContainerBeam::createPoolWatchClient(const libconfig::Setting &config) {
  return new PoolWatchClientBeam(base_, this, config);
}

bool ClientContainerBeam::sendJobToKafka(const string jobId, const StratumJobBeam &job, PoolWatchClientBeam *client) {
  // find job's client id
  size_t clientId;
  for (clientId = 0; clientId < clients_.size(); clientId++) {
    if (clients_[clientId].get() == client) {
      break;
    }
  }
  if (clientId >= clients_.size()) {
    LOG(ERROR) << "Drop a job that its client has been destroyed: " << job.serializeToJson();
    return false;
  }

  // submit to Kafka
  string jobStr = job.serializeToJson();
  kafkaProducer_.produce(jobStr.c_str(), jobStr.size());

  LOG(INFO) << "sumbit to Kafka, msg len: " << jobStr.size();
  LOG(INFO) << "new job: " << jobStr;

  // add to job cache
  std::lock_guard<std::mutex> lock(jobCacheLock_);
  jobCacheKeyQ_.push(job.input_);
  jobCacheMap_[job.input_] = {jobId, job, clientId};

  // clear job cache
  while (jobCacheMap_.size() > kMaxJobCacheSize_) {
    auto itr = jobCacheMap_.find(jobCacheKeyQ_.front());
    jobCacheKeyQ_.pop();
    if (itr != jobCacheMap_.end()) {
      jobCacheMap_.erase(itr);
    }
  }

  return true;
}

///////////////////////////////// PoolWatchClient //////////////////////////////
PoolWatchClientBeam::PoolWatchClientBeam(
  struct event_base *base,
  ClientContainerBeam *container,
  const libconfig::Setting &config
)
  : PoolWatchClient(base, container, config)
{
}

PoolWatchClientBeam::~PoolWatchClientBeam() 
{
}

void PoolWatchClientBeam::onConnected() {
  string s = Strings::Format(
    "{"
      "\"id\":\"login\","
      "\"jsonrpc\":\"2.0\","
      "\"method\":\"login\","
      "\"api_key\":\"%s\""
    "}\n",
    workerName_.c_str()
  );
  sendData(s);
  state_ = SUBSCRIBED;
}

void PoolWatchClientBeam::handleStratumMessage(const string &line) {
  DLOG(INFO) << "<" << poolName_ << "> UpPoolWatchClient recv(" << line.size() << "): " << line;

  auto containerBeam = GetContainerBeam();

  JsonNode jnode;
  if (!JsonNode::parse(line.data(), line.data() + line.size(), jnode)) {
    LOG(ERROR) << "decode line fail, not a json string";
    return;
  }
  JsonNode jid     = jnode["id"];
  JsonNode jmethod = jnode["method"];
  
  if (state_ == SUBSCRIBED) {
    if (jid.type() == Utilities::JS::type::Str &&
        jid.str() == "login" &&
        jnode["code"].type() == Utilities::JS::type::Int &&
        jnode["code"].int64() != 0) {
      // authorize failed
      LOG(ERROR) << "<" << poolName_ << "> auth failed, name: \"" << workerName_ << "\", "
                << "response: " << line;
      // close connection
      bufferevent_free(bev_);
      return;
    }
  }

  if (jmethod.type() == Utilities::JS::type::Str) {

    if (jmethod.str() == "job") {
      if (state_ == SUBSCRIBED) {
        // The beam node will not send a success response for the authentication request,
        // so the first job notify is used as a sign of successful authentication.
        state_ = AUTHENTICATED;
      }

      StratumJobBeam sjob;
      if (!sjob.initFromRawJob(line, containerBeam->getExposedSubmitRpc())) {
        LOG(ERROR) << "<" << poolName_ << "> init stratum job failed, "
                   << "raw job: " << line;
      }

      containerBeam->sendJobToKafka(jid.str(), sjob, this);
    }

    return;
  }
}
