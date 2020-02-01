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

#include <fmt/format.h>

#include <chrono>
#include <thread>

////////////////////////////////////////////////BlockMakerEth////////////////////////////////////////////////////////////////
BlockMakerEth::BlockMakerEth(
    shared_ptr<BlockMakerDefinition> def,
    const char *kafkaBrokers,
    const MysqlConnectInfo &poolDB)
  : BlockMaker(def, kafkaBrokers, poolDB) {
  if (!checkRpcSubmitBlock()) {
    LOG(FATAL)
        << "One of Ethereum nodes don't support both parity_submitBlockDetail "
           "and eth_submitBlock, cannot submit block to it!";
  }
}

void BlockMakerEth::processSolvedShare(rd_kafka_message_t *rkmessage) {
  const char *message = (const char *)rkmessage->payload;
  JsonNode r;
  if (!JsonNode::parse(message, message + rkmessage->len, r)) {
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
      r["chain"].type() != Utilities::JS::type::Str) {
    LOG(ERROR) << "eth solved share format wrong";
    return;
  }

  StratumWorkerPlain worker;
  worker.userId_ = r["userId"].int32();
  worker.workerHashId_ = r["workerId"].int64();
  worker.fullName_ = r["workerFullName"].str();

  std::string extraNonceStr;
  if (r["extraNonce"].type() == Utilities::JS::type::Int) {
    auto extraNonce = r["extraNonce"].uint32();
    extraNonceStr = fmt::format(",\"0x{:08x}\"", extraNonce);
    LOG(INFO) << "submit a block with extra nonce " << extraNonce;
  } else if (r["extraNonce"].type() == Utilities::JS::type::Str) {
    auto extraNonce = r["extraNonce"].str();
    extraNonceStr = ",\"" + HexAddPrefix(extraNonce) + "\"";
    LOG(INFO) << "submit a block with extra nonce " << extraNonce;
  }

  submitBlockNonBlocking(
      r["nonce"].str(),
      r["header"].str(),
      r["mix"].str(),
      def()->nodes,
      r["height"].uint32(),
      r["chain"].str(),
      r["networkDiff"].uint64(),
      worker,
      extraNonceStr);
}

bool BlockMakerEth::submitBlock(
    const string &nonce,
    const string &header,
    const string &mix,
    const string &extraNonce,
    const string &rpcUrl,
    const string &rpcUserPass,
    bool parity,
    string &errMsg,
    string &blockHash,
    string &request,
    string &response,
    bool &resultFound) {
  resultFound = false;

  /**
  * Use parity_submitBlockDetail and eth_submitBlock at the same time.
  *
  * About RPC parity_submitBlockDetail:
  *     A new RPC to submit POW work to Ethereum node. It has the same
  functionality as `eth_submitWork`
  *     but returns the hash of the submitted block.
  *     When an error occurs, the specific error message will be returned
  instead of a `false`.
  *     It defined by the BTCPool project and implemented in the Parity Ethereum
  node as a private RPC.
  *
  * RPC parity_submitWorkDetail

  * Params (same as `eth_submitWork`):
  *   [
  *       "<nonce>",
  *       "<pow_hash>",
  *       "<mix_hash>"
  *   ]
  *
  * Result on success:
  *   "block_hash"
  *
  * Error on failure:
  *   {code: -32005, message: "Cannot submit work.", data: "<reason for
  submission failure>"}
  *
  * Examples for the RPC calling:
  * <https://github.com/paritytech/parity-ethereum/pull/9404>
  *
  */
  request = Strings::Format(
      "{\"jsonrpc\":\"2.0\",\"method\":\"%s\",\"params\":[\"%s\",\"%s\",\"%s\""
      "%s],\"id\":1}",
      parity ? "parity_submitWorkDetail" : "eth_submitWork",
      HexAddPrefix(nonce).c_str(),
      HexAddPrefix(header).c_str(),
      HexAddPrefix(mix).c_str(),
      extraNonce.c_str());

  bool ok = blockchainNodeRpcCall(
      rpcUrl.c_str(), rpcUserPass.c_str(), request.c_str(), response);
  DLOG(INFO) << "eth_submitWork request for server " << rpcUrl << ": "
             << request;
  DLOG(INFO) << "eth_submitWork response for server " << rpcUrl << ": "
             << response;
  if (!ok) {
    LOG(WARNING) << "Call RPC eth_submitWork failed, node url: " << rpcUrl
                 << ", request: " << request << ", response: " << response;
    return false;
  }

  JsonNode r;
  if (!JsonNode::parse(
          response.c_str(), response.c_str() + response.size(), r)) {
    LOG(WARNING) << "decode response failure, node url: " << rpcUrl
                 << ", request: " << request << ", response: " << response;
    return false;
  }

  if (r.type() != Utilities::JS::type::Obj &&
      r["id"].type() != Utilities::JS::type::Int) {
    LOG(WARNING) << "Result is not a valid JSON-RPC object, node url: "
                 << rpcUrl << ", request: " << request
                 << ", response: " << response;
    return false;
  }

  if (parity) {
    //
    // Success result of parity_submitWorkDetail. Example:
    // {"jsonrpc":"2.0","result":"0x07a992176ab51ee50539c1ba287bef937fe49c9a96dafa03954fb6fefa594691","id":5}
    //
    if (r["result"].type() == Utilities::JS::type::Str) {
      resultFound = true;
      blockHash = r["result"].str();
      return true;
    }

    //
    // Failure result of parity_submitWorkDetail. Example:
    // {"jsonrpc":"2.0","error":{"code":-32005,"message":"Cannot submit
    // work.","data":"PoW hash is invalid or out of date."},"id":5}
    //
    if (r["error"].type() == Utilities::JS::type::Obj &&
        r["error"]["data"].type() == Utilities::JS::type::Str) {
      errMsg = r["error"]["data"].str();
      resultFound = true;
    }

    return false;
  } else {
    //
    // Response of eth_submitWork. Example:
    // {"jsonrpc":"2.0","result":false,"id":5}
    //
    if (r["result"].type() == Utilities::JS::type::Bool) {
      resultFound = true;
      return r["result"].boolean();
    }

    LOG(WARNING) << "Unexpected result, node url: " << rpcUrl
                 << ", request: " << request << ", response: " << response;
    return false;
  }
}

bool BlockMakerEth::checkRpcSubmitBlock() {
  if (def()->nodes.empty()) {
    LOG(FATAL) << "Node list is empty, cannot submit block!";
    return false;
  }

  for (const auto &itr : def()->nodes) {
    string blockHash, errMsg, request, response;
    bool resultFound = false;

    submitBlock(
        "0x0000000000000000",
        "0x0000000000000000000000000000000000000000000000000000000000000000",
        "0x0000000000000000000000000000000000000000000000000000000000000000",
        "",
        itr.rpcAddr_,
        itr.rpcUserPwd_,
        true,
        errMsg,
        blockHash,
        request,
        response,
        resultFound);

    if (resultFound) {
      LOG(INFO) << "Node " << itr.rpcAddr_
                << " supports parity_submitBlockDetail. Block hash will be "
                   "recorded correctly if submit block to it."
                << " Request: " << request << ", response: " << response;
      itr.parity_ = true;
      continue;
    } else {
      submitBlock(
          "0x0000000000000000",
          "0x0000000000000000000000000000000000000000000000000000000000000000",
          "0x0000000000000000000000000000000000000000000000000000000000000000",
          "",
          itr.rpcAddr_,
          itr.rpcUserPwd_,
          false,
          errMsg,
          blockHash,
          request,
          response,
          resultFound);
      if (resultFound) {
        LOG(WARNING)
            << "Node " << itr.rpcAddr_
            << " doesn't supports parity_submitBlockDetail. Block hash "
               "will be empty if submit block to it."
            << " Request: " << request << ", response: " << response;
        itr.parity_ = false;
        continue;
      } else {
        LOG(FATAL) << "Node " << itr.rpcAddr_
                   << " doesn't support both parity_submitBlockDetail and "
                      "eth_submitBlock, cannot submit block to it!"
                   << " Request: " << request << ", response: " << response;
        return false;
      }
    }
  }

  return true;
}

void BlockMakerEth::submitBlockNonBlocking(
    const string &nonce,
    const string &header,
    const string &mix,
    const vector<NodeDefinition> &nodes,
    const uint32_t height,
    const string &chain,
    const uint64_t networkDiff,
    const StratumWorkerPlain &worker,
    const string &extraNonce) {
  // run threads
  for (size_t i = 0; i < nodes.size(); i++) {
    auto t = std::thread(std::bind(
        &BlockMakerEth::_submitBlockThread,
        this,
        nonce,
        header,
        mix,
        nodes[i],
        height,
        chain,
        networkDiff,
        worker,
        extraNonce));
    t.detach();
  }
}

void BlockMakerEth::_submitBlockThread(
    const string &nonce,
    const string &header,
    const string &mix,
    const NodeDefinition &node,
    const uint32_t height,
    const string &chain,
    const uint64_t networkDiff,
    const StratumWorkerPlain &worker,
    const string &extraNonce) {
  string blockHash;

  // unused vars
  string request, response;
  bool resultFound;

  auto submitBlockOnce = [&]() {
    string errMsg;
    bool success = BlockMakerEth::submitBlock(
        nonce,
        header,
        mix,
        extraNonce,
        node.rpcAddr_,
        node.rpcUserPwd_,
        node.parity_,
        errMsg,
        blockHash,
        request,
        response,
        resultFound);
    if (success) {
      LOG(INFO) << "submit block success, chain: " << chain
                << ", height: " << height << ", hash: " << blockHash
                << ", hash_no_nonce: " << header
                << ", networkDiff: " << networkDiff
                << ", worker: " << worker.fullName_;
      return true;
    }

    LOG(WARNING) << "submit block failed, chain: " << chain
                 << ", height: " << height << ", hash_no_nonce: " << header
                 << ", err_msg: " << errMsg;

    // If the node has given a clear result (error message), retrying is
    // uesless.
    return resultFound;
  };

  int retryTime = 5;
  while (retryTime > 0) {
    if (submitBlockOnce()) {
      break;
    }
    std::this_thread::sleep_for(
        6s -
        std::chrono::seconds{
            retryTime}); // first sleep 1s, second sleep 2s, ...
    retryTime--;
  }

  // Still writing to the database even if submitting failed
  saveBlockToDB(nonce, header, blockHash, height, chain, networkDiff, worker);
}

void BlockMakerEth::saveBlockToDB(
    const string &nonce,
    const string &header,
    const string &blockHash,
    const uint32_t height,
    const string &chain,
    const uint64_t networkDiff,
    const StratumWorkerPlain &worker) {
  const string nowStr = date("%F %T");
  string sql;
  sql = Strings::Format(
      "INSERT INTO `found_blocks` "
      " (`puid`, `worker_id`"
      ", `worker_full_name`, `chain`"
      ", `height`, `hash`, `hash_no_nonce`, `nonce`"
      ", `rewards`"
      ", `network_diff`, `created_at`)"
      " VALUES (%d, %d"
      ", '%s', '%s', %u"
      ", '%s', '%s', '%s'"
      ", %d, %u, '%s')",
      worker.userId_,
      worker.workerHashId_,
      // filter again, just in case
      filterWorkerName(worker.fullName_),
      chain,
      height,
      HexAddPrefix(blockHash),
      HexAddPrefix(header),
      HexAddPrefix(nonce),
      EthConsensus::getStaticBlockReward(height, chain),
      networkDiff,
      nowStr);

  if (!blockHash.empty()) {
    // Other thread may have written a record without the hash, so update it
    // here.
    sql += " ON DUPLICATE KEY UPDATE `hash` = VALUES(`hash`)";
  }

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
