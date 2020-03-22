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
#include "StatisticsCkb.h"
#include "Utils.h"
#include "utilities_js.hpp"

static std::map<uint64_t /*height*/, double /*rewards*/> height2reword;
static uint64_t lastTipNum = 0;

template <>
double ShareStatsDay<ShareCkb>::getShareReward(const ShareCkb &share) {
  // if share's height existed in map ,return directly.
  if (height2reword.find(share.height()) != height2reword.end()) {
    std::map<uint64_t, double>::iterator iter;
    for (iter = height2reword.begin(); iter != height2reword.end();) {
      if (iter->first < share.height()) {
        iter = height2reword.erase(iter);
      } else {
        iter++;
      }
    }
    return height2reword[share.height()];
  }

  string getTipNum =
      "{\"id\" : 2, \"jsonrpc\" : \"2.0\", \"method\" : "
      "\"get_tip_block_number\", \"params\" : []}";
  string response = "", blockHash = "";
  double blockreward = 0.0;
  // wait for N+11 block submited
  if (height2reword.size() == 0 || share.height() + 11 > lastTipNum) {
    for (;;) {
      bool res = blockchainNodeRpcCall(
          rpcUrl_.c_str(), "", getTipNum.c_str(), response);
      if (res) {
        JsonNode r;
        if (JsonNode::parse(
                response.c_str(), response.c_str() + response.length(), r) &&
            std::strtoull(r["result"].str().c_str(), 0, 16) >=
                share.height() + 11) {
          lastTipNum = std::strtoull(r["result"].str().c_str(), 0, 16);
          LOG(INFO) << "share height : " << share.height()
                    << " network height : " << lastTipNum;
          break;
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(3000));
    }
  }
  // get N+11th blockhash
  string getBlockHash = Strings::Format(
      "{ \"id\" : 2, \"jsonrpc\" : \"2.0\",\"method\" : "
      "\"get_header_by_number\", \"params\" : [\"0x%x\"]}",
      share.height() + 11);
  bool res = blockchainNodeRpcCall(
      rpcUrl_.c_str(), "", getBlockHash.c_str(), response);
  if (res) {
    JsonNode r;
    if (JsonNode::parse(
            response.c_str(), response.c_str() + response.length(), r)) {
      blockHash = r["result"]["hash"].str();
      LOG(INFO) << "block height : " << share.height() + 11
                << " block hash : " << blockHash;
    }
  }
  // get N+11th blockRewards
  string getBlockReward = Strings::Format(
      "{ \"id\" : 2, \"jsonrpc\" : \"2.0\",\"method\" : "
      "\"get_cellbase_output_capacity_details\", \"params\" : [\"%s\"]}",
      blockHash.c_str());

  res = blockchainNodeRpcCall(
      rpcUrl_.c_str(), "", getBlockReward.c_str(), response);
  if (res) {
    JsonNode r;
    if (JsonNode::parse(
            response.c_str(), response.c_str() + response.length(), r)) {
      string reward_t = r["result"]["total"].str();
      uint64_t blockreward_t = std::strtoull(reward_t.c_str(), 0, 16);
      blockreward = blockreward_t;
      height2reword.insert({share.height(), blockreward});
    }
  }
  return blockreward;
}

template class ShareStatsDay<ShareCkb>;
