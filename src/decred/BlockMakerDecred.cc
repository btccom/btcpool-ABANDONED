/*
 The MIT License (MIT)

 Copyright (c) [2018] [BTC.COM]

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

#include "BlockMakerDecred.h"
#include "StratumDecred.h"
#include "DecredUtils.h"

BlockMakerDecred::BlockMakerDecred(
    shared_ptr<BlockMakerDefinition> def,
    const char *kafkaBrokers,
    const MysqlConnectInfo &poolDB)
  : BlockMaker(def, kafkaBrokers, poolDB) {
}

void BlockMakerDecred::processSolvedShare(rd_kafka_message_t *rkmessage) {
  if (rkmessage->len != sizeof(FoundBlockDecred)) {
    return;
  }
  auto foundBlock = reinterpret_cast<FoundBlockDecred *>(rkmessage->payload);

  // TODO: Think about a better way to do it asynchronously for all block
  // makers...
  for (auto &node : def()->nodes) {
    std::thread t(std::bind(
        &BlockMakerDecred::submitBlockHeader, this, node, foundBlock->header_));
    t.detach();
  }
  std::thread d(std::bind(&BlockMakerDecred::saveBlockToDB, this, *foundBlock));
  d.detach();
}

void BlockMakerDecred::submitBlockHeader(
    const NodeDefinition &node, const BlockHeaderDecred &header) {
  // RPC call getwork with padded block header as data parameter is equivalent
  // to submitbblock
  string request =
      "{\"jsonrpc\":\"1.0\",\"id\":\"1\",\"method\":\"getwork\",\"params\":[\"";
  request +=
      HexStr(BEGIN(header), END(header)) + "8000000100000000000005a0\"]}";

  LOG(INFO) << "submit block to: " << node.rpcAddr_ << ", request: " << request;
  // try N times
  for (size_t i = 0; i < 3; i++) {
    string response;
    bool res = blockchainNodeRpcCall(
        node.rpcAddr_.c_str(),
        node.rpcUserPwd_.c_str(),
        request.c_str(),
        response);

    // success
    if (res == true) {
      LOG(INFO) << "rpc call success, submit block response: " << response;
      break;
    }

    // failure
    LOG(ERROR) << "rpc call fail: " << response;
  }
}

void BlockMakerDecred::saveBlockToDB(const FoundBlockDecred &foundBlock) {
  auto &header = foundBlock.header_;
  const string nowStr = date("%F %T");
  string sql = Strings::Format(
      "INSERT INTO `found_blocks` "
      " (`puid`, `worker_id`, `worker_full_name`, `job_id`"
      "  ,`height`, `hash`, `rewards`, `size`, `prev_hash`"
      "  ,`bits`, `version`, `voters`, `network`, `created_at`)"
      " VALUES (%d,%d,\"%s\",%u,%d,\"%s\",%d,%d,\"%s\",%u,%d,%u,%u,\"%s\");",
      foundBlock.userId_,
      foundBlock.workerId_,
      // filter again, just in case
      filterWorkerName(foundBlock.workerFullName_),
      foundBlock.jobId_,
      header.height.value(),
      header.getHash().ToString(),
      GetBlockRewardDecredWork(
          header.height.value(),
          header.voters.value(),
          NetworkParamsDecred::get(foundBlock.network_)),
      header.size.value(),
      header.prevBlock.ToString(),
      header.nBits.value(),
      header.version.value(),
      header.voters.value(),
      static_cast<uint32_t>(foundBlock.network_),
      nowStr);

  LOG(INFO) << "BlockMakerDecred::saveBlockToDB: " << sql;

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
  }
}
