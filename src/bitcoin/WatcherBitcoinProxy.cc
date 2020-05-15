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
#include "WatcherBitcoinProxy.h"
#include "utilities_js.hpp"
#include "BitcoinUtils.h"

#include <libconfig.h++>

///////////////////////////////// ClientContainer //////////////////////////////
ClientContainerBitcoinProxy::ClientContainerBitcoinProxy(
    const libconfig::Config &config)
  : ClientContainer(config)
  , kafkaSolvedShareConsumer_(
        kafkaBrokers_.c_str(),
        config.lookup("poolwatcher.solved_share_topic").c_str(),
        0 /*patition*/) {

  bool enableSharelogger = false;
  config.lookupValue("solved_sharelog_writer.enabled", enableSharelogger);

  config.lookupValue(
      "poolwatcher.pool_inactive_interval", poolInactiveInterval);

  if (enableSharelogger) {
    shareLogWriter = std::make_unique<ShareLogWriterBase<ShareBitcoin>>(
        config.lookup("poolwatcher.type").c_str(),
        config.lookup("solved_sharelog_writer.data_dir").c_str(),
        config.lookup("solved_sharelog_writer.compression_level"));
  }

  grandPoolEnabled_ = false;
  config.lookupValue("poolwatcher.grandPoolEnabled", grandPoolEnabled_);
}

ClientContainerBitcoinProxy::~ClientContainerBitcoinProxy() {
}

bool ClientContainerBitcoinProxy::initInternal() {
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

  threadSolvedShareConsume_ = std::thread(
      &ClientContainerBitcoinProxy::runThreadSolvedShareConsume, this);
  return true;
}

void ClientContainerBitcoinProxy::runThreadSolvedShareConsume() {
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
      tryFlushSolvedShares();
      continue;
    }

    consumeSolvedShare(rkmessage);

    /* Return message to rdkafka */
    rd_kafka_message_destroy(rkmessage);
  }

  LOG(INFO) << "stop solved share consume thread";
}

void ClientContainerBitcoinProxy::consumeSolvedShare(
    rd_kafka_message_t *rkmessage) {
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

  //
  // solved share message:  FoundBlock + coinbase_Tx
  //
  FoundBlock foundBlock;
  CBlockHeader blkHeader;
  vector<char> coinbaseTxBin;

  {
    if (rkmessage->len <= sizeof(FoundBlock)) {
      LOG(ERROR) << "invalid SolvedShare length: " << rkmessage->len;
      return;
    }
    coinbaseTxBin.resize(rkmessage->len - sizeof(FoundBlock));

    // foundBlock
    memcpy(
        (uint8_t *)&foundBlock,
        (const uint8_t *)rkmessage->payload,
        sizeof(FoundBlock));

    // coinbase tx
    memcpy(
        (uint8_t *)coinbaseTxBin.data(),
        (const uint8_t *)rkmessage->payload + sizeof(FoundBlock),
        coinbaseTxBin.size());

    // copy header
    foundBlock.headerData_.get(blkHeader);
  }

  string workerFullName = foundBlock.workerFullName_;

  LOG(INFO) << "received a new solved share, worker: " << workerFullName
            << ", height: " << foundBlock.height_
            << ", jobId: " << foundBlock.jobId_
#ifdef CHAIN_TYPE_ZEC
            << ", nonce: " << blkHeader.nNonce.ToString();
#else
            << ", nonce: " << blkHeader.nNonce;
#endif

  JobCache jobCache;
  {
    std::lock_guard<std::mutex> lock(jobCacheLock_);
    auto itr = jobCacheMap_.find(foundBlock.jobId_);

    if (itr != jobCacheMap_.end()) {
      jobCache = itr->second;
    } else {
      // find the same job with different times
      uint64_t jobHash = foundBlock.jobId_ & 0xFFFFFFFF;
      auto ritr = jobCacheMap_.rbegin();
      for (; ritr != jobCacheMap_.rend(); ritr++) {
        if ((ritr->second.sJob_.jobId_ & 0xFFFFFFFF) == jobHash) {
          jobCache = ritr->second;
          break;
        }
      }
      if (ritr == jobCacheMap_.rend()) {
        LOG(WARNING) << "cannot find stratum job of solved share: "
                     << foundBlock.jobId_;
        return;
      }
    }
  }

#ifdef CHAIN_TYPE_ZEC

  uint32_t versionMask = 0;
  string solutionHex;
  Bin2Hex(blkHeader.nSolution, solutionHex);

  string submitJson = Strings::Format(
      "{\"params\":[\"%s\",%s,\"%08x\",\"%s\",\"%s\"]"
      ",\"id\":\"987\",\"method\":\"mining.submit\"}\n",
      filterWorkerName(workerFullName),
      jobCache.upstreamJobId_,
      blkHeader.nTime,
      blkHeader.nNonce.ToString(),
      solutionHex);

#else

  size_t coinbase1Size = jobCache.sJob_.coinbase1_.size() / 2;
  if (coinbaseTxBin.size() <
      coinbase1Size + jobCache.sJob_.proxyExtraNonce2Size_) {
    LOG(WARNING) << "cannot find ExtraNonce2, too short coinbase tx";
    return;
  }
  string extraNonce2;
  Bin2Hex(
      (const uint8_t *)(coinbaseTxBin.data() + coinbase1Size),
      jobCache.sJob_.proxyExtraNonce2Size_,
      extraNonce2);

  string extraGrandNonce1;
  if (grandPoolEnabled_) { // 4+8
    Bin2Hex(
        (const uint8_t *)(coinbaseTxBin.data() + coinbase1Size),
        4,
        extraGrandNonce1);
    Bin2Hex(
        (const uint8_t *)(coinbaseTxBin.data() + coinbase1Size + 4),
        jobCache.sJob_.proxyExtraNonce2Size_ - 4,
        extraNonce2);
  }

  uint32_t versionMask = jobCache.sJob_.nVersion_ ^ blkHeader.nVersion;
  string versionMaskJson;
  if (versionMask != 0) {
    versionMaskJson = Strings::Format(",\"%08x\"", versionMask);
  }

  string submitJson = Strings::Format(
      "{\"params\":[\"%s\",%s,\"%s\",\"%08x\",\"%08x\"%s]"
      ",\"id\":\"987\",\"method\":\"mining.submit\"}\n",
      grandPoolEnabled_ ? extraGrandNonce1 : filterWorkerName(workerFullName),
      jobCache.upstreamJobId_,
      extraNonce2,
      blkHeader.nTime,
      blkHeader.nNonce,
      versionMaskJson);

#endif

  auto client = std::dynamic_pointer_cast<PoolWatchClientBitcoinProxy>(
      clients_[jobCache.clientId_]);
  if (!client) {
    LOG(ERROR) << "client " << jobCache.clientId_
               << " is not available for the solved share: " << submitJson;
    return;
  }

  client->submitShare(submitJson);

  if (shareLogWriter) {
    ShareBitcoin share;
    share.set_version(ShareBitcoin::CURRENT_VERSION);
    share.set_jobid(jobCache.sJob_.jobId_);
    share.set_workerhashid(foundBlock.workerId_);
    share.set_userid(foundBlock.userId_);
    share.set_sharediff(jobCache.sJob_.proxyJobDifficulty_);
    share.set_blkbits(blkHeader.nBits);
    share.set_timestamp((uint64_t)time(nullptr));
    share.set_height(foundBlock.height_);
    share.set_versionmask(versionMask);
    share.set_sessionid(jobCache.clientId_);
    share.set_status(StratumStatus::ACCEPT);
    share.set_ip("127.0.0.1");
// set nonce
#ifdef CHAIN_TYPE_ZEC
    uint32_t nonceHash = djb2(blkHeader.nNonce.ToString().c_str());
    share.set_nonce(nonceHash);
#else
    share.set_nonce(blkHeader.nNonce);
#endif

    shareLogWriter->addShare(std::move(share));
    tryFlushSolvedShares();
  }
}

void ClientContainerBitcoinProxy::tryFlushSolvedShares() {
  if (shareLogWriter) {
    time_t now = time(nullptr);
    if (now - lastFlushTime_ > kFlushDiskInterval) {
      shareLogWriter->flushToDisk();
      lastFlushTime_ = now;
    }
  }
}

PoolWatchClient *ClientContainerBitcoinProxy::createPoolWatchClient(
    const libconfig::Setting &config) {
  return new PoolWatchClientBitcoinProxy(base_, this, config);
}

bool ClientContainerBitcoinProxy::sendJobToKafka(
    const string upstreamJobId,
    const StratumJobBitcoin &job,
    PoolWatchClientBitcoinProxy *client) {
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

  time_t now = time(nullptr);
  if (clientId > lastJobClient_ && now - lastJobTime_ < poolInactiveInterval) {
    LOG(INFO) << "skip a job from low priority pool <" << client->poolName_
              << ">, height: " << job.height_
              << ", prev hash: " << job.prevHash_.ToString();
    return false;
  }

  std::lock_guard<std::mutex> lock(jobCacheLock_);
  if (jobGbtHashSet_.find(job.gbtHash_) != jobGbtHashSet_.end()) {
    LOG(INFO) << "ignore duplicated job, gbtHash: " << job.gbtHash_;
    return false;
  }

  // submit to Kafka
  string jobStr = job.serializeToJson();
  kafkaProducer_.produce(jobStr.data(), jobStr.size());

  LOG(INFO) << "sumbit to Kafka, msg len: " << jobStr.size();
  LOG(INFO) << "new job " << upstreamJobId << ": " << jobStr;

  // add to job cache
  jobCacheMap_[job.jobId_] = {upstreamJobId, job, clientId};
  jobGbtHashSet_.insert(job.gbtHash_);

  lastJobClient_ = clientId;
  lastJobTime_ = now;

  // clear job cache
  while (jobCacheMap_.size() > kMaxJobCacheSize_) {
    auto itr = jobCacheMap_.begin();
    jobGbtHashSet_.erase(itr->second.sJob_.gbtHash_);
    jobCacheMap_.erase(itr);
  }

  return true;
}

///////////////////////////////// PoolWatchClient //////////////////////////////
PoolWatchClientBitcoinProxy::PoolWatchClientBitcoinProxy(
    struct event_base *base,
    ClientContainerBitcoinProxy *container,
    const libconfig::Setting &config)
  : PoolWatchClient(base, container, config)
  , currentDifficulty_(0) {
  config.lookupValue("passwd", passwd_);
  grandPoolEnabled_ = container->isGrandPoolEnabled();
}

PoolWatchClientBitcoinProxy::~PoolWatchClientBitcoinProxy() {
}

void PoolWatchClientBitcoinProxy::onConnected() {
  string s = Strings::Format(
      "{\"id\":1,\"method\":\"mining.subscribe\""
      ",\"params\":[\"%s\"]}\n",
      grandPoolEnabled_ ? BTCCOM_GRANDPOOL_WATCHER_AGENT
                        : BTCCOM_WATCHER_AGENT);
  sendData(s);
}

void PoolWatchClientBitcoinProxy::submitShare(string submitJson) {
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

void PoolWatchClientBitcoinProxy::handleStratumMessage(const string &line) {
  DLOG(INFO) << "<" << poolName_ << "> UpPoolWatchClient recv(" << line.size()
             << "): " << line;

  auto containerBitcoin = GetContainerBitcoinProxy();

  JsonNode jnode;
  if (!JsonNode::parse(line.data(), line.data() + line.size(), jnode)) {
    LOG(ERROR) << "decode line fail, not a json string";
    return;
  };
  JsonNode jresult = jnode["result"];
  JsonNode jerror = jnode["error"];
  JsonNode jmethod = jnode["method"];

  if (jmethod.type() == Utilities::JS::type::Str &&
      jnode["params"].type() == Utilities::JS::type::Array) {
    JsonNode jparams = jnode["params"];
    auto jparamsArr = jparams.array();

    if (jmethod.str() == "mining.notify") {
      StratumJobBitcoin sjob;
      if (!sjob.initFromStratumJob(
              jparamsArr, currentDifficulty_, extraNonce1_, extraNonce2Size_)) {
        LOG(WARNING) << "init job failed: " << line;
        return;
      }

      sjob.jobId_ = container_->generateJobId();
      string upstreamJobId = "null";
      if (jparamsArr[0].type() == Utilities::JS::type::Str) {
        upstreamJobId = "\"" + jparamsArr[0].str() + "\"";
      } else if (jparamsArr[0].type() == Utilities::JS::type::Int) {
        upstreamJobId = jparamsArr[0].str();
      }
      containerBitcoin->sendJobToKafka(upstreamJobId, sjob, this);

    } else if (jmethod.str() == "mining.set_difficulty") {
      if (jparamsArr.size() >= 1) {
        currentDifficulty_ = jparamsArr[0].uint64();
        LOG(INFO) << "difficulty changed: " << currentDifficulty_;
      }

    } else {
      LOG(INFO) << "<" << poolName_ << "> recv(" << line.size()
                << "): " << line;
    }
    return;
  }

  if (state_ == CONNECTED) {
    //
    // {"id":1,"result":[[["mining.set_difficulty","01000002"],
    //                    ["mining.notify","01000002"]],"01000002",8],"error":null}
    //
    if (jerror.type() != Utilities::JS::type::Null) {
      LOG(ERROR) << "<" << poolName_
                 << "> json result is null, err: " << jerror.str()
                 << ", response: " << line;
      return;
    }
    std::vector<JsonNode> resArr = jresult.array();
    if (resArr.size() < 3) {
      LOG(ERROR) << "<" << poolName_
                 << "> result element's number is less than 3: " << line;
      return;
    }

    extraNonce1_ = resArr[1].str();
    extraNonce2Size_ = resArr[2].uint32();
    LOG(INFO) << "<" << poolName_ << "> extraNonce1: " << extraNonce1_
              << ", extraNonce2 Size: " << extraNonce2Size_;

    if (extraNonce2Size_ < 8) {
      LOG(ERROR) << "Too short extraNonce2, it should be 8 bytes or longer";
    }

    if (grandPoolEnabled_ && extraNonce2Size_ < 12) {
      LOG(ERROR) << "Too short extraNonce2 for grand pool, it should be 12 "
                    "bytes or longer";
      state_ = TODO_RECONNECT;
      return;
    }

    // subscribe successful
    state_ = SUBSCRIBED;

    // do mining.authorize
    string s = Strings::Format(
        "{\"id\": 1, \"method\": \"mining.authorize\","
        "\"params\": [\"%s\", \"%s\"]}\n",
        workerName_,
        passwd_);
    sendData(s);
    return;
  }

  if (state_ == SUBSCRIBED) {
    if (jresult.boolean() == true) {
      // authorize successful
      state_ = AUTHENTICATED;
      LOG(INFO) << "<" << poolName_ << "> auth success, name: \"" << workerName_
                << "\"";
    } else {
      LOG(ERROR) << "<" << poolName_ << "> auth fail, name: \"" << workerName_
                 << "\", response : " << line;
    }
    return;
  }

  LOG(INFO) << "<" << poolName_ << "> recv(" << line.size() << "): " << line;
}
