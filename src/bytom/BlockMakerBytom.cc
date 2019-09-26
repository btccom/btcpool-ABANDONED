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
#include "BlockMakerBytom.h"
#include "BytomUtils.h"

#include "utilities_js.hpp"

#include <boost/thread.hpp>

//////////////////////////////////////BlockMakerBytom//////////////////////////////////////////////////
BlockMakerBytom::BlockMakerBytom(
    shared_ptr<BlockMakerDefinition> def,
    const char *kafkaBrokers,
    const MysqlConnectInfo &poolDB)
  : BlockMaker(def, kafkaBrokers, poolDB) {
}

void BlockMakerBytom::processSolvedShare(rd_kafka_message_t *rkmessage) {
  const char *message = (const char *)rkmessage->payload;
  JsonNode r;
  if (!JsonNode::parse(message, message + rkmessage->len, r)) {
    LOG(ERROR) << "decode common event failure";
    return;
  }

  if (r.type() != Utilities::JS::type::Obj ||
      r["nonce"].type() != Utilities::JS::type::Int ||
      r["header"].type() != Utilities::JS::type::Str ||
      r["height"].type() != Utilities::JS::type::Int ||
      r["networkDiff"].type() != Utilities::JS::type::Int ||
      r["userId"].type() != Utilities::JS::type::Int ||
      r["workerId"].type() != Utilities::JS::type::Int ||
      r["workerFullName"].type() != Utilities::JS::type::Str) {
    LOG(ERROR) << "eth solved share format wrong";
    return;
  }

  string bhString = r["header"].str();
  string request = Strings::Format("{\"block_header\": \"%s\"}\n", bhString);

  submitBlockNonBlocking(request);

  // NOTE: Database save is not implemented. Need to setup mysql in test
  // environment
  StratumWorkerPlain worker;
  worker.userId_ = r["userId"].int32();
  worker.workerHashId_ = r["workerId"].int64();
  worker.fullName_ = r["workerFullName"].str();

  uint64_t networkDiff = r["networkDiff"].uint64();
  uint64_t height = r["height"].uint64();
  saveBlockToDBNonBlocking(bhString, height, networkDiff, worker);
}

void BlockMakerBytom::submitBlockNonBlocking(const string &request) {
  for (const auto &itr : def()->nodes) {
    // use thread to submit
    std::thread t(std::bind(
        &BlockMakerBytom::_submitBlockThread,
        this,
        itr.rpcAddr_,
        itr.rpcUserPwd_,
        request));
    t.detach();
  }
}

void BlockMakerBytom::_submitBlockThread(
    const string &rpcAddress,
    const string &rpcUserpass,
    const string &request) {
  string response;
  LOG(INFO) << "submitting block to " << rpcAddress
            << " with request value: " << request;
  rpcCall(
      rpcAddress.c_str(),
      rpcUserpass.c_str(),
      request.c_str(),
      request.length(),
      response,
      "curl");
  LOG(INFO) << "submission result: " << response;
}

void BlockMakerBytom::saveBlockToDBNonBlocking(
    const string &header,
    const uint32_t height,
    const uint64_t networkDiff,
    const StratumWorkerPlain &worker) {
  std::thread t(std::bind(
      &BlockMakerBytom::_saveBlockToDBThread,
      this,
      header,
      height,
      networkDiff,
      worker));
  t.detach();
}

void BlockMakerBytom::_saveBlockToDBThread(
    const string &header,
    const uint32_t height,
    const uint64_t networkDiff,
    const StratumWorkerPlain &worker) {
  const string nowStr = date("%F %T");
  string sql;
  sql = Strings::Format(
      "INSERT INTO `found_blocks` "
      " (`puid`, `worker_id`"
      ", `worker_full_name`"
      ", `height`, `hash`, `rewards`"
      ", `network_diff`, `created_at`)"
      " VALUES (%d,%d,'%s',%u,'%s',%d,%u,'%s');",
      worker.userId_,
      worker.workerHashId_,
      // filter again, just in case
      filterWorkerName(worker.fullName_),
      height,
      header,
      GetBlockRewardBytom(height),
      networkDiff,
      nowStr);

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
  } else {
    LOG(INFO) << "insert found block success for height " << height;
  }
}
