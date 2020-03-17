#include "gtest/gtest.h"
#include "uint256.h"
#include "arith_uint256.h"
#include "tellor/CommonTellor.h"
#include "bitcoin/BitcoinUtils.h"

TEST(CommonTellor, GetTellorPowHash) {
  string challenge =
      "0x64966a8be800bd7d993d125a07e5fd93ae291e65f65bd53ae3a03558e4f40dc2";
  string publicaddress = "0xe037ec8ec9ec423826750853899394de7f024fee",
         nonce = "33343434363838";

  ASSERT_EQ(
      tellor::GetTellorPowHash(challenge, publicaddress, nonce).GetHex(),
      "225511ee644932082ad0f7c17d904d055c8d0daa9b03dbb266c405d8d9e5ee45");
}

TEST(CommonTellor, GetTellorPow) {

  auto mode = [](arith_uint256 a, arith_uint256 b) -> arith_uint256 {
    return a - (b * (a / b));
  };

  ASSERT_TRUE(0 == mode(arith_uint256(0x8), arith_uint256(0x2)));
  ASSERT_TRUE(1 == mode(arith_uint256(0x1111111), arith_uint256(0x2)));
}

TEST(CommonTellor, CalCurDiff) {

  ASSERT_TRUE(tellor::calcCurDiff(776845875459869, 65536) == 26787788808961);
  ASSERT_TRUE(tellor::calcCurDiff(811415516917833, 65536) == 1269820840247);
}

TEST(CommonTellor, reversehash) {
  // uint256 reverse32bit(uint256 &&hash)
  uint256 hash1 = uint256S(
      "04c2ea1d136a3f620ea3f9bce5e96a816af5ffc7d76e188047fc7b94e88b7c00");

  auto reverse32 = [](uint256 hash) -> uint256 {
    for (uint32_t *i = (uint32_t *)BEGIN(hash), *j = i + 7; i < j; i++, j--) {
      uint32_t tmp = *i;
      *i = *j;
      *j = tmp;
    }
    return hash;
  };
  uint256 hash2 = reverse32(hash1);

  std::cout << ">>>> hash1 :" << hash1.GetHex() << std::endl;
  std::cout << ">>>> hash2 :" << hash2.GetHex() << std::endl;
}