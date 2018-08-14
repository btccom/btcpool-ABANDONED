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
#include "BlockMakerEth.h"
#include "EthConsensus.h"

#include "utilities_js.hpp"

#include <boost/thread.hpp>

////////////////////////////////////////////////BlockMakerEth////////////////////////////////////////////////////////////////
BlockMakerEth::BlockMakerEth(const BlockMakerDefinition& def, const char *kafkaBrokers, const MysqlConnectInfo &poolDB) 
  : BlockMaker(def, kafkaBrokers, poolDB)
{
  useSubmitBlockDetail_ = checkRpcSubmitBlockDetail();
  if (useSubmitBlockDetail_) {
    LOG(INFO) << "use RPC eth_submitBlockDetail";
  }
  else if (checkRpcSubmitBlock()) {
    LOG(INFO) << "use RPC eth_submitBlock, it has limited functionality and the block hash will not be recorded.";
  }
  else {
    LOG(FATAL) << "Ethereum nodes doesn't support both eth_submitBlockDetail and eth_submitBlock, cannot submit block!";
  }
}

void BlockMakerEth::processSolvedShare(rd_kafka_message_t *rkmessage)
{
  const char *message = (const char *)rkmessage->payload;
  JsonNode r;
  if (!JsonNode::parse(message, message + rkmessage->len, r))
  {
    LOG(ERROR) << "decode common event failure";
    return;
  }

  if (r.type() != Utilities::JS::type::Obj ||
      r["nonce"].type() != Utilities::JS::type::Str ||
      r["header"].type() != Utilities::JS::type::Str ||
      r["mix"].type() != Utilities::JS::type::Str ||
      r["height"].type() != Utilities::JS::type::Int ||
      r["networkDiff"].type() != Utilities::JS::type::Int ||
      r["userId"].type() != Utilities::JS::type::Int ||
      r["workerId"].type() != Utilities::JS::type::Int ||
      r["workerFullName"].type() != Utilities::JS::type::Str ||
      r["chain"].type() != Utilities::JS::type::Str)
  {
    LOG(ERROR) << "eth solved share format wrong";
    return;
  }

  StratumWorker worker;
  worker.userId_ = r["userId"].int32();
  worker.workerHashId_ = r["workerId"].int64();
  worker.fullName_ = r["workerFullName"].str();

  submitBlockNonBlocking(r["nonce"].str(), r["header"].str(), r["mix"].str(), def_.nodes,
                         r["height"].uint32(), r["chain"].str(), r["networkDiff"].uint64(),
                         worker);
}

bool BlockMakerEth::submitBlock(const string &nonce, const string &header, const string &mix,
                                const string &rpcUrl, const string &rpcUserPass) {
  string request = Strings::Format("{\"jsonrpc\": \"2.0\", \"method\": \"eth_submitWork\", \"params\": [\"%s\",\"%s\",\"%s\"], \"id\": 5}\n",
                                   HexAddPrefix(nonce).c_str(),
                                   HexAddPrefix(header).c_str(),
                                   HexAddPrefix(mix).c_str());

  string response;
  bool ok = blockchainNodeRpcCall(rpcUrl.c_str(), rpcUserPass.c_str(), request.c_str(), response);
  DLOG(INFO) << "eth_submitWork request: " << request;
  DLOG(INFO) << "eth_submitWork response: " << response;
  if (!ok) {
    LOG(WARNING) << "Call RPC eth_submitWork failed, node url: " << rpcUrl;
    return false;
  }

  JsonNode r;
  if (!JsonNode::parse(response.c_str(), response.c_str() + response.size(), r)) {
    LOG(WARNING) << "decode response failure, node url: " << rpcUrl << ", response: " << response;
    return false;
  }

  if (r.type() != Utilities::JS::type::Obj || r["result"].type() != Utilities::JS::type::Bool) {
    LOG(WARNING) << "node doesn't support eth_submitWork, node url: " << rpcUrl << ", response: " << response;
    return false;
  }

  return r["result"].boolean();
}

/**
  * RPC eth_submitWorkDetail:
  *     A new RPC to submit POW work to Ethereum node. It has the same functionality as `eth_submitWork`
  *     but returns more details about the submitted block.
  *     It defined by the BTCPool project and implemented in the Parity Ethereum node as a patch.
  * 
  * Params (same as `eth_submitWork`):
  *     [
  *         (string) nonce,
  *         (string) pow_hash,
  *         (string) mix_hash
  *     ]
  * 
  * Result:
  *     [
  *         (bool)           success,
  *         (string or null) error_msg,
  *         (string or null) block_hash,
  *         ... // more fields may added in the future
  *     ]
  */
bool BlockMakerEth::submitBlockDetail(const string &nonce, const string &header, const string &mix,
                                      const string &rpcUrl, const string &rpcUserPass,
                                      string &errMsg, string &blockHash) {
  
  string request = Strings::Format("{\"jsonrpc\": \"2.0\", \"method\": \"eth_submitWorkDetail\", \"params\": [\"%s\",\"%s\",\"%s\"], \"id\": 5}\n",
                                   HexAddPrefix(nonce).c_str(),
                                   HexAddPrefix(header).c_str(),
                                   HexAddPrefix(mix).c_str());

  string response;
  bool ok = blockchainNodeRpcCall(rpcUrl.c_str(), rpcUserPass.c_str(), request.c_str(), response);
  DLOG(INFO) << "eth_submitWorkDetail request: " << request;
  DLOG(INFO) << "eth_submitWorkDetail response: " << response;
  if (!ok) {
    LOG(WARNING) << "Call RPC eth_submitWorkDetail failed, node url: " << rpcUrl;
    return false;
  }

  JsonNode r;
  if (!JsonNode::parse(response.c_str(), response.c_str() + response.size(), r)) {
    LOG(WARNING) << "decode response failure, node url: " << rpcUrl << ", response: " << response;
    return false;
  }

  if (r.type() != Utilities::JS::type::Obj || r["result"].type() != Utilities::JS::type::Array) {
    LOG(WARNING) << "node doesn't support eth_submitWorkDetail, node url: " << rpcUrl << ", response: " << response;
    return false;
  }

  auto result = r["result"].children();

  bool success = false;
  if (result->size() >= 1 && result->at(0).type() == Utilities::JS::type::Bool) {
    success = result->at(0).boolean();
  }

  if (result->size() >= 2 && result->at(1).type() == Utilities::JS::type::Str) {
    errMsg = result->at(1).str();
  }

  if (result->size() >= 3 && result->at(2).type() == Utilities::JS::type::Str) {
    blockHash = result->at(2).str();
  }

  return success;
}

bool BlockMakerEth::checkRpcSubmitBlock() {
  string request = Strings::Format("{\"jsonrpc\": \"2.0\", \"method\": \"eth_submitWork\", \"params\": [\"%s\",\"%s\",\"%s\"], \"id\": 5}\n",
                                   "0x0000000000000000",
                                   "0x0000000000000000000000000000000000000000000000000000000000000000",
                                   "0x0000000000000000000000000000000000000000000000000000000000000000");

  for (const auto &itr : def_.nodes) {
    string response;
    bool ok = blockchainNodeRpcCall(itr.rpcAddr_.c_str(), itr.rpcUserPwd_.c_str(), request.c_str(), response);
    if (!ok) {
      LOG(WARNING) << "Call RPC eth_submitWork failed, node url: " << itr.rpcAddr_;
      return false;
    }

    JsonNode r;
    if (!JsonNode::parse(response.c_str(), response.c_str() + response.size(), r)) {
      LOG(WARNING) << "decode response failure, node url: " << itr.rpcAddr_ << ", response: " << response;
      return false;
    }

    if (r.type() != Utilities::JS::type::Obj || r["result"].type() != Utilities::JS::type::Bool) {
      LOG(WARNING) << "node doesn't support eth_submitWork, node url: " << itr.rpcAddr_ << ", response: " << response;
      return false;
    }
  }

  return true;
}

bool BlockMakerEth::checkRpcSubmitBlockDetail() {
  string request = Strings::Format("{\"jsonrpc\": \"2.0\", \"method\": \"eth_submitWorkDetail\", \"params\": [\"%s\",\"%s\",\"%s\"], \"id\": 5}\n",
                                   "0x0000000000000000",
                                   "0x0000000000000000000000000000000000000000000000000000000000000000",
                                   "0x0000000000000000000000000000000000000000000000000000000000000000");

  if (def_.nodes.empty()) {
    LOG(FATAL) << "Node list is empty, cannot submit block!";
    return false;
  }

  for (const auto &itr : def_.nodes) {
    string response;
    bool ok = blockchainNodeRpcCall(itr.rpcAddr_.c_str(), itr.rpcUserPwd_.c_str(), request.c_str(), response);
    if (!ok) {
      LOG(WARNING) << "Call RPC eth_submitWorkDetail failed, node url: " << itr.rpcAddr_;
      return false;
    }

    JsonNode r;
    if (!JsonNode::parse(response.c_str(), response.c_str() + response.size(), r)) {
      LOG(WARNING) << "decode response failure, node url: " << itr.rpcAddr_ << ", response: " << response;
      return false;
    }

    if (r.type() != Utilities::JS::type::Obj ||
      r["result"].type() != Utilities::JS::type::Array ||
      r["result"].children()->size() < 3 ||
      r["result"].children()->at(0).type() != Utilities::JS::type::Bool)
    {
      LOG(WARNING) << "node doesn't support eth_submitWorkDetail, node url: " << itr.rpcAddr_ << ", response: " << response;
      return false;
    }
  }

  return true;
}

void BlockMakerEth::submitBlockNonBlocking(const string &nonce, const string &header, const string &mix, const vector<NodeDefinition> &nodes,
                                           const uint32_t height, const string &chain, const uint64_t networkDiff, const StratumWorker &worker) {
  boost::thread t(boost::bind(&BlockMakerEth::_submitBlockThread, this,
                              nonce, header, mix, nodes,
                              height, chain, networkDiff, worker));
}

void BlockMakerEth::_submitBlockThread(const string &nonce, const string &header, const string &mix, const vector<NodeDefinition> &nodes,
                                       const uint32_t height, const string &chain, const uint64_t networkDiff, const StratumWorker &worker) {
  string blockHash;

  auto submitBlockOnce = [&]() {
    // try eth_submitWorkDetail
    if (useSubmitBlockDetail_) {
      for (size_t i=0; i<nodes.size(); i++) {
        string errMsg;
        auto node = nodes[i];
        bool success = submitBlockDetail(nonce, header, mix, node.rpcAddr_, node.rpcUserPwd_,
                                        errMsg, blockHash);
        if (success) {
          LOG(INFO) << "eth_submitWorkDetail success, chain: " << chain << ", height: " << height << ", hash: " << blockHash
                    << ", networkDiff: " << networkDiff << ", worker: " << worker.fullName_;
          return true;
        }

        LOG(WARNING) << "eth_submitWorkDetail failed, chain: " << chain << ", height: " << height << ", hash_no_nonce: " << header
                    << ", err_msg: " << errMsg;
      }
    }

    // try eth_submitWork
    {
      for (size_t i=0; i<nodes.size(); i++) {
        auto node = nodes[i];
        bool success = submitBlock(nonce, header, mix, node.rpcAddr_, node.rpcUserPwd_);
        if (success) {
          LOG(INFO) << "eth_submitWork success, chain: " << chain << ", height: " << height << ", hash_no_nonce: " << header
                    << ", networkDiff: " << networkDiff << ", worker: " << worker.fullName_;
          return true;
        }

        LOG(WARNING) << "eth_submitWork failed, chain: " << chain << ", height: " << height << ", hash_no_nonce: " << header;
      }
    }

    return false;
  };

  int retryTime = 5;
  while (retryTime > 0) {
    if (submitBlockOnce()) {
      break;
    }
    sleep(6 - retryTime); // first sleep 1s, second sleep 2s, ...
    retryTime--;
  }

  // Still writing to the database even if submitting failed
  saveBlockToDB(header, blockHash, height, chain, networkDiff, worker);
}

void BlockMakerEth::saveBlockToDB(const string &header, const string &blockHash, const uint32_t height,
                                  const string &chain, const uint64_t networkDiff, const StratumWorker &worker) {
  const string nowStr = date("%F %T");
  string sql;
  sql = Strings::Format("INSERT INTO `found_blocks` "
                        " (`puid`, `worker_id`"
                        ", `worker_full_name`, `chain`"
                        ", `height`, `hash`, `hash_no_nonce`"
                        ", `rewards`"
                        ", `network_diff`, `created_at`)"
                        " VALUES (%ld, %" PRId64
                        ", '%s', '%s'"
                        ", %lu, '%s', '%s'"
                        ", %" PRId64
                        ", %" PRIu64 ", '%s'); ",
                        worker.userId_, worker.workerHashId_,
                        // filter again, just in case
                        filterWorkerName(worker.fullName_).c_str(), chain.c_str(),
                        height, blockHash.c_str(), header.c_str(),
                        EthConsensus::getStaticBlockReward(height, chain),
                        networkDiff, nowStr.c_str());

  // try connect to DB
  MySQLConnection db(poolDB_);
  for (size_t i = 0; i < 3; i++) {
    if (db.ping())
      break;
    else
      sleep(3);
  }

  if (db.execute(sql) == false) {
    LOG(ERROR) << "insert found block failure: " << sql;
  }
  else
  {
    LOG(INFO) << "insert found block success for height " << height;
  }
}
