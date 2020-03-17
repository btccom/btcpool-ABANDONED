#include "StratumTellor.h"
#include "Utils.h"
#include "uint256.h"
#include "utilities_js.hpp"
#include <glog/logging.h>

StratumJobTellor::StratumJobTellor()
  : nTime_(0U) {
}

//{"challenge":"35b1ac3585ee147308a8dd5183dee7d2327144a04efd27ec9446ea02316e8a1b","difficulty":639715540109038,"request_id":6,"public_address":"40d10136f476a22edb7f0c32231b38aabd5d3244","height":9606505}

bool StratumJobTellor::initFromRawJob(const string &msg) {
  JsonNode j;
  if (!JsonNode::parse(msg.c_str(), msg.c_str() + msg.length(), j)) {
    LOG(ERROR) << "deserialize Ckb work failed " << msg;
    return false;
  }
  if (j.type() != Utilities::JS::type::Obj ||
      j["challenge"].type() != Utilities::JS::type::Str ||
      j["public_address"].type() != Utilities::JS::type::Str ||
      j["request_id"].type() != Utilities::JS::type::Int ||
      j["height"].type() != Utilities::JS::type::Int ||
      j["difficulty"].type() != Utilities::JS::type::Int) {
    LOG(ERROR) << "work format not expected";
    return false;
  }

  nTime_ = time(nullptr);
  challenge_ = j["challenge"].str();
  publicAddress_ = j["public_address"].str();
  requestId_ = j["request_id"].uint64();
  height_ = j["height"].uint64();
  difficulty_ = j["difficulty"].uint64();

  return true;
}

string StratumJobTellor::serializeToJson() const {

  return Strings::Format(
      "{\"created_at_ts\":%u"
      ",\"jobid\":%" PRIu64
      ""
      ",\"challenge\":\"%s\""
      ",\"public_address\":\"%s\""
      ",\"height\":%" PRIu64
      ""
      ",\"request_id\":%" PRIu64
      ""
      ",\"difficulty\":%" PRIu64
      ""
      ",\"timestamp\":%" PRIu64
      ""
      "}",
      nTime_,
      jobId_,
      challenge_.c_str(),
      publicAddress_.c_str(),
      height_,
      requestId_,
      difficulty_,
      timestamp_);
}

bool StratumJobTellor::unserializeFromJson(const char *s, size_t len) {
  JsonNode j;
  if (!JsonNode::parse(s, s + len, j)) {
    return false;
  }
  if (j["created_at_ts"].type() != Utilities::JS::type::Int ||
      j["jobid"].type() != Utilities::JS::type::Int ||
      j["challenge"].type() != Utilities::JS::type::Str ||
      j["public_address"].type() != Utilities::JS::type::Str ||
      j["height"].type() != Utilities::JS::type::Int ||
      j["request_id"].type() != Utilities::JS::type::Int ||
      j["difficulty"].type() != Utilities::JS::type::Int ||
      j["timestamp"].type() != Utilities::JS::type::Int) {
    LOG(ERROR) << "parse bytom stratum job failure: " << s;
    return false;
  }

  nTime_ = j["created_at_ts"].uint64();
  jobId_ = j["jobid"].uint64();
  challenge_ = j["challenge"].str();
  publicAddress_ = j["public_address"].str();
  height_ = j["height"].uint64();
  requestId_ = j["request_id"].uint64();
  // difficulty_ = uint256S(j["difficulty"].str());
  difficulty_ = j["difficulty"].uint64();
  timestamp_ = j["timestamp"].uint64();
  return true;
}
