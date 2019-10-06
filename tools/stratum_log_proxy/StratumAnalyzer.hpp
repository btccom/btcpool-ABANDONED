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
#include <nlohmann/json.hpp>

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

class StratumAnalyzer : public std::enable_shared_from_this<StratumAnalyzer> {
public:
  using OnSubmitLogin = function<void(StratumWorker)>;

  const size_t MAX_JOB_SIZE = 100;
  const size_t MAX_SUBMIT_SIZE = 100;

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
    uint64_t diff_;
    uint64_t height_ = 0;
    time_t time_ = 0;

    string toString() {
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

  struct Submit {
    JSON id_;
    string powHash_;
    string mixDigest_;
    string response_;
    uint64_t nonce_;
    uint64_t diff_;
    uint64_t height_ = 0;
    time_t time_ = 0;
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
  SeqMap<JSON /*id*/, Job> submits_;

public:
  void setOnSubmitLogin(OnSubmitLogin func) { onSubmitLogin_ = func; }

  void updateMinerInfo(const PoolInfo &info) { poolInfo_ = info; }

  string getSubmitLoginLine() {
    JSON json = {{"id", loginRequestId_},
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

  StratumAnalyzer(const string &ip, uint16_t port)
    : running_(false)
    , ip_(ip)
    , port_(port) {}

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
        LOG(INFO) << toString() << (record.upload_ ? "[upload]" : "[download]")
                  << " invalid json: " << record.line_;
        return;
      }

      if (record.upload_) {
        // upload: miner -> pool
        if (json["method"].is_string()) {
          string method = json["method"].get<string>();

          if (method == "eth_submitLogin") {
            parseSubmitLogin(move(json));

            if (onSubmitLogin_) {
              onSubmitLogin_(worker_);
              onSubmitLogin_ = nullptr;
            }
          }
        } else {
          LOG(INFO) << toString()
                    << "[upload] missing method, json: " << record.line_;
        }

      } else {
        // There is no easy way to determine which response is a job. The job
        // may come from a server proactive notification (id == 0) or a response
        // of eth_getWork call (id != 0).
        if (json["result"].is_array() && json["result"].size() >= 3 &&
            json["result"][0].is_string() &&
            json["result"][0].get<string>().size() == 66 &&
            json["result"][1].is_string() &&
            json["result"][1].get<string>().size() == 66 &&
            json["result"][2].is_string() &&
            // it may be 60 or 66 bytes
            json["result"][2].get<string>().size() >= 60) {
          parseJobNotify(move(json));
        }
      }
    } catch (const JSONException &ex) {
      LOG(INFO) << toString() << (record.upload_ ? "[upload]" : "[download]")
                << " json parser exception: " << ex.what()
                << ", json: " << record.line_;
    }
  }

  void parseSubmitLogin(JSON &&json) {
    auto params = json["params"];
    if (!params.is_array() || params.size() < 1) {
      LOG(INFO) << toString() << "[upload] missing params, json: " << json;
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

  void parseJobNotify(JSON &&json) {
    const auto &params = json["result"];

    Job job;
    job.time_ = time(nullptr);
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
    DLOG(INFO) << toString() << "[job] " << job.toString();

    jobs_.clear(MAX_JOB_SIZE);
  }
};
