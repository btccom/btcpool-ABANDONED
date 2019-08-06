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
  , poolDB_(MysqlConnectInfo(
        config.lookup("pooldb.host").c_str(),
        (int)config.lookup("pooldb.port"),
        config.lookup("pooldb.username").c_str(),
        config.lookup("pooldb.password").c_str(),
        config.lookup("pooldb.dbname").c_str()))
  , kafkaSolvedShareConsumer_(
        kafkaBrokers_.c_str(),
        config.lookup("poolwatcher.solved_share_topic").c_str(),
        0 /*patition*/) {
}

ClientContainerBeam::~ClientContainerBeam() {
}

bool ClientContainerBeam::initInternal() {
  // we need to consume the latest few
  const int32_t kConsumeLatestN = 5;

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
      std::thread(&ClientContainerBeam::runThreadSolvedShareConsume, this);
  return true;
}

void ClientContainerBeam::runThreadSolvedShareConsume() {
  LOG(INFO) << "waiting for stratum jobs...";
  for (;;) {
    {
      std::lock_guard<std::mutex> lock(jobCacheLock_);
      if (jobCacheMap_.size() > 0) {
        break;
      }
    }
    std::this_thread::sleep_for(1s);
  }

  LOG(INFO) << "start solved share consume thread";

  const int32_t kTimeoutMs = 1000;

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

void ClientContainerBeam::consumeSolvedShare(rd_kafka_message_t *rkmessage) {
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

  if (jroot["input"].type() != Utilities::JS::type::Str ||
      jroot["output"].type() != Utilities::JS::type::Str ||
      jroot["nonce"].type() != Utilities::JS::type::Str) {
    LOG(ERROR) << "solved share json missing fields: " << json;
    return;
  }

  string nonce = jroot["nonce"].str();
  string input = jroot["input"].str();
  string output = jroot["output"].str();
  uint32_t height = jroot["height"].uint32();
  string blockBits = jroot["blockBits"].str();
  uint32_t userId = jroot["userId"].uint32();
  int64_t workerId = jroot["workerId"].int64();
  string workerFullName = jroot["workerFullName"].str();
  string blockHash = jroot["blockHash"].str();

  LOG(INFO) << "received a new solved share, worker: " << workerFullName
            << ", height: " << height << ", blockHash: " << blockHash
            << ", blockBits: " << blockBits << ", input: " << input
            << ", nonce: " << nonce << ", output: " << output;

  std::lock_guard<std::mutex> lock(jobCacheLock_);
  auto itr = jobCacheMap_.find(input);

  if (itr == jobCacheMap_.end()) {
    LOG(WARNING) << "cannot find stratum job of solved share: " << json;
    return;
  }

  const JobCache &job = itr->second;
  string submitJson = Strings::Format(
      "{"
      "\"jsonrpc\":\"2.0\","
      "\"id\":\"%s\","
      "\"method\":\"solution\","
      "\"nonce\":\"%s\","
      "\"output\":\"%s\""
      "}\n",
      job.jobId_,
      nonce,
      output);

  auto client =
      std::dynamic_pointer_cast<PoolWatchClientBeam>(clients_[job.clientId_]);
  if (!client) {
    LOG(ERROR) << "client " << job.clientId_
               << " is not available for the solved share: " << json;
    return;
  }

  // save block to DB
  const string nowStr = date("%F %T");
  string sql = Strings::Format(
      "INSERT INTO `found_blocks`("
      "  `puid`, `worker_id`"
      ", `worker_full_name`"
      ", `height`, `hash`"
      ", `input`, `nonce`"
      ", `rewards`, `block_bits`"
      ", `created_at`) "
      "VALUES("
      "  %d, %d"
      ", '%s'"
      ", %u, '%s'"
      ", '%s', '%s'"
      ", %d"
      ", '%s'"
      ", '%s');",
      userId,
      workerId,
      filterWorkerName(workerFullName),
      height,
      blockHash,
      input,
      nonce,
      (int64_t)Beam_GetStaticBlockReward(height),
      blockBits,
      nowStr);
  poolDB_.addSQL(sql);

  powHashMap_[job.jobId_] = blockHash;
  client->submitShare(submitJson);
  LOG(INFO) << "submit block " << blockHash;
}

PoolWatchClient *
ClientContainerBeam::createPoolWatchClient(const libconfig::Setting &config) {
  return new PoolWatchClientBeam(base_, this, config);
}

void ClientContainerBeam::updateBlockHash(string jobId, string blockHash) {
  string powHash;
  {
    std::lock_guard<std::mutex> lock(jobCacheLock_);
    auto itr = powHashMap_.find(jobId);
    if (itr == powHashMap_.end()) {
      LOG(ERROR) << "Cannot find the powHash of job " << jobId
                 << ". Its blockHash: " << blockHash;
      return;
    }
    powHash = itr->second;
  }

  // update block hash
  const string nowStr = date("%F %T");
  string sql = Strings::Format(
      "UPDATE `found_blocks` SET `hash`='%s', `created_at`='%s' WHERE "
      "`hash`='%s'",
      blockHash,
      nowStr,
      powHash);
  poolDB_.addSQL(sql);
  LOG(INFO) << "update block hash from " << powHash << " to " << blockHash;
}

bool ClientContainerBeam::sendJobToKafka(
    const string jobId,
    const StratumJobBeam &job,
    PoolWatchClientBeam *client) {
  // find job's client id
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

  // submit to Kafka
  string jobStr = job.serializeToJson();
  kafkaProducer_.produce(jobStr.data(), jobStr.size());

  LOG(INFO) << "sumbit to Kafka, msg len: " << jobStr.size();
  LOG(INFO) << "new job: " << jobStr;

  // add to job cache
  std::lock_guard<std::mutex> lock(jobCacheLock_);
  jobCacheMap_[job.input_] = {jobId, job, clientId};

  // clear job cache
  jobCacheMap_.clear(kMaxJobCacheSize_);
  powHashMap_.clear(kMaxPowHashSize_);

  return true;
}

///////////////////////////////// PoolWatchClient //////////////////////////////
PoolWatchClientBeam::PoolWatchClientBeam(
    struct event_base *base,
    ClientContainerBeam *container,
    const libconfig::Setting &config)
  : PoolWatchClient(base, container, config) {
}

PoolWatchClientBeam::~PoolWatchClientBeam() {
}

void PoolWatchClientBeam::onConnected() {
  string s = Strings::Format(
      "{"
      "\"id\":\"login\","
      "\"jsonrpc\":\"2.0\","
      "\"method\":\"login\","
      "\"api_key\":\"%s\""
      "}\n",
      workerName_);
  sendData(s);
  state_ = SUBSCRIBED;
}

void PoolWatchClientBeam::submitShare(string submitJson) {
  {
    std::lock_guard<std::mutex> lock(wantSubmittedSharesLock_);
    if (state_ != AUTHENTICATED) {
      LOG(INFO) << "<" << poolName_
                << "> client is not ready, submission will be delayed";
      wantSubmittedShares_ += submitJson;
      return;
    }
  }

  sendData(submitJson);
  LOG(INFO) << "<" << poolName_ << "> submit solution: " << submitJson;
}

void PoolWatchClientBeam::handleStratumMessage(const string &line) {
  DLOG(INFO) << "<" << poolName_ << "> UpPoolWatchClient recv(" << line.size()
             << "): " << line;

  auto containerBeam = GetContainerBeam();

  JsonNode jnode;
  if (!JsonNode::parse(line.data(), line.data() + line.size(), jnode)) {
    LOG(ERROR) << "decode line fail, not a json string";
    return;
  }
  JsonNode jid = jnode["id"];
  JsonNode jmethod = jnode["method"];

  if (state_ == SUBSCRIBED) {
    if (jid.type() == Utilities::JS::type::Str && jid.str() == "login" &&
        jnode["code"].type() == Utilities::JS::type::Int &&
        jnode["code"].int64() != 0) {
      // authorize failed
      LOG(ERROR) << "<" << poolName_ << "> auth failed, name: \"" << workerName_
                 << "\", "
                 << "response: " << line;
      // close connection
      bufferevent_free(bev_);
      return;
    }
  }

  if (jmethod.type() == Utilities::JS::type::Str) {
    if (jmethod.str() == "job") {
      if (state_ == SUBSCRIBED) {
        std::lock_guard<std::mutex> lock(wantSubmittedSharesLock_);

        // The beam node will not send a success response for the authentication
        // request, so the first job notify is used as a sign of successful
        // authentication.
        state_ = AUTHENTICATED;

        if (!wantSubmittedShares_.empty()) {
          sendData(wantSubmittedShares_);
          LOG(INFO) << "<" << poolName_
                    << "> submit delayed solution: " << wantSubmittedShares_;
          wantSubmittedShares_.clear();
        }
      }

      StratumJobBeam sjob;
      if (!sjob.initFromRawJob(
              line,
              Strings::Format("%s:%u", poolHost_, poolPort_),
              workerName_)) {
        LOG(ERROR) << "<" << poolName_ << "> init stratum job failed, "
                   << "raw job: " << line;
      }
      sjob.jobId_ = container_->generateJobId();

      containerBeam->sendJobToKafka(jid.str(), sjob, this);
      return;
    }
  }

  LOG(INFO) << "<" << poolName_ << "> recv(" << line.size() << "): " << line;
  if (jnode["blockhash"].type() == Utilities::JS::type::Str &&
      jid.type() == Utilities::JS::type::Str) {
    string jobId = jid.str();
    string blockHash = jnode["blockhash"].str();
    containerBeam->updateBlockHash(jobId, blockHash);
  }
}
