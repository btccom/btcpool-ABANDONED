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
#include "bitcoin/BitcoinUtils.h"
#include "eth/CommonEth.h"
#include "beam/CommonBeam.h"

TEST(Utils, Strings_Format_1) {
  for (int i = 1; i < 1024; i++) {
    string s;
    for (int j = 0; j < i; j++) {
      s += "A";
    }
    string s1 = Strings::Format("%s", s);
    ASSERT_EQ(s1, s);
  }
}

TEST(Utils, Strings_Append_1) {
  for (int i = 1; i < 1024; i++) {
    string s;
    for (int j = 0; j < i; j++) {
      s += "A";
    }
    string s1;
    Strings::Append(s1, "%s", s);
    ASSERT_EQ(s1, s);
  }
}

TEST(Utils, Strings_Format_2) {
  uint64_t u64 = 18446744073709551100u;
  uint32_t u32 = 4294963333u;
  uint16_t u16 = 65401u;
  uint8_t u8 = 253u;
  int64_t i64 = -9223372036854775333;
  int32_t i32 = -2147483123;
  int16_t i16 = -12394;
  int8_t i8 = -125;
  float f32 = 3.1415925;
  double f64 = 3.1415926535897;

  string formatResult =
      R"(18446744073709551100,4294963333,65401,253,-9223372036854775333,-2147483123,-12394,-125,3.1415925,3.1415926535897)";
  ASSERT_EQ(
      Strings::Format(
          "%u,%u,%u,%u,%d,%d,%d,%d,%0.7f,%0.13f",
          u64,
          u32,
          u16,
          u8,
          i64,
          i32,
          i16,
          i8,
          f32,
          f64),
      formatResult);
  ASSERT_EQ(
      Strings::Format(
          "%lu,%lu,%lu,%lu,%ld,%ld,%ld,%ld,%0.7lf,%0.13lf",
          u64,
          u32,
          u16,
          u8,
          i64,
          i32,
          i16,
          i8,
          f32,
          f64),
      formatResult);
  ASSERT_EQ(
      Strings::Format(
          "%llu,%llu,%llu,%llu,%lld,%lld,%lld,%lld,%0.7llf,%0.13llf",
          u64,
          u32,
          u16,
          u8,
          i64,
          i32,
          i16,
          i8,
          f32,
          f64),
      formatResult);

  formatResult =
      R"(18446744073709551100,4294963333,65401,253,-9223372036854775333,-2147483123,-12394,-125)";
  ASSERT_EQ(
      Strings::Format(
          "%" PRIu8 ",%" PRIu16 ",%" PRIu32 ",%" PRIu64 ",%" PRId8 ",%" PRId16
          ",%" PRId32 ",%" PRId64,
          u64,
          u32,
          u16,
          u8,
          i64,
          i32,
          i16,
          i8),
      formatResult);

  formatResult =
      R"(fffffffffffffdfc,fffff085,0000ff79,000000fd,80000000000001db,000000008000020d,00000000ffffcf96,ffffff83)";
  ASSERT_EQ(
      Strings::Format(
          "%016llx,%08x,%08x,%08x,%016" PRIx64 ",%016x,%016lx,%08x",
          u64,
          u32,
          u16,
          u8,
          i64,
          i32,
          i16,
          i8),
      formatResult);
}

TEST(Utils, Strings_Append_2) {
  uint64_t u64 = 18446744073709551100u;
  uint32_t u32 = 4294963333u;
  uint16_t u16 = 65401u;
  uint8_t u8 = 253u;
  int64_t i64 = -9223372036854775333;
  int32_t i32 = -2147483123;
  int16_t i16 = -12394;
  int8_t i8 = -125;
  float f32 = 3.1415925;
  double f64 = 3.1415926535897;

  string formatResult1 =
      R"(18446744073709551100,4294963333,65401,253,-9223372036854775333,-2147483123,-12394,-125,3.1415925,3.1415926535897)";
  {
    string dest = formatResult1;
    string appendResult = formatResult1 + formatResult1;
    Strings::Append(
        dest,
        "%u,%u,%u,%u,%d,%d,%d,%d,%0.7f,%0.13f",
        u64,
        u32,
        u16,
        u8,
        i64,
        i32,
        i16,
        i8,
        f32,
        f64);
    ASSERT_EQ(dest, appendResult);
  }

  string formatResult2 =
      R"(18446744073709551100,4294963333,65401,253,-9223372036854775333,-2147483123,-12394,-125)";
  {
    string dest = formatResult1;
    string appendResult = formatResult1 + formatResult2;
    Strings::Append(
        dest,
        "%" PRIu8 ",%" PRIu16 ",%" PRIu32 ",%" PRIu64 ",%" PRId8 ",%" PRId16
        ",%" PRId32 ",%" PRId64,
        u64,
        u32,
        u16,
        u8,
        i64,
        i32,
        i16,
        i8);
    ASSERT_EQ(dest, appendResult);
  }

  string formatResult3 =
      R"(fffffffffffffdfc,fffff085,0000ff79,000000fd,80000000000001db,000000008000020d,00000000ffffcf96,ffffff83)";
  {
    string dest = formatResult2;
    string appendResult = formatResult2 + formatResult3;
    Strings::Append(
        dest,
        "%016llx,%08x,%08x,%08x,%016" PRIx64 ",%016x,%016lx,%08x",
        u64,
        u32,
        u16,
        u8,
        i64,
        i32,
        i16,
        i8);
    ASSERT_EQ(dest, appendResult);
  }
}

TEST(Utils, Strings_Format_3) {
  string a = "123";
  string b = "456";
  string c = "789";
  string d = "012";
  string e;
  string f;

  ASSERT_EQ(
      Strings::Format("%s%s%s%s%s%s", a, b.c_str(), e, c, f, d.c_str()),
      "123456789012");
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

  ASSERT_TRUE(
      Beam_ComputeHash(input, nonce, output, hash, 1 /* BEAM Hash I */));
  ASSERT_EQ(
      Beam_Uint256Conv(hash).ToString(),
      "007bb47a19e35751a5f42f45949e76358843e774caac0efa6441ede89443cc06");

  double diff = Beam_BitsToDiff(bits);
  ASSERT_EQ(Strings::Format("%0.4lf", diff), "25.2098");

  uint256 target = Beam_BitsToTarget(bits);
  ASSERT_EQ(
      target.ToString(),
      "0a3d70a3d70a3d70a3d70a3d70a3d70a3d70a3d70a3d70a3d70a3d70a3d70a3d");
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

  ASSERT_TRUE(
      Beam_ComputeHash(input, nonce, output, hash, 1 /* BEAM Hash I */));
  ASSERT_EQ(
      Beam_Uint256Conv(hash).ToString(),
      "631812289123ed3f64a10d2c3afa942fdfa76fc12ae1c9459f1da21c46205901");

  double diff = Beam_BitsToDiff(bits);
  ASSERT_EQ(Strings::Format("%0.4lf", diff), "24.8926");

  uint256 target = Beam_BitsToTarget(bits);
  ASSERT_EQ(
      target.ToString(),
      "0aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
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
      "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");

  target = Beam_DiffToTarget((uint64_t)2);
  ASSERT_EQ(
      target.ToString(),
      "7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");

  target = Beam_DiffToTarget((uint64_t)1024);
  ASSERT_EQ(
      target.ToString(),
      "003fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
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

#ifdef CHAIN_TYPE_ZEC
TEST(Utils, EquihashCompute) {
  SelectParams(CBaseChainParams::TESTNET);

  CBlockHeader header;
  header.nVersion = 0x00000004;
  header.hashPrevBlock = uint256S(
      "0003a3443f44e028aa899f9a0fd8bea7623a5cf1ee25e2b9f088b789e092f5c3");
  header.hashMerkleRoot = uint256S(
      "8b50a41b2427c1cea8b9a6d5f26b641c29149522b2978ef9b01a71fb28c01724");
  header.hashFinalSaplingRoot = uint256S(
      "05606dd751e30275923b47918d33f1ef037034b40c69aab59e5b95a50506ef9f");
  header.nTime = 0x5c83c95d;
  header.nBits = 0x1f16dc04;
  header.nNonce = uint256S(
      "000000000000000000000000000000e600000001000000000000000000000000");
  string solution =
      "fd40050055bb12dc8ec21eceb2a8cd3c63c12eb5f4d5edc11ea10275f256eecbd1faf4b6"
      "82de5dc940e895d0991a6c9e6f6b9e4895f12fb4dc71bcb23a761c5cc0385f51aee503a5"
      "33abe84ce86da4f5f9624219d64d90061124408d125faf29b194bff1f28b89c6c1b527ab"
      "22eef9e75149ddf3f4b8832b27fedb00ebd5341b0609cbebed7925e2dbf02ac33f03b29b"
      "9b28419a92521021956821c8b2867612d1743a4eed98ba9cb5f8310a426dc21d9ad49bb6"
      "8d81dc6c124b1df07e347c0a107c99afcd884bfbc5fa71da5e22379223e9953cae4fdcfe"
      "202067099f9fcc9518a4bf2d91e045d551d0527145b7552325d14aa20b0535e336f6f7fd"
      "1ff9de0d59df37922b1c338a2fc4da95dc52ae034effb9a5134ec0eb0c5741fbef8fe4df"
      "d96e0085557575bc931b0682e4bb0e9d0a8df9e8822cf06ed22d22d423401d40652d0d4b"
      "d465bbbf363f68f007d74fefddbe39015a214062d03340d3c33311954a84016ea2b41407"
      "252b6d3733ca1cfbedba77fea6d31c2aa4b699eb3405b225cb7d65a987ec6350c33a4807"
      "1da481364e9b25e34fe84294158d79d632d647b85878ded4312b76052dc8d63da1e75d18"
      "a6752e902e2431632b3f8352133b82a13b95b0cd2d68d5c5fbf22279c677d9c18932c2d2"
      "6383349b81e98df8d05de65ef297a897ba633f239ffb8ed6fe1fba9695d6d4c24d69fec9"
      "7762ef0225f697e19d38bff20763b67a3b4dcb88769ea28e023aeded75d2abebffd9a3db"
      "1656edcde0561ffd3e0f2fdd39c14d07db8cbb03ee8a658531ea7271336911a7e1353b6b"
      "68336ceb35cb1b5f512398cbbd9bc90b1bba4f224a8a4b6011b18a8b8cf22a705a5c0acf"
      "50b09e3c1da8809f8084a58d01e914238b8c7e541f2de0757d94942158c46de6f4354b85"
      "b1ef875384f274fc03a8e132dd5fd6ef889d0cd49cfe816ff90fa401730cdf199e4ba1bc"
      "457289e3a00b711426cb794f15d94d685acf0c5ce0f0d2023fb8e52e1dd97c53be0d3197"
      "df9530c981e65055cf6fb3462ede5177165b0d4d530a2ac3d690b3c7f577253f0235f2b3"
      "7e310b03e95b875dca8541817c149e1ab9fcab15527e09da3cfd25fa4b975957fb3d482a"
      "38655ff62881d37f760e65f21be0055a95a6d324ae58ab72695eae5264ca1613fe229f20"
      "17ad956b33569dac52fb39c5fa4eb9071550653b99766f3e3d5728f7775521dcdfbf5bcd"
      "1d2ecfabf05859a3a511559de0c19451aade9531f41ed47f0c2f4f1d9d0517134ab2e097"
      "554c50ad695c5248d7f7e3e797f566e80637d8f02363032adac95a086517268d12807f12"
      "a344d9d3faa68570514eaf3f5b9a8b26799717c8e03916e980f31962e395dac57b095705"
      "986961c8cd85b554e507f91079ed909ac64819cceac56a4d3e05fb01c571017938a6468e"
      "96d2ab0196b31a8a4d975112d79420715ae3cf55bbbb0a662cc899a78a0e20bde360159b"
      "335013233deb5fecc40eddde8c37e9511f62c9243304ee0f6df27e9ce89371997ec9e8f6"
      "d871dc4f5ade306f5433d9d4fff45108c405c6d547280eccfc509dbd87e5e524bcfb08d0"
      "4d9d2b7532a87e735fc2a87572e08ec63ba75de39414a3be66e86a496f9631318864c54b"
      "6d81cef4c1a038db5342898f4b0b36796813e5f6bf4e0a041915e40276b4a514d843b789"
      "31b05856b014216abc9fb32b161354d7928abe14b3ecb3697ed0454cea51083686112c64"
      "db6a8723c7c9f6a1e241a7e8c615833504fc23378f021b2b8715cb6a84e19ccf75a1f3b1"
      "f52bae07b2f510224fd3a1a9b060fd1c7d3cfcbcea7e280a14d8efbd8fabbd43bd87a473"
      "8fb8d7999e21bbe55f0a1f7614dd4f48e6a438d65a0e6759aae246bee08332ee8d36a26a"
      "923fef3a74061fecd731d35b512f5a";
  header.nSolution =
      ParseHex(solution.substr(getSolutionVintSize() * 2).c_str());

  ASSERT_EQ(CheckEquihashSolution(&header, Params()), true);
  ASSERT_EQ(
      header.GetHash().ToString(),
      "006f5dca69b024e5bf0647275f6223551090ddb7dda5e89afe2c8f548c6ad580");
}
#endif
