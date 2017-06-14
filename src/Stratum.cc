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
#include "Stratum.h"

#include "zcash/core_io.h"
#include "zcash/hash.h"
#include "zcash/script/script.h"
#include "zcash/uint256.h"
#include "zcash/util.h"

#include "utilities_js.hpp"
#include "Utils.h"

#include <glog/logging.h>


// filter for woker name and miner agent
string filterWorkerName(const string &workerName) {
  string s;
  s.reserve(workerName.size());

  for (const auto &c : workerName) {
    if (('a' <= c && c <= 'z') ||
        ('A' <= c && c <= 'Z') ||
        ('0' <= c && c <= '9') ||
        c == '-' || c == '.' || c == '_' || c == ':' ||
        c == '|' || c == '^' || c == '/') {
      s += c;
    }
  }

  return s;
}


//////////////////////////////// StratumError ////////////////////////////////
const char * StratumError::toString(int err) {
  switch (err) {
    case NO_ERROR:
      return "no error";

    case JOB_NOT_FOUND:
      return "Job not found (=stale)";
    case DUPLICATE_SHARE:
      return "Duplicate share";
    case LOW_DIFFICULTY:
      return "Low difficulty";
    case UNAUTHORIZED:
      return "Unauthorized worker";
    case NOT_SUBSCRIBED:
      return "Not subscribed";

    case ILLEGAL_METHOD:
      return "Illegal method";
    case ILLEGAL_PARARMS:
      return "Illegal params";
    case IP_BANNED:
      return "Ip banned";
    case INVALID_USERNAME:
      return "Invalid username";
    case INTERNAL_ERROR:
      return "Internal error";
    case TIME_TOO_OLD:
      return "Time too old";
    case TIME_TOO_NEW:
      return "Time too new";

    case UNKNOWN: default:
      return "Unknown";
  }
}

//////////////////////////////// StratumWorker ////////////////////////////////
StratumWorker::StratumWorker(): userId_(0), workerHashId_(0) {}

void StratumWorker::reset() {
  userId_ = 0;
  workerHashId_ = 0;

  fullName_.clear();
  userName_.clear();
  workerName_.clear();
}

string StratumWorker::getUserName(const string &fullName) const {
  auto pos = fullName.find(".");
  if (pos == fullName.npos) {
    return fullName;
  }
  return fullName.substr(0, pos);
}

void StratumWorker::setUserIDAndNames(const int32_t userId, const string &fullName) {
  reset();
  userId_ = userId;

  auto pos = fullName.find(".");
  if (pos == fullName.npos) {
    userName_   = fullName;
  } else {
    userName_   = fullName.substr(0, pos);
    workerName_ = fullName.substr(pos+1);
  }

  // the worker name will insert to DB, so must be filter
  workerName_ = filterWorkerName(workerName_);

  // max length for worker name is 20
  if (workerName_.length() > 20) {
    workerName_.resize(20);
  }

  if (workerName_.empty()) {
    workerName_ = DEFAULT_WORKER_NAME;
  }

  workerHashId_ = calcWorkerId(workerName_);
  fullName_ = userName_ + "." + workerName_;
}

int64_t StratumWorker::calcWorkerId(const string &workerName) {
  int64_t workerHashId = 0;

  // calc worker hash id, 64bits
  // https://en.wikipedia.org/wiki/Birthday_attack
  const uint256 workerNameHash = Hash(workerName.begin(), workerName.end());

  // need to convert to uint64 first than copy memory
  const uint64_t tmpId = strtoull(workerNameHash.ToString().substr(0, 16).c_str(),
                                  nullptr, 16);
  memcpy((uint8_t *)&workerHashId, (uint8_t *)&tmpId, 8);

  if (workerHashId == 0) {  // zero is kept
    workerHashId++;
  }

  return workerHashId;
}


//////////////////////////////////  StratumJob  ////////////////////////////////
static
bool DecodeHexHeader(CBlockHeader& header, const std::string& strHexHeader)
{
  if (!IsHex(strHexHeader))
    return false;

  std::vector<unsigned char> data(ParseHex(strHexHeader));
  CDataStream ssHeader(data, SER_NETWORK, BITCOIN_PROTOCOL_VERSION);
  try {
    ssHeader >> header;
  }
  catch (const std::exception&) {
    return false;
  }

  return true;
}

//-----------------------------------------------------------------

StratumJob::StratumJob() {
  SetNull();
}

void StratumJob::SetNull() {
  jobId_   = 0u;
  height_  = 0;
  txCount_ = 0;
  minTime_ = 0u;
  maxTime_ = 0u;
  header_.SetNull();
}

string StratumJob::serializeToJson() const {
  //
  // we use key->value json string, so it's easy to update system
  // key-value items SHOULD the same as StratumJob::unserializeFromJson()
  //
  CDataStream ssBlock(SER_NETWORK, BITCOIN_PROTOCOL_VERSION);
  ssBlock << header_;
  std::string headerHex = HexStr(ssBlock.begin(), ssBlock.end());

  return Strings::Format("{\"jobId\":%" PRIu64",\"originalHash\":\"%s\","
                         "\"height\":%d,\"txCount\":%d,"
                         "\"minTime\":%u,\"maxTime\":%u,\"header\":\"%s\""
                         "}",
                         jobId_, originalHash_.c_str(),
                         height_, txCount_, minTime_, maxTime_,
                         headerHex.c_str());
}

bool StratumJob::unserializeFromJson(const char *s, size_t len) {
  //
  // key-value items SHOULD the same as StratumJob::serializeToJson()
  //
  JsonNode j;
  if (!JsonNode::parse(s, s + len, j)) {
    goto error;
  }
  if (j["jobId"].type()        != Utilities::JS::type::Int ||
      j["originalHash"].type() != Utilities::JS::type::Str ||
      j["height"].type()       != Utilities::JS::type::Int ||
      j["txCount"].type()      != Utilities::JS::type::Int ||
      j["minTime"].type()      != Utilities::JS::type::Int ||
      j["maxTime"].type()      != Utilities::JS::type::Int ||
      j["header"].type()       != Utilities::JS::type::Str) {
    LOG(ERROR) << "parse stratum job failure: " << s;
    goto error;
  }

  jobId_        = j["jobId"].uint64();
  originalHash_ = j["originalHash"].str();
  minTime_      = j["min_time"].uint32();
  maxTime_      = j["max_time"].uint32();
  height_       = j["height"].int32();
  txCount_      = j["tx_count"].int32();

  if (DecodeHexHeader(header_, j["header"].str()) == false) {
    goto error;
  }
  return true;  // success

error:
  SetNull();
  return false;
}

bool StratumJob::initFromGbt(const char *gbt) {
  //
  //  Kafka Message: KAFKA_TOPIC_RAWGBT
  //  "{\"original_hash\":\"%s\","
  //  "\"height\":%d,\"min_time\":%u,\"max_time\":%u,"
  //  "\"tx_count\":%d,\"created_at\":%u,\"created_at_str\":\"%s\","
  //  "\"block_hex\":\"%s\""
  //  "}"
  //
  JsonNode r;
  if (!JsonNode::parse(gbt, gbt + strlen(gbt), r)) {
    LOG(ERROR) << "parse rawgbt message to json fail";
    return false;
  }
  if (r["original_hash"].type()  != Utilities::JS::type::Str ||
      r["height"].type()         != Utilities::JS::type::Int ||
      r["min_time"].type()       != Utilities::JS::type::Int ||
      r["max_time"].type()       != Utilities::JS::type::Int ||
      r["tx_count"].type()       != Utilities::JS::type::Int ||
      r["created_at"].type()     != Utilities::JS::type::Int ||
      r["created_at_str"].type() != Utilities::JS::type::Str ||
      r["block_hex"].type()      != Utilities::JS::type::Str) {
    LOG(ERROR) << "invalid rawgbt: missing fields";
    return false;
  }

  originalHash_ = r["original_hash"].str();
  minTime_ = r["min_time"].uint32();
  maxTime_ = r["max_time"].uint32();
  height_  = r["height"].int32();
  txCount_ = r["tx_count"].int32();

  // decode block hex
  CBlock block;
  if (DecodeHexBlk(block, r["block_hex"].str()) == false) {
    LOG(ERROR) << "decode block hex failure: " << originalHash_;
    return false;
  }
  // set header
  header_ = block.GetBlockHeader();

  // jobId: timestamp + originalHash, we need to make sure jobId is unique
  // jobId can convert to uint64_t
  const string jobIdStr = Strings::Format("%08x%s", header_.nTime,
                                          originalHash_.substr(0, 8).c_str());
  assert(jobIdStr.length() == 16);
  jobId_ = strtoull(jobIdStr.c_str(), nullptr, 16/* hex */);

  return true;
}

bool StratumJob::isEmptyBlock() {
  return txCount_ == 1 ? true : false;  // only coinbase tx means empty block
}
