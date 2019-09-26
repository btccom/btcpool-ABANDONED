#include "gtest/gtest.h"
#include "ckb/CommonCkb.h"

TEST(CommonCkb, eaglesong_V1) {
  uint256 pow_hash = uint256S(
      "6349f73a19471aff5aa5189b018c5d6db7e435c14d00bccfba5a6896bdfc15cf");
  arith_uint256 hash = CKB::GetEaglesongHash(pow_hash, 0x9a93e9597317c7a);
  ASSERT_EQ(
      hash.GetHex(),
      "0000154f991e76b9fcf40c80a43dc3e8cddd02e9b3b0c3e05078e41084406cb9");
}

TEST(CommonCkb, difficulty_V2) {
  uint256 pow_hash = uint256S(
      "2860e9966c50829a76e650dc4abdf49c925d2fd116eab69cd7bc1ae6673225ef");
  arith_uint256 hash =
      CKB::GetEaglesongHash2(pow_hash, htobe64(0x3e29d5eaf71970c0));
  ASSERT_EQ(
      hash.GetHex(),
      "0000dfd9214a52ee0860d988e66c1799847744ef43155b8e00c3f6e3948dbb93");
}

#include "Utils.h"
#include "utilities_js.hpp"

std::map<uint64_t/*height*/, double/*rewards*/> height2reword_;
double getblockreeward() {
  if(height2reword_.find(1000) != height2reword_.end()) {
    return height2reword_[1000];
  }
  string getTipNum = "{\"id\" : 2, \"jsonrpc\" : \"2.0\", \"method\" : \"get_tip_block_number\", \"params\" : []}";
  string response = "", blockHash = "";
  double  blockreward = 0.0;

  for (;;) {
    bool res = blockchainNodeRpcCall("http://localhost:8114", "", getTipNum.c_str(), response);
    LOG(INFO) << "request : " << getTipNum ;
    LOG(INFO) << response;
    if(res) {
      JsonNode r;
      if (JsonNode::parse(
              response.c_str(), response.c_str() + response.length(), r)&& std::strtoull(r["result"].str().c_str(), 0 ,16) >= 9354 + 11) {
            LOG(INFO) << "height : " << std::strtoull(r["result"].str().c_str(), 0 ,16);
            break;
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  }
  string getBlockHash = Strings::Format(
          "{ \"id\" : 2, \"jsonrpc\" : \"2.0\",\"method\" : \"get_header_by_number\", \"params\" : [\"0x%x\"]}",
          9354);
  bool res = blockchainNodeRpcCall("http://localhost:8114", "", getBlockHash.c_str(), response);
  if(res) {
    JsonNode r;
    if (JsonNode::parse(
            response.c_str(), response.c_str() + response.length(), r)) {
      blockHash = r["result"]["hash"].str();
      LOG(INFO) << "block hash : " << blockHash;
    }
  }

  string getBlockReward = Strings::Format(
        "{ \"id\" : 2, \"jsonrpc\" : \"2.0\",\"method\" : \"get_cellbase_output_capacity_details\", \"params\" : [\"%s\"]}",
        blockHash.c_str());
  
  res = blockchainNodeRpcCall("http://localhost:8114", "", getBlockReward.c_str(), response);
  if(res) {
    JsonNode r;
    if (JsonNode::parse(
            response.c_str(), response.c_str() + response.length(), r)) {
      string reward_t = r["result"]["total"].str();
      uint64_t blockreward_t = std::strtoull(reward_t.c_str(), 0, 16);
      LOG(INFO) << "blockreward : " << blockreward_t;
      blockreward = blockreward_t;
    }
  }
  return blockreward;
}

TEST(CommonCkb, curl) {
    LOG(INFO) << "rewards : " << getblockreeward();
}