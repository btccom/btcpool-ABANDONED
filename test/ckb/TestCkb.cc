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