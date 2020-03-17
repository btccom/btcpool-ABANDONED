#include "StratumClientTellor.h"
#include "Utils.h"
#include <boost/endian/buffers.hpp>

#include "CommonTellor.h"

StratumClientTellor::StratumClientTellor(
    bool enableTLS,
    struct event_base *base,
    const string &workerFullName,
    const string &workerPasswd,
    const libconfig::Config &config)
  : StratumClient{enableTLS, base, workerFullName, workerPasswd, config} {
}

string StratumClientTellor::constructShare() {
  uint64_t nonceuint = sessionId_;
  nonceuint = (nonceuint << 32) + extraNonce2_;

  string s = Strings::Format(
      "{\"id\": 10, \"method\": \"mining.submit\", "
      "\"params\": [\"%s\",\"%s\",\"%08llx\"]}\n",
      workerFullName_.c_str(),
      latestJobId_.c_str(),
      extraNonce2_);

  extraNonce2_++;
  return s;
}

void StratumClientTellor::handleLine(const string &line) {
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
      challenge_ = jparamsArr[1].str();
      public_address_ = jparamsArr[2].str();

      DLOG(INFO) << "job id: " << latestJobId_
                 << ", challenge_ : " << challenge_
                 << ", difficulry: " << latestDiff_;
    } else if (jmethod.str() == "mining.set_difficulty") {
      latestDiff_ = jparamsArr[0].uint64();
      DLOG(INFO) << "latestDiff_: " << latestDiff_;
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
        "\"params\": [\"%s\", \"%s\"]}\n",
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
