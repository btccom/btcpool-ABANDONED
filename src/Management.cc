
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
#include <glog/logging.h>
#include "Management.h"
#include "StratumServer.h"
#include "StratumSession.h"
#include "DiffController.h"
#include "Utils.h"
#include "UserInfo.h"

Management::Management(const libconfig::Config &cfg, StratumServer &server)
  : running_(false)
  , currentAutoChainId_(0)
  , singleUserAutoSwitchChain_(false)
  , singleUserCurrentChainId_(0)
  , controllerTopicConsumer_(
        cfg.lookup("management.kafka_brokers").c_str(),
        cfg.lookup("management.controller_topic").c_str(),
        0 /*patition*/)
  , processorTopicProducer_(
        cfg.lookup("management.kafka_brokers").c_str(),
        cfg.lookup("management.processor_topic").c_str(),
        RD_KAFKA_PARTITION_UA)
  , server_(server) {
  uptime_ = time(nullptr);

  cfg.lookupValue("management.auto_switch_chain", autoSwitchChain_);
  cfg.lookupValue("sserver.type", chainType_);
}

bool Management::setup() {
  // ------------------- Init Kafka -------------------
  {
    const int32_t kConsumeLatestN = 1;

    // we need to consume the latest one
    map<string, string> consumerOptions;
    consumerOptions["fetch.wait.max.ms"] = "10";
    if (controllerTopicConsumer_.setup(
            RD_KAFKA_OFFSET_TAIL(kConsumeLatestN), &consumerOptions) == false) {
      LOG(INFO) << "setup consumer fail";
      return false;
    }

    if (!controllerTopicConsumer_.checkAlive()) {
      LOG(ERROR) << "kafka brokers is not alive";
      return false;
    }
  }

  // processorTopicProducer_
  {
    map<string, string> options;
    // We want the message to be delivered immediately, reducing the cache.
    options["queue.buffering.max.messages"] = "10";
    options["queue.buffering.max.ms"] = "1000";
    options["batch.num.messages"] = "10";

    if (!processorTopicProducer_.setup(&options)) {
      LOG(ERROR) << "kafka processorTopicProducer_ setup failure";
      return false;
    }
    if (!processorTopicProducer_.checkAlive()) {
      LOG(ERROR) << "kafka processorTopicProducer_ is NOT alive";
      return false;
    }
  }

  return true;
}

void Management::stop() {
  running_ = false;
  if (consumerThread_.joinable()) {
    consumerThread_.join();
  }
}

void Management::run() {
  updateSingleUserChain();

  running_ = true;
  consumerThread_ = std::thread([this]() {
    try {
      LOG(INFO) << "Management thread running...";
      sendOnlineNotification();

      const int32_t kTimeoutMs = 5000;
      while (running_) {
        rd_kafka_message_t *rkmessage;
        rkmessage = controllerTopicConsumer_.consumer(kTimeoutMs);

        // timeout, most of time it's not nullptr and set an error:
        //          rkmessage->err == RD_KAFKA_RESP_ERR__PARTITION_EOF
        if (rkmessage != nullptr) {
          // consume stratum job
          //
          // It will create a StratumJob and try to broadcast it immediately
          // with broadcastStratumJob(shared_ptr<StratumJob>). A derived class
          // needs to implement the abstract method
          // broadcastStratumJob(shared_ptr<StratumJob>) to decide whether to
          // add the StratumJob to the map exJobs_ and whether to send the job
          // to miners immediately. Derived classes do not need to implement a
          // scheduled sending mechanism, checkAndSendMiningNotify() will
          // provide a default implementation.
          handleMessage(rkmessage);

          // Return message to rdkafka
          rd_kafka_message_destroy(rkmessage);
        }
      }

      sendOfflineNotification();
      LOG(INFO) << "Management thread stopped";

    } catch (const std::exception &ex) {
      LOG(ERROR) << "Management thread exception: " << ex.what();
      sendExceptionReport(ex);

      // Recovery from failure
      if (running_) {
        LOG(INFO) << "Restart management thread after 5 seconds...";
        std::this_thread::sleep_for(5s);
        running_ = false;
        run();
      }
    }
  });
}

void Management::handleMessage(rd_kafka_message_t *rkmessage) {
  try {
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

    if (rkmessage->len == 0) {
      return;
    }

    JSON json =
        JSON::parse(string((const char *)rkmessage->payload, rkmessage->len));
    if (!json.is_object() || !json["type"].is_string() ||
        !json["action"].is_string()) {
      DLOG(WARNING) << "[Management] json missing the field 'type' or 'action'";
      return;
    }
    string type = json["type"].get<string>();
    string action = json["action"].get<string>();
    JSON id = json["id"];

    DLOG(INFO) << "[Management] new message, type: " << type
               << ", action: " << action << ", id: " << id;

    // filter matching
    if (json["filter"].is_object()) {
      if (!checkFilter(json["filter"])) {
        DLOG(INFO) << "[Management] does not match the filter, skip";
        return;
      }
      DLOG(INFO) << "[Management] match the filter, continue";
    }

    // Get server status
    if (type == "sserver_cmd") {
      if (action == "list") {
        server_.dispatch([this, id]() {
          JSON response = getServerBriefDesc("sserver_response", "list");
          response["id"] = id;
          sendMessage(response.dump());
          LOG(INFO) << "[Management] sent server brief desc";
        });
        return;
      }

      if (action == "get_status") {
        server_.dispatch([this, id]() {
          JSON response =
              getConfigureAndStatus("sserver_response", "get_status");
          response["id"] = id;
          sendMessage(response.dump());
          LOG(INFO) << "[Management] sent server status";
        });
        return;
      }

      // Automatic switching chain
      if (action == "auto_switch_chain") {
        if (!json["chain_name"].is_string()) {
          LOG(WARNING) << "[Auto Switch Chain] missing chain_name in command: "
                       << json.dump();
          return;
        }

        string chainName = json["chain_name"].get<string>();
        ssize_t chainId = -1;
        for (ssize_t i = 0; i < (ssize_t)server_.chains_.size(); i++) {
          if (server_.chains_[i].name_ == chainName) {
            chainId = i;
            break;
          }
        }

        // not found
        if (chainId == -1) {
          LOG(WARNING) << "[Auto Switch Chain] cannot find chain " << chainName;
          return;
        }

        // no change
        if (chainId == (ssize_t)currentAutoChainId_) {
          DLOG(INFO) << "[Auto Switch Chain] no change of the chain";
          return;
        }

        LOG(INFO) << "[Auto Switch Chain] chain switching: "
                  << server_.chainName(currentAutoChainId_) << " -> "
                  << server_.chainName(chainId);

        auto oldChainId = currentAutoChainId();
        currentAutoChainId_ = chainId;

        server_.userInfo_->autoSwitchChain(
            oldChainId,
            currentAutoChainId(),
            [this, id](
                size_t oldChain,
                size_t newChain,
                size_t users,
                size_t miners) mutable {
              JSON response = getResponseTemplate(
                  "sserver_response", "auto_switch_chain", id);
              response["result"] = true;
              response["old_chain_name"] = server_.chainName(oldChain);
              response["new_chain_name"] = server_.chainName(newChain);
              ;
              response["switched_users"] = users;
              response["switched_connections"] = miners;
              sendMessage(response.dump());
            });
        return;
      }

      DLOG(INFO) << "[Management] unknown action: " << action;
      return;
    }

    DLOG(INFO) << "[Management] unknown type: " << type;

  } catch (const std::exception &ex) {
    LOG(ERROR) << "Management thread exception: " << ex.what();
    sendExceptionReport(ex);
  }
}

bool Management::checkFilter(JSON filter) {
  bool exclusion = false;
  if (filter["exclusion"].is_boolean()) {
    exclusion = filter["exclusion"].get<bool>();
  }
  if (filter["server_id"].is_object()) {
    auto myId = filter["server_id"][std::to_string(server_.serverId_)];
    if (myId.is_boolean() && myId.get<bool>()) {
      return !exclusion;
    }
  }
  return exclusion;
}

void Management::sendMessage(std::string msg) {
  processorTopicProducer_.produce(msg.data(), msg.size());
}

void Management::sendOnlineNotification() {
  sendMessage(getConfigureAndStatus("sserver_notify", "online").dump());
  LOG(INFO) << "[Management] sent server online notify";
}

void Management::sendOfflineNotification() {
  sendMessage(getConfigureAndStatus("sserver_notify", "offline").dump());
  LOG(INFO) << "[Management] sent server offline notify";
}

void Management::sendExceptionReport(const std::exception &ex) {
  JSON json = getConfigureAndStatus("sserver_notify", "exception");
  json["exception"] = {
      {"what", ex.what()},
  };
  sendMessage(json.dump());
  LOG(INFO) << "[Management] sent server exception";
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

JSON Management::getServerBriefDesc(const string &type, const string &action) {
  JSON json = {
      {"created_at", date("%F %T")},
      {"type", type},
      {"action", action},
      {"server_id", server_.serverId_},
      {"host",
       {
           {"hostname", IpAddress::getHostName()},
           {"ip", IpAddress::getInterfaceAddrs()},
       }},
      {"status",
       {
           {"uptime", uptime_},
           {"connections",
            {
                {"count", server_.connections_.size()},
            }},
       }},
  };

  return json;
}

JSON Management::getConfigureAndStatus(
    const string &type, const string &action) {
  JSON chains = JSON::object();
  JSON chainStatus = JSON::object();
  for (size_t i = 0; i < server_.chains_.size(); i++) {
    auto itr = server_.chains_[i];

    chains[itr.name_] = {
        {"name", itr.name_},
        {"users_list_id_api_url", server_.userInfo_->chains_[i].apiUrl_},
        {"kafka_brokers", itr.kafkaProducerCommonEvents_->getBrokers()},
        {"job_topic", itr.jobRepository_->kafkaConsumer_.getTopic()},
        {"share_topic", itr.kafkaProducerShareLog_->getTopic()},
        {"solved_share_topic", itr.kafkaProducerSolvedShare_->getTopic()},
        {"common_events_topic", itr.kafkaProducerCommonEvents_->getTopic()},
        {"single_user_puid", itr.singleUserId_},
    };

    std::map<string, size_t> shareStats;
    for (const auto &stats : itr.shareStats_) {
      shareStats[std::to_string(stats.first)] = stats.second;
    }

    chainStatus[itr.name_] = {
        {"name", itr.name_},
        {"share_status", shareStats},
        {"last_job_time", itr.jobRepository_->lastJobSendTime_},
        {"last_job_height", itr.jobRepository_->lastJobHeight_},
        {"last_job_id", itr.jobRepository_->lastJobId_},
    };
  }

  string ip;
  uint16_t port = 0;
  IpAddress::getIpPortFromStruct((sockaddr *)&(server_.sin_), ip, port);

  string niceHashMinDiffPath =
      server_.chains_[0].jobRepository_->niceHashMinDiffWatcher_
      ? server_.chains_[0].jobRepository_->niceHashMinDiffWatcher_->getPath()
      : "";

  JSON nicehash = {
      {"forced", server_.chains_[0].jobRepository_->niceHashForced_},
      {"min_difficulty",
       server_.chains_[0].jobRepository_->niceHashMinDiff_.load()},
      {"min_difficulty_zookeeper_path", niceHashMinDiffPath},
  };

  JSON sserver = {
      {"type", chainType_},
      {"id", server_.serverId_},
      {"ip", ip},
      {"port", port},
      {"share_avg_seconds",
       server_.defaultDifficultyController_->kRecordSeconds_},
      {"max_job_lifetime",
       server_.chains_[0].jobRepository_->kMaxJobsLifeTime_},
      {"mining_notify_interval",
       server_.chains_[0].jobRepository_->kMiningNotifyInterval_},
      {"default_difficulty", server_.defaultDifficultyController_->curDiff_},
      {"max_difficulty", server_.defaultDifficultyController_->kMaxDiff_},
      {"min_difficulty", server_.defaultDifficultyController_->kMinDiff_},
      {"diff_adjust_period",
       server_.defaultDifficultyController_->kDiffWindow_},
      {"shutdown_grace_period", server_.shutdownGracePeriod_},
      {"nicehash", nicehash},
      {"multi_chains", server_.chains_.size() > 0},
      {"enable_simulator", server_.isEnableSimulator_},
      {"enable_submit_invalid_block", server_.isSubmitInvalidBlock_},
      {"enable_dev_mode", server_.isDevModeEnable_},
      {"dev_fixed_difficulty", server_.devFixedDifficulty_},
  };

  JSON management = {
      {"kafka_brokers", processorTopicProducer_.getBrokers()},
      {"controller_topic", controllerTopicConsumer_.getTopic()},
      {"processor_topic", processorTopicProducer_.getTopic()},
      {"auto_switch_chain", autoSwitchChain_},
  };

  JSON users = {
      {"case_insensitive", server_.userInfo_->caseInsensitive_},
      {"zookeeper_userchain_map", server_.userInfo_->zkUserChainMapDir_},
      {"strip_user_suffix", server_.userInfo_->stripUserSuffix_},
      {"user_suffix_separator", server_.userInfo_->userSuffixSeparator_},
      {"enable_auto_reg", server_.userInfo_->enableAutoReg_},
      {"auto_reg_max_pending_users",
       server_.userInfo_->autoRegMaxPendingUsers_},
      {"zookeeper_auto_reg_watch_dir", server_.userInfo_->zkAutoRegWatchDir_},
      {"namechains_check_interval",
       server_.userInfo_->nameChainsCheckIntervalSeconds_},
      {"single_user_chain", server_.singleUserChain()},
      {"single_user_mode", server_.singleUserMode()},
      {"single_user_name", server_.singleUserName()},
  };

  std::map<string, std::map<string, size_t>> sessions;
  for (auto &session : server_.connections_) {
    ++(sessions[server_.chainName(session->getChainId())]
               [FormatSessionStatus(session->getState())]);
  }

  JSON json = {
      {"created_at", date("%F %T")},
      {"type", type},
      {"action", action},
      {"server_id", server_.serverId_},
      {"host",
       {
           {"hostname", IpAddress::getHostName()},
           {"ip", IpAddress::getInterfaceAddrs()},
       }},
      {"status",
       {
           {"uptime", uptime_},
           {"connections",
            {
                {"count", server_.connections_.size()},
                {"state", sessions},
            }},
           {"chains", chainStatus},
       }},
      {"config",
       {
           {"sserver", sserver},
           {"chains", chains},
           {"users", users},
           {"management", management},
       }},
  };

  return json;
}

JSON Management::getResponseTemplate(
    const string &type, const string &action, const JSON &id) {
  JSON json = {
      {"created_at", date("%F %T")},
      {"type", type},
      {"action", action},
      {"server_id", server_.serverId_},
      {"id", id},
  };
  return json;
}

bool Management::autoSwitchChainEnabled() const {
  if (server_.singleUserChain()) {
    return true;
  }
  return autoSwitchChain_;
}

size_t Management::currentAutoChainId() const {
  if (server_.singleUserChain()) {
    return singleUserAutoSwitchChain_ ? currentAutoChainId_
                                      : singleUserCurrentChainId_;
  }
  return currentAutoChainId_;
}

bool Management::updateSingleUserChain() {
  if (!server_.singleUserChain() || server_.chains_.size() <= 1) {
    return false;
  }

  try {

    auto oldChainId = currentAutoChainId();
    string oldChain = singleUserChainType_;

    if (server_.userInfo_->zkGetRawChainW(
            server_.singleUserName(),
            singleUserChainType_,
            handleSwitchChainEvent,
            this)) {

      if (autoSwitchChain_ &&
          singleUserChainType_ == server_.userInfo_->AUTO_CHAIN_NAME) {
        singleUserAutoSwitchChain_ = true;
        singleUserCurrentChainId_ = 0;

      } else {
        ssize_t chainId = -1;
        for (ssize_t i = 0; i < (ssize_t)server_.chains_.size(); i++) {
          if (server_.chains_[i].name_ == singleUserChainType_) {
            chainId = i;
            break;
          }
        }

        // not found
        if (chainId == -1) {
          LOG(WARNING) << "[single user chain] cannot find chain "
                       << singleUserChainType_;
          return false;
        }

        singleUserCurrentChainId_ = (size_t)chainId;
        singleUserAutoSwitchChain_ = false;
      }

      LOG(INFO) << "[single user chain] chain update, reference user: "
                << server_.singleUserName() << ", chain: " << oldChain << " -> "
                << singleUserChainType_;

      server_.userInfo_->autoSwitchChain(
          oldChainId,
          currentAutoChainId(),
          [this](
              size_t oldChain,
              size_t newChain,
              size_t users,
              size_t miners) mutable {
            JSON response = getResponseTemplate(
                "sserver_action", "single_user_switch_chain", JSON());
            response["result"] = true;
            response["old_chain_name"] = server_.chainName(oldChain);
            response["new_chain_name"] = server_.chainName(newChain);
            ;
            response["switched_users"] = users;
            response["switched_connections"] = miners;
            sendMessage(response.dump());
          });

      return true;
    }

    LOG(WARNING) << "[single user chain] cannot find mining chain in "
                    "zookeeper, user name: "
                 << server_.singleUserName() << " ("
                 << server_.userInfo_->zkUserChainMapDir_
                 << server_.singleUserName() << ")";

  } catch (const std::exception &ex) {
    LOG(ERROR) << "Management::checkSingleUserChain(): zkGetRawChain() failed: "
               << ex.what();
  } catch (...) {
    LOG(ERROR) << "Management::checkSingleUserChain(): unknown exception";
  }

  return false;
}

bool Management::checkSingleUserChain() {
  if (!server_.singleUserChain() || server_.chains_.size() <= 1) {
    return false;
  }

  try {
    string newChainName =
        server_.userInfo_->zkGetRawChain(server_.singleUserName());
    if (newChainName == server_.userInfo_->AUTO_CHAIN_NAME) {
      if (singleUserAutoSwitchChain_) {
        DLOG(INFO) << "[single user chain] User does not switch chains, "
                      "reference user: "
                   << server_.singleUserName() << ", chain: " << newChainName;
      } else {
        LOG(INFO)
            << "[single user chain] User switched the chain, reference user: "
            << server_.singleUserName() << ", chains: " << singleUserChainType_
            << " -> " << newChainName;
        updateSingleUserChain();
        return true;
      }
    } else if (singleUserChainType_ == newChainName) {
      DLOG(INFO)
          << "[single user chain] User does not switch chains, reference user: "
          << server_.singleUserName() << ", chain: " << newChainName;
    } else {
      LOG(INFO)
          << "[single user chain] User switched the chain, reference user: "
          << server_.singleUserName() << ", chains: " << singleUserChainType_
          << " -> " << newChainName;
      updateSingleUserChain();
      return true;
    }
  } catch (const std::exception &ex) {
    LOG(ERROR) << "Management::checkSingleUserChain(): zkGetRawChain() failed: "
               << ex.what();
  } catch (...) {
    LOG(ERROR) << "Management::checkSingleUserChain(): unknown exception";
  }

  return false;
}

void Management::handleSwitchChainEvent(
    zhandle_t *zh, int type, int state, const char *path, void *pManagement) {
  static_cast<Management *>(pManagement)->updateSingleUserChain();
}
