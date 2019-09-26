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
#include "BlockMakerCkb.h"
#include "utilities_js.hpp"

BlockMakerCkb::BlockMakerCkb(
    std::shared_ptr<BlockMakerDefinition> def,
    const char *kafkaBrokers,
    const MysqlConnectInfo &poolDB)
  : BlockMaker(def, kafkaBrokers, poolDB) {
}

void BlockMakerCkb::processSolvedShare(rd_kafka_message_t *rkmessage) {
  if (rkmessage->err) {
    if (rkmessage->err == RD_KAFKA_RESP_ERR__PARTITION_EOF) {
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

  if (jroot["pow_hash"].type() != Utilities::JS::type::Str ||
      jroot["height"].type() != Utilities::JS::type::Int ||
      jroot["target"].type() != Utilities::JS::type::Str ||
      jroot["nonce"].type() != Utilities::JS::type::Int ||
      jroot["job_id"].type() != Utilities::JS::type::Int) {
    LOG(ERROR) << "solved share json missing fields: " << json;
    return;
  }

  string blockHash = jroot["pow_hash"].str();
  uint64_t height = jroot["height"].uint64();
  string target = jroot["target"].str();
  uint64_t job_id = jroot["job_id"].uint64();
  uint32_t userId = jroot["userId"].uint32();
  int64_t workerId = jroot["workerId"].int64();
  string workerFullName = jroot["workerFullName"].str();

  const string nowStr = date("%F %T");
  string sql = Strings::Format(
      "INSERT INTO `found_blocks` "
      " (`puid`, `worker_id`, `worker_full_name`, `job_id`"
      "  ,`height`, `hash`"
      //"  ,`bits`, `rewards`, `created_at`)"
      ", `rewards`, `created_at`)"
      " VALUES (%d, %" PRId64 ",\"%s\", %" PRIu64 "  ,%" PRIu64
      ",\"%s\",%" PRId64 ",\"%s\");",
      userId,
      workerId,
      filterWorkerName(workerFullName).c_str(),
      job_id,
      height,
      blockHash.c_str(),
      // target,
      // GetCoinbaseReward(height),
      0,
      nowStr.c_str());

  LOG(INFO) << "save share to db : " << sql;

  std::thread t([this, sql, blockHash]() {
    // try connect to DB
    MySQLConnection db(poolDB_);
    for (size_t i = 0; i < 3; i++) {
      if (db.ping())
        break;
      else
        std::this_thread::sleep_for(3s);
    }

    if (db.execute(sql) == false) {
      LOG(ERROR) << "insert found block failure: " << sql;
      return;
    }

    LOG(INFO) << "insert found block " << blockHash << " success";
  });

  t.detach();
}
