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

  string request = Strings::Format("{\"jsonrpc\": \"2.0\", \"method\": \"eth_submitWork\", \"params\": [\"%s\",\"%s\",\"%s\"], \"id\": 5}\n",
                                   HexAddPrefix(r["nonce"].str()).c_str(),
                                   HexAddPrefix(r["header"].str()).c_str(),
                                   HexAddPrefix(r["mix"].str()).c_str());

  submitBlockNonBlocking(request);
  saveBlockToDBNonBlocking(r["header"].str().c_str(), r["height"].uint32(),
                           r["chain"].str().c_str(), r["networkDiff"].uint64(), worker);
}

bool BlockMakerEth::init() {
  //
  // Sloved Share
  //
  // we need to consume the latest 2 messages, just in case
  if (kafkaConsumerSolvedShare_.setup(RD_KAFKA_OFFSET_TAIL(2)) == false) {
    LOG(INFO) << "setup kafkaConsumerSolvedShare_ fail";
    return false;
  }
  if (!kafkaConsumerSolvedShare_.checkAlive()) {
    LOG(ERROR) << "kafka brokers is not alive: kafkaConsumerSolvedShare_";
    return false;
  }

  return true;
}

void BlockMakerEth::submitBlockNonBlocking(const string &blockJson) {
  for (const auto &itr : nodeRpcUri_) {
    // use thread to submit
    boost::thread t(boost::bind(&BlockMakerEth::_submitBlockThread, this,
                                itr.first, itr.second, blockJson));
  }
}

void BlockMakerEth::_submitBlockThread(const string &rpcAddress, const string &rpcUserpass,
                                       const string &blockJson)
{
  string response;
  LOG(INFO) << "submit ETH block: " << blockJson;
  bitcoindRpcCall(rpcAddress.c_str(), rpcUserpass.c_str(), blockJson.c_str(), response);
  LOG(INFO) << "submission result: " << response;
}

void BlockMakerEth::saveBlockToDBNonBlocking(const string &header, const uint32_t height, const string &chain,
                                             const uint64_t networkDiff, const StratumWorker &worker) {
  boost::thread t(boost::bind(&BlockMakerEth::_saveBlockToDBThread, this,
                              header, height, chain, networkDiff, worker));
}

void BlockMakerEth::_saveBlockToDBThread(const string &header, const uint32_t height, const string &chain,
                                         const uint64_t networkDiff, const StratumWorker &worker) {
  const string nowStr = date("%F %T");
  string sql;
  sql = Strings::Format("INSERT INTO `found_blocks` "
                        " (`puid`, `worker_id`"
                        ", `worker_full_name`, `chain`"
                        ", `height`, `hash`, `rewards`"
                        ", `network_diff`, `created_at`)"
                        " VALUES (%ld, %" PRId64
                        ", '%s', '%s'"
                        ", %lu, '%s', %" PRId64
                        ", %" PRIu64 ", '%s'); ",
                        worker.userId_, worker.workerHashId_,
                        // filter again, just in case
                        filterWorkerName(worker.fullName_).c_str(), chain.c_str(),
                        height, header.c_str(), EthConsensus::getStaticBlockReward(height, chain),
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
