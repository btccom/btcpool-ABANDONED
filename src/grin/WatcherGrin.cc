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

#include <sstream>

ClientContainerGrin::ClientContainerGrin(const libconfig::Config &config)
  : ClientContainer{config}
  , kafkaSolvedShareConsumer_{kafkaBrokers_.c_str(),
                              config.lookup("poolwatcher.solved_share_topic")
                                  .c_str(),
                              0 /*patition*/}
  , poolDB_{config.lookup("pooldb.host").c_str(),
            (int)config.lookup("pooldb.port"),
            config.lookup("pooldb.username").c_str(),
            config.lookup("pooldb.password").c_str(),
            config.lookup("pooldb.dbname").c_str()} {
}

PoolWatchClient *
ClientContainerGrin::createPoolWatchClient(const libconfig::Setting &config) {
  return new PoolWatchClientGrin{base_, this, config};
}

bool ClientContainerGrin::initInternal() {
  // we need to consume the latest few
  static const int32_t kConsumeLatestN = 5;

  map<string, string> consumerOptions;
  consumerOptions["fetch.wait.max.ms"] = "10";
  if (kafkaSolvedShareConsumer_.setup(
          RD_KAFKA_OFFSET_TAIL(kConsumeLatestN), &consumerOptions) == false) {
    LOG(INFO) << "setup kafkaSolvedShareConsumer_ fail";
    return false;
  }

  if (!kafkaSolvedShareConsumer_.checkAlive()) {
    LOG(ERROR) << "kafka brokers is not alive";
    return false;
  }

  threadSolvedShareConsume_ =
      std::thread(&ClientContainerGrin::runThreadSolvedShareConsume, this);
  return true;
}

void ClientContainerGrin::runThreadSolvedShareConsume() {
  LOG(INFO) << "waiting for stratum jobs...";
  for (;;) {
    {
      std::lock_guard<std::mutex> lock(jobCacheLock_);
      if (!jobCacheMap_.empty()) {
        break;
      }
    }
    std::this_thread::sleep_for(1s);
  }

  LOG(INFO) << "start solved share consume thread";

  static const int32_t kTimeoutMs = 1000;

  while (running_) {
    rd_kafka_message_t *rkmessage;
    rkmessage = kafkaSolvedShareConsumer_.consumer(kTimeoutMs);

    // timeout, most of time it's not nullptr and set an error:
    //          rkmessage->err == RD_KAFKA_RESP_ERR__PARTITION_EOF
    if (rkmessage == nullptr) {
      continue;
    }

    consumeSolvedShare(rkmessage);

    /* Return message to rdkafka */
    rd_kafka_message_destroy(rkmessage);
  }

  LOG(INFO) << "stop solved share consume thread";
}

void ClientContainerGrin::consumeSolvedShare(rd_kafka_message_t *rkmessage) {
  // check error
  if (rkmessage->err) {
    if (rkmessage->err == RD_KAFKA_RESP_ERR__PARTITION_EOF) {
      // Reached the end of the topic+partition queue on the broker.
      // Not really an error.
      //      LOG(INFO) << "consumer reached end of " <<
      //      rd_kafka_topic_name(rkmessage->rkt)
      //      << "[" << rkmessage->partition << "] "
      //      << " message queue at offset " << rkmessage->offset;
      // acturlly
      return;
    }

    LOG(ERROR) << "consume error for topic "
               << rd_kafka_topic_name(rkmessage->rkt) << "["
               << rkmessage->partition << "] offset " << rkmessage->offset
               << ": " << rd_kafka_message_errstr(rkmessage);

    if (rkmessage->err == RD_KAFKA_RESP_ERR__UNKNOWN_PARTITION ||
        rkmessage->err == RD_KAFKA_RESP_ERR__UNKNOWN_TOPIC) {
      LOG(FATAL) << "consume fatal";
    }
    return;
  }

  string json((const char *)rkmessage->payload, rkmessage->len);
  JsonNode jroot;
  if (!JsonNode::parse(json.c_str(), json.c_str() + json.size(), jroot)) {
    LOG(ERROR) << "cannot parse solved share json: " << json;
    return;
  }

  if (jroot["prePow"].type() != Utilities::JS::type::Str ||
      jroot["height"].type() != Utilities::JS::type::Int ||
      jroot["edgeBits"].type() != Utilities::JS::type::Int ||
      jroot["nonce"].type() != Utilities::JS::type::Int ||
      jroot["proofs"].type() != Utilities::JS::type::Array ||
      jroot["blockHash"].type() != Utilities::JS::type::Str) {
    LOG(ERROR) << "solved share json missing fields: " << json;
    return;
  }

  string prePow = jroot["prePow"].str();
  uint64_t height = jroot["height"].uint64();
  uint32_t edgeBits = jroot["edgeBits"].uint32();
  uint64_t nonce = jroot["nonce"].uint64();
  uint32_t userId = jroot["userId"].uint32();
  int64_t workerId = jroot["workerId"].int64();
  string workerFullName = jroot["workerFullName"].str();
  string blockHash = jroot["blockHash"].str();
  std::ostringstream oss;
  oss << jroot["proofs"];
  auto proofs = oss.str();
  string timestamp;
  if (jroot["timestamp"].type() == Utilities::JS::type::Int) {
    timestamp =
        Strings::Format(",\"timestamp\": %" PRId64, jroot["timestamp"].int64());
  }
  LOG(INFO) << "received a new solved share, worker: " << workerFullName
            << ", prePow: " << prePow << ", height: " << height
            << ", edgeBits: " << edgeBits << ", nonce: " << nonce
            << ", proofs: " << proofs << ", blockHash: " << blockHash;

  std::lock_guard<std::mutex> lock(jobCacheLock_);
  auto itr = jobCacheMap_.find(prePow);

  if (itr == jobCacheMap_.end()) {
    LOG(WARNING) << "cannot find stratum job of solved share: " << json;
    return;
  }

  auto nodeJobId = itr->second.nodeJobId;
  auto client = clients_.at(itr->second.clientId);
  if (!client) {
    LOG(ERROR) << "client for prePow " << prePow
               << " is not available for the solved share: " << json;
    return;
  }

  string submitJson = Strings::Format(
      "{\"id\":\"0\""
      ",\"jsonrpc\":\"2.0\""
      ",\"method\":\"submit\""
      ",\"params\":"
      "{\"edge_bits\":%u,\"height\":%u,\"job_id\":%u"
      ",\"nonce\":%u"
      ",\"pow\":%s"
      "%s}}\n",
      edgeBits,
      height,
      nodeJobId,
      nonce,
      proofs,
      timestamp);

  LOG(INFO) << "submitting block: " << submitJson;
  client->sendData(submitJson);

  // save block to DB
  const string nowStr = date("%F %T");
  string sql = Strings::Format(
      "INSERT INTO `found_blocks`("
      "  `puid`, `worker_id`"
      ", `worker_full_name`"
      ", `height`, `hash`"
      ", `edge_bits`, `nonce`"
      ", `rewards`, `job_id`"
      ", `created_at`) "
      "VALUES("
      "  %d, %d, '%s'"
      ", %u, '%s'"
      ", '%u', '%016x'"
      ", %d, '%u', '%s');",
      userId,
      workerId,
      filterWorkerName(workerFullName),
      height,
      blockHash,
      edgeBits,
      nonce,
      (int64_t)GetBlockRewardGrin(height),
      nodeJobId,
      nowStr);
  std::thread t([this, sql, blockHash]() {
    // try connect to DB
    MySQLConnection db(poolDB_);
    for (size_t i = 0; i < 3; i++) {
      if (db.ping())
        break;
      else
        std::this_thread::sleep_for(3s);
    }

    if (db.execute(sql) == false) {
      LOG(ERROR) << "insert found block failure: " << sql;
      return;
    }

    LOG(INFO) << "insert found block " << blockHash << " success";
  });
  t.detach();
}

bool ClientContainerGrin::sendJobToKafka(
    const StratumJobGrin &job, PoolWatchClientGrin *client) {
  // Find the client for the job
  size_t clientId;
  for (clientId = 0; clientId < clients_.size(); clientId++) {
    if (clients_[clientId].get() == client) {
      break;
    }
  }
  if (clientId >= clients_.size()) {
    LOG(ERROR) << "discard a job that its client has been destroyed: "
               << job.serializeToJson();
    return false;
  }

  // Submit to Kafka
  string jobStr = job.serializeToJson();
  kafkaProducer_.produce(jobStr.c_str(), jobStr.size());
  LOG(INFO) << "sumbit job to Kafka: " << jobStr;

  // Job cache management
  std::lock_guard<std::mutex> lock{jobCacheLock_};
  jobCacheQueue_.push(job.prePowStr_);
  jobCacheMap_[job.prePowStr_] = {job.nodeJobId_, clientId};
  while (jobCacheMap_.size() > kMaxJobCacheSize_) {
    auto itr = jobCacheMap_.find(jobCacheQueue_.front());
    jobCacheQueue_.pop();
    if (itr != jobCacheMap_.end()) {
      jobCacheMap_.erase(itr);
    }
  }

  return true;
}

PoolWatchClientGrin::PoolWatchClientGrin(
    struct event_base *base,
    ClientContainer *container,
    const libconfig::Setting &config)
  : PoolWatchClient(base, container, config) {
}

void PoolWatchClientGrin::onConnected() {
  // Grin node does not yet implement login method and jobs are pushed
  // automatically once connected
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
        LOG(ERROR) << "<" << poolName_ << "> init stratum job failed, "
                   << "raw job: " << line;
      } else {
        sjob.jobId_ = container_->generateJobId();
        static_cast<ClientContainerGrin *>(container_)
            ->sendJobToKafka(sjob, this);
      }
    }
  }
}
