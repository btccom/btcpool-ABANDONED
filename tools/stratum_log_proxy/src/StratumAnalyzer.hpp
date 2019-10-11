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
#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <functional>
#include <algorithm>
#include <nlohmann/json.hpp>

#include "MySQLConnection.hpp"
#include "StratumBase.hpp"
#include "uint256.h"
#include "arith_uint256.h"

using namespace std;
using JSON = nlohmann::json;
using JSONException = nlohmann::detail::exception;

static uint64_t Eth_TargetToDifficulty(const string &targetStr) {
  static const arith_uint256 kMaxUint256(
      "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
  static const uint64_t kMaxUint64 = 0xffffffffffffffffull;

  arith_uint256 target = UintToArith256(uint256S(targetStr));
  if (target == 0) {
    return kMaxUint64;
  }
  arith_uint256 diff = kMaxUint256 / target;
  return diff.GetLow64();
}

// replace ' to "
static string quote(string value) {
  std::replace(value.begin(), value.end(), '\'', '"');
  return value;
}

class StratumAnalyzer : public std::enable_shared_from_this<StratumAnalyzer> {
public:
  using OnSubmitLogin = function<void(StratumWorker)>;

  const size_t MAX_CACHED_JOB_NUM = 50;
  const size_t MAX_CACHED_SHARE_NUM = 20;

protected:
  struct Record {
    bool upload_ = 0;
    time_t time_ = 0;
    string line_;
  };

  struct Job {
    JSON id_;
    string powHash_;
    string dagSeed_;
    string noncePrefix_;
    uint64_t diff_ = 1;
    uint64_t height_ = 0;
    time_t time_ = 0;

    string toString() const {
      return StringFormat(
          "id: %s, powHash: %s, dagSeed: %s, noncePrefix: %s, diff: %u, "
          "height: %u, time: %s",
          id_.dump(),
          powHash_,
          dagSeed_,
          noncePrefix_,
          diff_,
          height_,
          date("%F %T", time_));
    }
  };

  struct Share {
    JSON id_;
    string powHash_;
    string mixDigest_;
    string response_;
    uint64_t nonce_ = 0;
    uint64_t diff_ = 1;
    uint64_t height_ = 0;
    time_t time_ = 0;

    string toString() const {
      return StringFormat(
          "id: %s, powHash: %s, mixDigest: %s, response: %s, nonce: %016x, "
          "diff: %u, "
          "height: %u, time: %s",
          id_.dump(),
          powHash_,
          mixDigest_,
          response_,
          nonce_,
          diff_,
          height_,
          date("%F %T", time_));
    }
  };

  string uploadIncompleteLine_;
  string downloadIncompleteLine_;
  vector<Record> lines_;
  mutex recordLock_;

  atomic<bool> running_;
  thread runningThread_;

  OnSubmitLogin onSubmitLogin_;
  JSON loginRequestId_;

  string ip_;
  uint16_t port_ = 0;
  StratumWorker worker_;
  PoolInfo poolInfo_;

  SeqMap<string /*powHash*/, Job> jobs_;
  LinkMap<JSON /*id*/, Share> shares_;

  MySQLExecQueue &mysqlExecQueue_;
  string minerInfoSql_;
  const LogOptions &logOptions_;

public:
  void setOnSubmitLogin(OnSubmitLogin func) { onSubmitLogin_ = func; }

  void updateMinerInfo(const PoolInfo &info) {
    poolInfo_ = info;
    minerInfoSql_ = StringFormat(
        "'%s',%u,'%s','%s','%s','%s','%s','%s','%s','%s','%s','%s'",
        quote(ip_),
        port_,
        quote(worker_.fullName_),
        quote(worker_.wallet_),
        quote(worker_.userName_),
        quote(worker_.workerName_),
        quote(worker_.password_),
        quote(poolInfo_.name_),
        quote(poolInfo_.url_),
        quote(poolInfo_.user_),
        quote(poolInfo_.worker_),
        quote(poolInfo_.pwd_));
  }

  string getSubmitLoginLine() {
    JSON json = {{"jsonrpc", "2.0"},
                 {"id", loginRequestId_},
                 {"method", "eth_submitLogin"},
                 {"params", {poolInfo_.user_, poolInfo_.pwd_}}};
    if (!poolInfo_.worker_.empty()) {
      json["worker"] = poolInfo_.worker_;
    }
    return json.dump() + "\n";
  }

  string getUploadIncompleteLine() {
    string result;
    for (const auto &itr : lines_) {
      result += itr.line_;
    }
    result += uploadIncompleteLine_;
    return result;
  }

  string toString() const {
    string result = StringFormat("%s:%u", ip_, port_);
    if (!worker_.fullName_.empty())
      result += " / " + worker_.fullName_;

    string pool = poolInfo_.url_;
    if (!poolInfo_.user_.empty() || !poolInfo_.worker_.empty()) {
      pool += " / " + poolInfo_.user_;
      if (!poolInfo_.worker_.empty())
        pool += "." + poolInfo_.worker_;
    }

    if (!pool.empty())
      result += " -> " + pool;

    return "[" + result + "] ";
  }

  StratumAnalyzer(
      const string &ip,
      uint16_t port,
      MySQLExecQueue &mysqlExecQueue,
      const LogOptions &logOptions)
    : running_(false)
    , ip_(ip)
    , port_(port)
    , mysqlExecQueue_(mysqlExecQueue)
    , logOptions_(logOptions) {}

  ~StratumAnalyzer() { stop(); }

  void addUploadText(const string &text) {
    lock_guard<mutex> scopeLock(recordLock_);
    uploadIncompleteLine_ += text;

    size_t pos = uploadIncompleteLine_.npos;
    while ((pos = uploadIncompleteLine_.find("\n")) !=
           uploadIncompleteLine_.npos) {
      lines_.emplace_back(
          Record{true, time(nullptr), uploadIncompleteLine_.substr(0, ++pos)});
      uploadIncompleteLine_ = uploadIncompleteLine_.substr(pos);
    }
  }

  void addDownloadText(const string &text) {
    lock_guard<mutex> scopeLock(recordLock_);
    downloadIncompleteLine_ += text;

    size_t pos = downloadIncompleteLine_.npos;
    while ((pos = downloadIncompleteLine_.find("\n")) !=
           downloadIncompleteLine_.npos) {
      lines_.emplace_back(
          Record{false, time(nullptr), downloadIncompleteLine_.substr(0, pos)});
      downloadIncompleteLine_ = downloadIncompleteLine_.substr(pos + 1);
    }
  }

  void run() {
    auto self(shared_from_this());
    runningThread_ = std::thread([this, self]() {
      running_ = true;
      while (running_) {
        this_thread::sleep_for(1s);
        recordLock_.lock();
        vector<Record> lines = lines_;
        lines_.clear();
        recordLock_.unlock();

        for (const Record &record : lines) {
          parseRecord(record);
        }
      }

      runOnce();
      while (shares_.size() > 0) {
        auto share = shares_.pop();
        share.response_ = "proxy close";
        writeShare(share);
      }
    });
  }

  void runOnce() {
    lock_guard<mutex> scopeLock(recordLock_);
    while (lines_.size() > 0) {
      auto line = lines_[0];
      lines_.erase(lines_.begin());
      parseRecord(line);
    }
  }

  void stop() {
    running_ = false;
    if (runningThread_.joinable()) {
      runningThread_.join();
    }
  }

  void parseRecord(const Record &record) {
    try {
      auto json = JSON::parse(record.line_);
      if (!json.is_object()) {
        LOG(WARNING) << toString()
                     << (record.upload_ ? "[upload]" : "[download]")
                     << " invalid json: " << record.line_;
        return;
      }

      if (record.upload_) {
        // upload: miner -> pool
        if (json["method"].is_string()) {
          string method = json["method"].get<string>();

          if (method == "eth_submitWork") {
            parseSubmitShare(json, record);
          } else if (method == "eth_submitLogin") {
            parseSubmitLogin(json);

            if (onSubmitLogin_) {
              onSubmitLogin_(worker_);
              onSubmitLogin_ = nullptr;
            }
          }
        } else {
          LOG(WARNING) << toString()
                       << "[upload] missing method, json: " << record.line_;
        }

      } else {
        // There is no easy way to determine which response is a job. The job
        // may come from a server proactive notification (id == 0) or a
        // response of eth_getWork call (id != 0).
        if (json["result"].is_array() && json["result"].size() >= 3 &&
            json["result"][0].is_string() &&
            json["result"][0].get<string>().size() == 66 &&
            json["result"][1].is_string() &&
            json["result"][1].get<string>().size() == 66 &&
            json["result"][2].is_string() &&
            // it should be 0x...
            json["result"][2].get<string>().size() > 2) {
          parseJobNotify(json, record);
        } else if (shares_.contains(json["id"])) {
          parseSubmitResponse(json);
        }
      }
    } catch (const JSONException &ex) {
      LOG(WARNING) << toString() << (record.upload_ ? "[upload]" : "[download]")
                   << " json parser exception: " << ex.what()
                   << ", json: " << record.line_;
    }
  }

  void parseSubmitLogin(JSON &json) {
    auto params = json["params"];
    if (!params.is_array() || params.size() < 1) {
      LOG(WARNING) << toString()
                   << "[eth_submitLogin] missing params, json: " << json;
      return;
    }

    loginRequestId_ = json["id"];

    string user = params[0].get<string>();
    string password;
    if (params.size() >= 2) {
      password = json["params"][1].get<string>();
    }
    if (json["worker"].is_string() && json["worker"].get<string>().size() > 0) {
      user += "." + json["worker"].get<string>();
    }

    worker_.setNames(user, password);
  }

  void parseJobNotify(JSON &json, const Record &record) {
    const auto &params = json["result"];

    Job job;
    job.time_ = record.time_;
    job.id_ = json["id"];
    job.powHash_ = params[0].get<string>();
    job.dagSeed_ = params[1].get<string>();
    job.diff_ = Eth_TargetToDifficulty(params[2].get<string>());
    if (params.size() >= 4 && params[3].is_string()) {
      job.noncePrefix_ = params[3].get<string>();
    }
    if (json["height"].is_number_unsigned()) {
      job.height_ = json["height"].get<uint64_t>();
    }

    jobs_[job.powHash_] = job;
    if (logOptions_.jobNotify_)
      LOG(INFO) << toString() << "[job] " << job.toString();
    writeJob(job);
    jobs_.clear(MAX_CACHED_JOB_NUM);
  }

  void parseSubmitShare(JSON &json, const Record &record) {
    auto params = json["params"];
    if (!params.is_array() || params.size() < 2) {
      LOG(WARNING) << toString()
                   << "[eth_submitWork] missing params, json: " << json;
      return;
    }
    if (!params[0].is_string() || !params[1].is_string()) {
      LOG(WARNING) << toString()
                   << "[eth_submitWork] missing params, json: " << json;
      return;
    }

    Share share;
    share.time_ = record.time_;
    share.id_ = json["id"];
    share.nonce_ = strtoull(params[0].get<string>().c_str(), nullptr, 16);
    share.powHash_ = params[1].get<string>();
    if (params.size() >= 3 && params[2].is_string()) {
      share.mixDigest_ = params[2].get<string>();
    }

    if (jobs_.contains(share.powHash_)) {
      const Job &job = jobs_[share.powHash_];
      share.diff_ = job.diff_;
      share.height_ = job.height_;
    } else {
      LOG(WARNING) << toString()
                   << "[share submit] cannot find the job of submit "
                   << share.toString();
    }

    shares_.push(share.id_, share);
    if (logOptions_.shareSubmit_)
      LOG(INFO) << toString() << "[share submit] " << share.toString();

    shares_.clearOld(MAX_CACHED_SHARE_NUM, [this](Share share) mutable {
      share.response_ = "no response";
      LOG(WARNING) << toString()
                   << "[share submit] pool did not respond to the submit "
                   << share.toString();
      writeShare(share);
    });
  }

  void parseSubmitResponse(JSON &json) {
    Share share;
    if (!shares_.pop(json["id"], share)) {
      LOG(ERROR)
          << toString()
          << "something went wrong, cannot find share from shares_, response: "
          << json.dump();
    }

    try {
      if (json["data"].is_object() && json["data"]["message"].is_string()) {
        share.response_ = json["data"]["message"].get<string>();
      } else if (
          json["error"].is_object() && json["error"]["message"].is_string()) {
        share.response_ = json["error"]["message"].get<string>();
      } else if (json["error"].is_array() && json["error"][1].is_string()) {
        share.response_ = json["error"][1].get<string>();
      } else if (json["result"].is_boolean()) {
        share.response_ = json["result"].get<bool>() ? "success" : "falied";
      } else {
        share.response_ = json.dump();
      }
    } catch (const JSONException &ex) {
      share.response_ = json.dump();
      LOG(WARNING) << toString()
                   << "[share response] json parser exception: " << ex.what()
                   << ", json: " << share.response_;
    }

    if (logOptions_.shareResponse_)
      LOG(INFO) << toString() << "[share response] " << share.toString();
    writeShare(share);
  }

  void writeJob(const Job &job) {
    string sql = StringFormat(
        "INSERT INTO jobs(job_id,pow_hash,dag_seed,nonce_prefix,"
        "diff,height,timestamp,ip,port,miner_fullname,miner_wallet,"
        "miner_user,miner_worker,miner_pwd,pool_name,pool_url,pool_user,"
        "pool_worker,pool_pwd) VALUES('%s','%s','%s','%s',%u,%u,%u,%s)",
        quote(job.id_.dump()),
        quote(job.powHash_),
        quote(job.dagSeed_),
        quote(job.noncePrefix_),
        job.diff_,
        job.height_,
        job.time_,
        minerInfoSql_);
    mysqlExecQueue_.addSQL(sql);
  }

  void writeShare(const Share &share) {
    string sql = StringFormat(
        "INSERT INTO shares(job_id,pow_hash,mix_digest,response,nonce,"
        "diff,height,timestamp,ip,port,miner_fullname,miner_wallet,"
        "miner_user,miner_worker,miner_pwd,pool_name,pool_url,pool_user,"
        "pool_worker,pool_pwd) VALUES('%s','%s','%s','%s',%u,%u,%u,%u,%s)",
        quote(share.id_.dump()),
        quote(share.powHash_),
        quote(share.mixDigest_),
        quote(share.response_),
        share.nonce_,
        share.diff_,
        share.height_,
        share.time_,
        minerInfoSql_);
    mysqlExecQueue_.addSQL(sql);
  }
};
