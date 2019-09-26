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
#include "StratumClientCkb.h"
#include "Utils.h"
#include <boost/endian/buffers.hpp>

#include "CommonCkb.h"

StratumClientCkb::StratumClientCkb(
    bool enableTLS,
    struct event_base *base,
    const string &workerFullName,
    const string &workerPasswd,
    const libconfig::Config &config)
  : StratumClient{enableTLS, base, workerFullName, workerPasswd, config} {
}

string StratumClientCkb::constructShare() {
  uint64_t nonceuint = sessionId_;
  nonceuint = (nonceuint << 32) + extraNonce2_;
  // auto prePowHash = uint256S(pow_hash_);

  //{"id":102,"method":"mining.submit","params":["ckb1qyq2znu0gempdahctxsm49sa9jdzq9vnka7qt9ntff.worker1","17282f3f","eaf71970c0"]}
  //// params: [username, jobId, nonce2]
  string s = Strings::Format(
      "{\"id\": 10, \"method\": \"mining.submit\", "
      "\"params\": [\"%s\",\"%s\",\"%08llx\"]}\n",
      workerFullName_.c_str(),
      latestJobId_.c_str(),
      extraNonce2_);

  extraNonce2_++;
  return s;
}

void StratumClientCkb::handleLine(const string &line) {
  DLOG(INFO) << "recv(" << line.size() << "): " << line;

  JsonNode jnode;
  if (!JsonNode::parse(line.data(), line.data() + line.size(), jnode)) {
    LOG(ERROR) << "decode line fail, not a json string";
    return;
  }
  JsonNode jresult = jnode["result"];
  JsonNode jerror = jnode["error"];
  JsonNode jmethod = jnode["method"];

  if (jmethod.type() == Utilities::JS::type::Str) {
    JsonNode jparams = jnode["params"];
    auto jparamsArr = jparams.array();

    if (jmethod.str() == "mining.notify") {
      latestJobId_ = jparamsArr[0].str();
      pow_hash_ = jparamsArr[1].str();
      height_ = jparamsArr[2].int64();

      DLOG(INFO) << "job id: " << latestJobId_ << ", header hash: " << pow_hash_
                 << ", target: " << latestTarget_.GetHex();
    } else if (jmethod.str() == "mining.set_target") {
      latestTarget_ = UintToArith256(uint256S(jparamsArr[0].str()));
      DLOG(INFO) << "latestTarget_: " << latestTarget_.GetHex();
    } else {
      LOG(ERROR) << "unknown method: " << line;
    }
    return;
  }

  if (state_ == AUTHENTICATED) {
    //
    // {"error": null, "id": 2, "result": true}
    //
    if (jerror.type() != Utilities::JS::type::Null ||
        jresult.type() != Utilities::JS::type::Bool ||
        jresult.boolean() != true) {
      LOG(ERROR) << "json result is null, err: " << jerror.str()
                 << ", line: " << line;
    }
    return;
  }

  if (state_ == CONNECTED) {
    // mining.authorize
    state_ = SUBSCRIBED;
    string s = Strings::Format(
        "{\"id\": 1, \"method\": \"mining.authorize\","
        "\"params\": [\"\%s\", \"%s\"]}\n",
        workerFullName_.c_str(),
        workerPasswd_.c_str());
    sendData(s);
    return;
  }

  if (state_ == SUBSCRIBED && jresult.boolean() == true) {
    state_ = AUTHENTICATED;
    return;
  }
}
