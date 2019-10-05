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

using namespace std;
using nljson = nlohmann::json;
using nlexception = nlohmann::detail::exception;

class StratumAnalyzer: public std::enable_shared_from_this<StratumAnalyzer> {
public:
  using OnSubmitLogin = function<void(StratumWorker)>;

protected:
  struct Record {
    bool upload_;
    time_t time_;
    string line_;

    Record(bool &&upload, time_t &&time, string &&line)
      : upload_(move(upload))
      , time_(move(time))
      , line_(move(line))
      {}
  };

  string uploadIncompleteLine_;
  string downloadIncompleteLine_;
  vector<Record> lines_;
  mutex recordLock_;

  atomic<bool> running_;
  thread runningThread_;

  OnSubmitLogin onSubmitLogin_;

  string ip_;
  uint16_t port_;
  StratumWorker worker_;
  PoolInfo poolInfo_;

public:
  void setOnSubmitLogin(OnSubmitLogin func) {
    onSubmitLogin_ = func;
  }

  void updateMinerInfo(const PoolInfo &info) {
    poolInfo_ = info;
  }

  string getUploadIncompleteLine() {
    return uploadIncompleteLine_;
  }

  StratumAnalyzer()
    : running_(false) {}
  
  ~StratumAnalyzer() {
    stop();
  }

  void addUploadText(const string &text) {
    lock_guard<mutex> scopeLock(recordLock_);
    uploadIncompleteLine_ += text;

    size_t pos = uploadIncompleteLine_.npos;
    while ((pos = uploadIncompleteLine_.find("\n")) != uploadIncompleteLine_.npos) {
      lines_.emplace_back(true, time(nullptr), uploadIncompleteLine_.substr(0, pos));
      uploadIncompleteLine_ = uploadIncompleteLine_.substr(pos + 1);
    }
  }

  void addDownloadText(const string &text) {
    lock_guard<mutex> scopeLock(recordLock_);
    downloadIncompleteLine_ += text;

    size_t pos = downloadIncompleteLine_.npos;
    while ((pos = downloadIncompleteLine_.find("\n")) != downloadIncompleteLine_.npos) {
      lines_.emplace_back(false, time(nullptr), downloadIncompleteLine_.substr(0, pos));
      downloadIncompleteLine_ = downloadIncompleteLine_.substr(pos + 1);
    }
  }

  void run() {
    auto self(shared_from_this());
    runningThread_ = std::thread([this, self]() {
      running_ = true;
      while (running_) {
        this_thread::sleep_for(5s);
        runOnce();
      }
    });
  }

  void runOnce() {
    recordLock_.lock();
    vector<Record> lines = lines_;
    lines_.clear();
    recordLock_.unlock();

    for (const Record &record : lines) {
      parseRecord(record);
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
      auto json = nljson::parse(record.line_);
      if (!json.is_object()) {
        LOG(INFO) << (record.upload_ ? "[upload]" : "[download]")
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
          LOG(INFO) << "[upload] missing method, json: " << record.line_;
        }

      } else {
        // download: pool -> miner
      }
    } catch (const nlexception &ex) {
      LOG(INFO) << (record.upload_ ? "[upload]" : "[download]")
        << " json parser exception: " << ex.what() << ", json: " << record.line_;
    }
  }

  void parseSubmitLogin(nljson &&json) {
    auto params = json["params"];
    if (!params.is_array() || params.size() < 1) {
      LOG(INFO) << "[upload] missing params, json: " << json;
      return;
    }

    string user = params[0].get<string>();
    string password;
    if (params.size() >= 2) {
      password = json["params"][1].get<string>();
    }
    if (json["worker"].is_string() && json["worker"].size() > 0) {
      user += "." + json["worker"].get<string>();
    }

    worker_.setNames(user, password);
  }
};
