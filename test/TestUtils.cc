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
#include <glog/logging.h>
#include <uint256.h>
#include "libethash/ethash.h"
#include "gtest/gtest.h"
#include "Common.h"
#include "Utils.h"
#include "eth/CommonEth.h"
#include "beam/CommonBeam.h"

TEST(Utils, Strings_Format) {
  for (int i = 1; i < 1024; i++) {
    string s;
    for (int j = 0; j < i; j++) {
      s += "A";
    }
    string s1 = Strings::Format("%s", s.c_str());
    ASSERT_EQ(s1, s);
  }
}

TEST(Utils, Strings_Append) {
  for (int i = 1; i < 1024; i++) {
    string s;
    for (int j = 0; j < i; j++) {
      s += "A";
    }
    string s1;
    Strings::Append(s1, "%s", s.c_str());
    ASSERT_EQ(s1, s);
  }
}

TEST(Utils, date) {
  // 1418051254 -> 2014-12-08 15:07:34 GMT
  ASSERT_EQ(date("%F %T", 1418051254), "2014-12-08 15:07:34");
}

TEST(Utils, share2HashrateG) {
  double h;

  // if minser's hashrate is 15 Ehs, the diff will be around 32,000,000,000.
  // if diff = 32,000,000,000, every 10 secons per share,
  // hashrate will 1.37439e+10 Ghs ~ 13743895347 Ghs
  h = share2HashrateG(32000000000, 10);
  ASSERT_EQ((int64_t)h, 13743895347);

  // if diff = 1, every 10 secons per share,
  // hashrate will 0.429497 Ghs ~ 429 Mhs
  h = share2HashrateG(1, 10);
  ASSERT_EQ((int64_t)(h * 1000), 429);
}

TEST(Utils, Bin2Hex) {
  const vector<char> bin = {(char)0xF0,
                            (char)0xFA,
                            (char)0x6E,
                            (char)0xCD,
                            (char)0xCD,
                            (char)0xCD,
                            (char)0xCD,
                            (char)0xCD};
  const string rightHex = "f0fa6ecdcdcdcdcd";
  const string rightHexReverse = "cdcdcdcdcd6efaf0";

  string result1;
  Bin2Hex(bin, result1);
  EXPECT_EQ(result1, rightHex);
  EXPECT_NE(result1, rightHexReverse);

  string result2;
  Bin2HexR(bin, result2);
  EXPECT_EQ(result2, rightHexReverse);
  EXPECT_NE(result2, rightHex);
}

/*
* Logs from beam-node
I 2019-01-03.11:52:01.533 GenerateNewBlock: size of block = 295; amount of tx =
0 I 2019-01-03.11:52:01.533 Block generated: Height=151, Fee=0,
Difficulty=04-935b7c(25.2098), Size=295 I 2019-01-03.11:52:01.533 New job for
external miner I 2019-01-03.11:52:01.533 stratum server new job 151 will be sent
to 1 connected peers I 2019-01-03.11:52:01.533 send:
{"difficulty":76766076,"id":"151","input":"1b77cd8835ad65f95613a8934114663b6610fe7fdd1600bd0792d40fd1bd001f","jsonrpc":"2.0","method":"job"}
I 2019-01-03.11:52:01.534 Mining nonce = eb5764f7f0b56497
I 2019-01-03.11:52:01.908 recv: {"method" : "solution", "id": "151", "nonce":
"957125643e939c09", "output":
"04bc2cad3a09e0cb21766a4849104a332e567251bc1e272163000bd24532b3dec4190fc3b31b8d42ae6c25e592e5ece09f77d28a58e0fe3b161cb97b68dfda6c3c6029efa5a12cc9e69aa6cd4676719adabab9a9ba15e38bda1c0d8c3090af30d0999f909b5498ce",
"jsonrpc":"2.0" } D 2019-01-03.11:52:01.909  sol.nonce=957125643e939c09
sol.output=04bc2cad3a09e0cb21766a4849104a332e567251bc1e272163000bd24532b3dec4190fc3b31b8d42ae6c25e592e5ece09f77d28a58e0fe3b161cb97b68dfda6c3c6029efa5a12cc9e69aa6cd4676719adabab9a9ba15e38bda1c0d8c3090af30d0999f909b5498ce
I 2019-01-03.11:52:01.909 stratum server solution to 151 from 127.0.0.1:2717
*/
TEST(Utils, BeamEquiHash_1) {
  string input =
      "1b77cd8835ad65f95613a8934114663b6610fe7fdd1600bd0792d40fd1bd001f";
  string output =
      "04bc2cad3a09e0cb21766a4849104a332e567251bc1e272163000bd24532b3dec4190fc3"
      "b31b8d42ae6c25e592e5ece09f77d28a58e0fe3b161cb97b68dfda6c3c6029efa5a12cc9"
      "e69aa6cd4676719adabab9a9ba15e38bda1c0d8c3090af30d0999f909b5498ce";
  uint64_t nonce = 0x957125643e939c09ull;
  uint32_t bits = 76766076ul;
  beam::Difficulty::Raw hash;

  ASSERT_TRUE(Beam_ComputeHash(input, nonce, output, hash));
  ASSERT_EQ(
      Beam_Uint256Conv(hash).ToString(),
      "007bb47a19e35751a5f42f45949e76358843e774caac0efa6441ede89443cc06");

  double diff = Beam_BitsToDiff(bits);
  ASSERT_EQ(Strings::Format("%0.4lf", diff), "25.2098");

  uint256 target = Beam_BitsToTarget(bits);
  ASSERT_EQ(
      target.ToString(),
      "c0b7351900000000000000000000000000000000000000000000000000000000");
}

TEST(Utils, BeamEquiHash_2) {
  string input =
      "e936a073a3478210e52a098120210b690ac046c6dfa13152bef72d728ec60c99";
  string output =
      "0294d542f7feb7daa6731e565231c1e3fe7889891da5b7c5c1a463d0cc1db347f7c5d3be"
      "4ea5e39e2bf47c45693ca08cf36977c33f03c27589e4695c12426b29d241e016d742c0f7"
      "58ee0e100de84a47a723bb716cca1dd7b57d7f8d03a4e8884b6eff8c4fe7ce9f";
  uint64_t nonce = 0x937125643e939c09ull;
  uint32_t bits = 76433405ul;
  beam::Difficulty::Raw hash;

  ASSERT_TRUE(Beam_ComputeHash(input, nonce, output, hash));
  ASSERT_EQ(
      Beam_Uint256Conv(hash).ToString(),
      "631812289123ed3f64a10d2c3afa942fdfa76fc12ae1c9459f1da21c46205901");

  double diff = Beam_BitsToDiff(bits);
  ASSERT_EQ(Strings::Format("%0.4lf", diff), "24.8926");

  uint256 target = Beam_BitsToTarget(bits);
  ASSERT_EQ(
      target.ToString(),
      "d07fe41800000000000000000000000000000000000000000000000000000000");
}

TEST(Utils, BeamDiffToBits) {
  for (uint64_t i = 1; i != 0; i <<= 1) {
    double diff = Beam_BitsToDiff(Beam_DiffToBits(i));
    ASSERT_EQ(i, (uint64_t)diff);
  }
}

TEST(Utils, BeamDiff1ToTarget) {
  uint256 target = Beam_DiffToTarget((uint64_t)1);
  ASSERT_EQ(
      target.ToString(),
      "0000000100000000000000000000000000000000000000000000000000000000");

  target = Beam_DiffToTarget((uint64_t)2);
  ASSERT_EQ(
      target.ToString(),
      "0000000200000000000000000000000000000000000000000000000000000000");

  target = Beam_DiffToTarget((uint64_t)1024);
  ASSERT_EQ(
      target.ToString(),
      "0000000004000000000000000000000000000000000000000000000000000000");
}

TEST(Utils, EthashCompute) {
#ifdef NDEBUG
  uint32_t height = 0x6eab2a;
  uint64_t nonce = 0x41ba179e96428b55;
  uint256 header = uint256S(
      "0x729a3740005234239728098a2d75855f5cb0fd7c536ad1337013bbc5159aefce");

  ethash_h256_t etheader;
  Uint256ToEthash256(header, etheader);

  ethash_light_t light;
  light = ethash_light_new(height);

  ethash_return_value_t r;
  r = ethash_light_compute(light, etheader, nonce);

  ASSERT_EQ(r.success, true);

  uint256 hash = Ethash256ToUint256(r.result);
  ASSERT_EQ(
      hash.ToString(),
      "0000000042901566d9a95493277579e6bca96c8c6bc5998c73f4fd96c8f63627");

  ethash_light_delete(light);
#else
  LOG(INFO) << "ethash_light_new() in debug build was too slow, skip the test.";
#endif
}
