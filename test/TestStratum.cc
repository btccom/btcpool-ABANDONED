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

#include "gtest/gtest.h"
#include "Common.h"
#include "Utils.h"
#include "Stratum.h"

#include "bitcoin/chainparams.h"
#include "bitcoin/hash.h"
#include "bitcoin/script/script.h"
#include "bitcoin/uint256.h"
#include "bitcoin/util.h"

#include <stdint.h>

TEST(Stratum, jobId2Time) {
  uint64_t jobId;

  // jobId:   timestamp    +    gbtHash
  //          ----------      ----------
  //           uint32_t        uint32_t
  //  const string jobIdStr = Strings::Format("%08x%s", (uint32_t)time(nullptr),
  //                                          gbtHash.ToString().substr(0, 8).c_str());
  jobId = (1469002809ull << 32) | 0x00000000FFFFFFFFull;
  ASSERT_EQ(jobId2Time(jobId), 1469002809u);

  jobId = (1469002809ull << 32) | 0x0000000000000000ull;
  ASSERT_EQ(jobId2Time(jobId), 1469002809u);
}

TEST(Stratum, Share) {
  Share s;

  ASSERT_EQ(s.isValid(), false);
  ASSERT_EQ(s.score(), 0);
  ASSERT_EQ(s.toString(), "share(jobId: 0, ip: 0.0.0.0, userId: 0, workerId: 0,"
            " timeStamp: 0/1970-01-01 00:00:00, share: 0, blkBits: 00000000, result: 0)");

  s.ip_ = htonl(167772161);  // 167772161 : 10.0.0.1
  ASSERT_EQ(s.toString(), "share(jobId: 0, ip: 10.0.0.1, userId: 0, workerId: 0,"
            " timeStamp: 0/1970-01-01 00:00:00, share: 0, blkBits: 00000000, result: 0)");
}

TEST(Stratum, Share2) {
  Share s;

  s.blkBits_ = 0x1d00ffffu;
  s.share_ = 1ll;
  ASSERT_EQ(s.score(), 1ll);

  s.blkBits_ = 0x18050edcu;
  s.share_ = UINT32_MAX;
  // double will be: 0.0197583
  ASSERT_EQ(score2Str(s.score()), "0.0197582875516673");
}

TEST(Stratum, StratumWorker) {
  StratumWorker w;
  uint64_t u;
  int64_t workerId;

  ASSERT_EQ(w.getUserName("abcd"), "abcd");
  ASSERT_EQ(w.getUserName("abcdabcdabcdabcdabcdabcdabcd"), "abcdabcdabcdabcdabcdabcdabcd");
  ASSERT_EQ(w.getUserName("abcd."), "abcd");
  ASSERT_EQ(w.getUserName("abcd.123"), "abcd");
  ASSERT_EQ(w.getUserName("abcd.123.456"), "abcd");

  //
  // echo -n '123' |openssl dgst -sha256 -binary |openssl dgst -sha256
  //
  w.setUserIDAndNames(INT32_MAX, "abcd.123");
  ASSERT_EQ(w.fullName_,   "abcd.123");
  ASSERT_EQ(w.userId_,     INT32_MAX);
  ASSERT_EQ(w.userName_,   "abcd");
  ASSERT_EQ(w.workerName_, "123");
  // '123' dsha256 : 5a77d1e9612d350b3734f6282259b7ff0a3f87d62cfef5f35e91a5604c0490a3
  //       uint256 : a390044c60a5915ef3f5fe2cd6873f0affb7592228f634370b352d61e9d1775a
  u = strtoull("a390044c60a5915e", nullptr, 16);
  memcpy((uint8_t *)&workerId, (uint8_t *)&u, 8);
  ASSERT_EQ(w.workerHashId_, workerId);


  w.setUserIDAndNames(0, "abcdefg");
  ASSERT_EQ(w.fullName_,   "abcdefg.__default__");
  ASSERT_EQ(w.userId_,     0);
  ASSERT_EQ(w.userName_,   "abcdefg");
  ASSERT_EQ(w.workerName_, "__default__");
  // '__default__' dsha256 : e00f302bc411fde77d954283be6904911742f2ac76c8e79abef5dff4e6a19770
  //               uint256 : 7097a1e6f4dff5be
  u = strtoull("7097a1e6f4dff5be", nullptr, 16);
  memcpy((uint8_t *)&workerId, (uint8_t *)&u, 8);
  ASSERT_EQ(w.workerHashId_, workerId);

  // check allow chars
  w.setUserIDAndNames(0, "abcdefg.azAZ09-._:|^/");
  ASSERT_EQ(w.workerName_, "azAZ09-._:|^/");
  ASSERT_EQ(w.fullName_,   "abcdefg.azAZ09-._:|^/");

  // some of them are bad chars
  w.setUserIDAndNames(0, "abcdefg.~!@#$%^&*()+={}|[]\\<>?,./");
  ASSERT_EQ(w.workerName_, "^|./");
  ASSERT_EQ(w.fullName_,   "abcdefg.^|./");

  // all bad chars
  w.setUserIDAndNames(0, "abcdefg.~!@#$%&*()+={}[]\\<>?,");
  ASSERT_EQ(w.workerName_, "__default__");
  ASSERT_EQ(w.fullName_,   "abcdefg.__default__");
}

TEST(Stratum, StratumJob) {
  StratumJob sjob;
  string poolCoinbaseInfo = "/BTC.COM/";
  uint32_t blockVersion = 0;
  bool res;

  SelectParams(CBaseChainParams::MAIN);
  CBitcoinAddress poolPayoutAddrMainnet("1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa");
  ASSERT_EQ(poolPayoutAddrMainnet.IsValid(), true);

  {
    string gbt;
    gbt += "{\"result\":{";
    gbt += "  \"capabilities\": [";
    gbt += "    \"proposal\"";
    gbt += "  ],";
    gbt += "  \"version\": 536870912,";
    gbt += "  \"previousblockhash\": \"000000004f2ea239532b2e77bb46c03b86643caac3fe92959a31fd2d03979c34\",";
    gbt += "  \"transactions\": [";
    gbt += "    {";
    gbt += "      \"data\": \"01000000010291939c5ae8191c2e7d4ce8eba7d6616a66482e3200037cb8b8c2d0af45b445000000006a47304402204df709d9e149804e358de4b082e41d8bb21b3c9d347241b728b1362aafcb153602200d06d9b6f2eca899f43dcd62ec2efb2d9ce2e10adf02738bb908420d7db93ede012103cae98ab925e20dd6ae1f76e767e9e99bc47b3844095c68600af9c775104fb36cffffffff0290f1770b000000001976a91400dc5fd62f6ee48eb8ecda749eaec6824a780fdd88aca08601000000000017a914eb65573e5dd52d3d950396ccbe1a47daf8f400338700000000\",";
    gbt += "      \"hash\": \"bd36bd4fff574b573152e7d4f64adf2bb1c9ab0080a12f8544c351f65aca79ff\",";
    gbt += "      \"depends\": [";
    gbt += "      ],";
    gbt += "      \"fee\": 10000,";
    gbt += "      \"sigops\": 1";
    gbt += "    }";
    gbt += "  ],";
    gbt += "  \"coinbaseaux\": {";
    gbt += "    \"flags\": \"\"";
    gbt += "  },";
    gbt += "  \"coinbasevalue\": 312659655,";
    gbt += "  \"longpollid\": \"000000004f2ea239532b2e77bb46c03b86643caac3fe92959a31fd2d03979c341911\",";
    gbt += "  \"target\": \"000000000000018ae20000000000000000000000000000000000000000000000\",";
    gbt += "  \"mintime\": 1469001544,";
    gbt += "  \"mutable\": [";
    gbt += "    \"time\",";
    gbt += "    \"transactions\",";
    gbt += "    \"prevblock\"";
    gbt += "  ],";
    gbt += "  \"noncerange\": \"00000000ffffffff\",";
    gbt += "  \"sigoplimit\": 20000,";
    gbt += "  \"sizelimit\": 1000000,";
    gbt += "  \"curtime\": 1469006933,";
    gbt += "  \"bits\": \"1a018ae2\",";
    gbt += "  \"height\": 898487";
    gbt += "}}";

    blockVersion = 0;
    SelectParams(CBaseChainParams::TESTNET);
    CBitcoinAddress poolPayoutAddrTestnet("myxopLJB19oFtNBdrAxD5Z34Aw6P8o9P8U");
    ASSERT_EQ(poolPayoutAddrTestnet.IsValid(), true);
    res = sjob.initFromGbt(gbt.c_str(), poolCoinbaseInfo, poolPayoutAddrTestnet, blockVersion, "");
    ASSERT_EQ(res, true);

    const string jsonStr = sjob.serializeToJson();
    StratumJob sjob2;
    res = sjob2.unserializeFromJson(jsonStr.c_str(), jsonStr.length());
    ASSERT_EQ(res, true);

    ASSERT_EQ(sjob2.prevHash_, uint256S("000000004f2ea239532b2e77bb46c03b86643caac3fe92959a31fd2d03979c34"));
    ASSERT_EQ(sjob2.prevHashBeStr_, "03979c349a31fd2dc3fe929586643caabb46c03b532b2e774f2ea23900000000");
    ASSERT_EQ(sjob2.height_, 898487);
    // 46 bytes, 5 bytes (timestamp), 9 bytes (poolCoinbaseInfo)
    // 01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff1e03b7b50d 0402363d58 2f4254432e434f4d2f
    ASSERT_EQ(sjob2.coinbase1_.substr(0, 92),
              "01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff1e03b7b50d");
    ASSERT_EQ(sjob2.coinbase1_.substr(102, 18), "2f4254432e434f4d2f");
    // 0402363d58 -> 0x583d3602 = 1480406530 = 2016-11-29 16:02:10
    uint32_t ts = (uint32_t)strtoull(sjob2.coinbase1_.substr(94, 8).c_str(), nullptr, 16);
    ts = HToBe(ts);
    ASSERT_EQ(ts == time(nullptr) || ts + 1 == time(nullptr), true);

    ASSERT_EQ(sjob2.coinbase2_,
              "ffffffff"  // sequence
              "01"        // 1 output
              // c7cea21200000000 -> 0000000012a2cec7 -> 312659655
              "c7cea21200000000"
              // 0x19 -> 25 bytes
              "1976a914ca560088c0fb5e6f028faa11085e643e343a8f5c88ac"
              // lock_time
              "00000000");
    ASSERT_EQ(sjob2.merkleBranch_.size(), 1);
    ASSERT_EQ(sjob2.merkleBranch_[0], uint256S("bd36bd4fff574b573152e7d4f64adf2bb1c9ab0080a12f8544c351f65aca79ff"));
    ASSERT_EQ(sjob2.nVersion_, 536870912);
    ASSERT_EQ(sjob2.nBits_,    436308706);
    ASSERT_EQ(sjob2.nTime_,    1469006933);
    ASSERT_EQ(sjob2.minTime_,  1469001544);
    ASSERT_EQ(sjob2.coinbaseValue_, 312659655);
    ASSERT_GE(time(nullptr), jobId2Time(sjob2.jobId_));
  }
}


TEST(Stratum, StratumJobWithWitnessCommitment) {
  StratumJob sjob;
  string poolCoinbaseInfo = "/BTC.COM/";
  uint32_t blockVersion = 0;
  bool res;

  {
    string gbt;
    gbt += "{\"result\":";
    gbt += "{";
    gbt += "  \"capabilities\": [";
    gbt += "    \"proposal\"";
    gbt += "  ],";
    gbt += "  \"version\": 536870912,";
    gbt += "  \"rules\": [";
    gbt += "    \"csv\",";
    gbt += "    \"!segwit\"";
    gbt += "  ],";
    gbt += "  \"vbavailable\": {";
    gbt += "  },";
    gbt += "  \"vbrequired\": 0,";
    gbt += "  \"previousblockhash\": \"0000000000000047e5bda122407654b25d52e0f3eeb00c152f631f70e9803772\",";
    gbt += "  \"transactions\": [";
    gbt += "    {";
    gbt += "      \"data\": \"0100000002449f651247d5c09d3020c30616cb1807c268e2c2346d1de28442b89ef34c976d000000006a47304402203eae3868946a312ba712f9c9a259738fee6e3163b05d206e0f5b6c7980";
    gbt += "161756022017827f248432f7313769f120fb3b7a65137bf93496a1ae7d6a775879fbdfb8cd0121027d7b71dab3bb16582c97fc0ccedeacd8f75ebee62fa9c388290294ee3bc3e935feffffffcbc82a21497f8db";
    gbt += "8d57d054fefea52aba502a074ed984efc81ec2ef211194aa6010000006a47304402207f5462295e52fb4213f1e63802d8fe9ec020ac8b760535800564694ea87566a802205ee01096fc9268eac483136ce08250";
    gbt += "6ac951a7dbc9e4ae24dca07ca2a1fdf2f30121023b86e60ef66fe8ace403a0d77d27c80ba9ba5404ee796c47c03c73748e59d125feffffff0286c35b00000000001976a914ab29f668d284fd2d65cec5f098432";
    gbt += "c4ece01055488ac8093dc14000000001976a914ac19d3fd17710e6b9a331022fe92c693fdf6659588ac8dd70f00\",";
    gbt += "      \"txid\": \"c284853b65e7887c5fd9b635a932e2e0594d19849b22914a8e6fb180fea0954f\",";
    gbt += "      \"hash\": \"c284853b65e7887c5fd9b635a932e2e0594d19849b22914a8e6fb180fea0954f\",";
    gbt += "      \"depends\": [";
    gbt += "      ],";
    gbt += "      \"fee\": 37400,";
    gbt += "      \"sigops\": 8,";
    gbt += "      \"weight\": 1488";
    gbt += "    },";
    gbt += "    {";
    gbt += "      \"data\": \"0100000001043f5e73755b5c6919b4e361f4cae84c8805452de3df265a6e2d3d71cbcb385501000000da0047304402202b14552521cd689556d2e44d914caf2195da37b80de4f8cd0fad9adf";
    gbt += "7ef768ef022026fcddd992f447c39c48c3ce50c5960e2f086ebad455159ffc3e36a5624af2f501483045022100f2b893e495f41b22cd83df6908c2fa4f917fd7bce9f8da14e6ab362042e11f7d022075bc2451e";
    gbt += "1cf2ae2daec0f109a3aceb6558418863070f5e84c945262018503240147522102632178d046673c9729d828cfee388e121f497707f810c131e0d3fc0fe0bd66d62103a0951ec7d3a9da9de171617026442fcd30";
    gbt += "f34d66100fab539853b43f508787d452aeffffffff0240420f000000000017a9143e9a6b79be836762c8ef591cf16b76af1327ced58790dfdf8c0000000017a9148ce5408cfeaddb7ccb2545ded41ef47810945";
    gbt += "4848700000000\",";
    gbt += "      \"txid\": \"28b1a5c2f0bb667aea38e760b6d55163abc9be9f1f830d9969edfab902d17a0f\",";
    gbt += "      \"hash\": \"28b1a5c2f0bb667aea38e760b6d55163abc9be9f1f830d9969edfab902d17a0f\",";
    gbt += "      \"depends\": [";
    gbt += "      ],";
    gbt += "      \"fee\": 20000,";
    gbt += "      \"sigops\": 8,";
    gbt += "      \"weight\": 1332";
    gbt += "    },";
    gbt += "    {";
    gbt += "      \"data\": \"01000000013faf73481d6b96c2385b9a4300f8974b1b30c34be30000c7dcef11f68662de4501000000db00483045022100f9881f4c867b5545f6d7a730ae26f598107171d0f68b860bd973db";
    gbt += "b855e073a002207b511ead1f8be8a55c542ce5d7e91acfb697c7fa2acd2f322b47f177875bffc901483045022100a37aa9998b9867633ab6484ad08b299de738a86ae997133d827717e7ed73d953022011e3f99";
    gbt += "d1bd1856f6a7dc0bf611de6d1b2efb60c14fc5931ba09da01558757f60147522102632178d046673c9729d828cfee388e121f497707f810c131e0d3fc0fe0bd66d62103a0951ec7d3a9da9de171617026442fcd";
    gbt += "30f34d66100fab539853b43f508787d452aeffffffff0240420f000000000017a9148d57003ecbaa310a365f8422602cc507a702197e87806868a90000000017a9148ce5408cfeaddb7ccb2545ded41ef478109";
    gbt += "454848700000000\",";
    gbt += "      \"txid\": \"67878210e268d87b4e6587db8c6e367457cea04820f33f01d626adbe5619b3dd\",";
    gbt += "      \"hash\": \"67878210e268d87b4e6587db8c6e367457cea04820f33f01d626adbe5619b3dd\",";
    gbt += "      \"depends\": [";
    gbt += "      ],";
    gbt += "      \"fee\": 20000,";
    gbt += "      \"sigops\": 8,";
    gbt += "      \"weight\": 1336";
    gbt += "    },";
    gbt += "  ],";
    gbt += "  \"coinbaseaux\": {";
    gbt += "    \"flags\": \"\"";
    gbt += "  },";
    gbt += "  \"coinbasevalue\": 319367518,";
    gbt += "  \"longpollid\": \"0000000000000047e5bda122407654b25d52e0f3eeb00c152f631f70e9803772604597\",";
    gbt += "  \"target\": \"0000000000001714480000000000000000000000000000000000000000000000\",";
    gbt += "  \"mintime\": 1480831053,";
    gbt += "  \"mutable\": [";
    gbt += "    \"time\",";
    gbt += "    \"transactions\",";
    gbt += "    \"prevblock\"";
    gbt += "  ],";
    gbt += "  \"noncerange\": \"00000000ffffffff\",";
    gbt += "  \"sigoplimit\": 80000,";
    gbt += "  \"sizelimit\": 4000000,";
    gbt += "  \"weightlimit\": 4000000,";
    gbt += "  \"curtime\": 1480834892,";
    gbt += "  \"bits\": \"1a171448\",";
    gbt += "  \"height\": 1038222,";
    gbt += "  \"default_witness_commitment\": \"6a24aa21a9ed842a6d6672504c2b7abb796fdd7cfbd7262977b71b945452e17fbac69ed22bf8\"";
    gbt += "}}";

    blockVersion = 0;
    SelectParams(CBaseChainParams::TESTNET);
    CBitcoinAddress poolPayoutAddrTestnet("myxopLJB19oFtNBdrAxD5Z34Aw6P8o9P8U");
    ASSERT_EQ(poolPayoutAddrTestnet.IsValid(), true);
    res = sjob.initFromGbt(gbt.c_str(), poolCoinbaseInfo, poolPayoutAddrTestnet, blockVersion, "");
    ASSERT_EQ(res, true);

    const string jsonStr = sjob.serializeToJson();
    StratumJob sjob2;
    res = sjob2.unserializeFromJson(jsonStr.c_str(), jsonStr.length());
    ASSERT_EQ(res, true);

    ASSERT_EQ(sjob2.prevHash_, uint256S("0000000000000047e5bda122407654b25d52e0f3eeb00c152f631f70e9803772"));
    ASSERT_EQ(sjob2.prevHashBeStr_, "e98037722f631f70eeb00c155d52e0f3407654b2e5bda1220000004700000000");
    ASSERT_EQ(sjob2.height_, 1038222);

    ASSERT_EQ(sjob2.coinbase2_,
              "ffffffff"  // sequence
              "02"        // 2 outputs
              // 5e29091300000000 -> 000000001309295e -> 319367518
              "5e29091300000000"
              // 0x19 -> 25 bytes
              "1976a914ca560088c0fb5e6f028faa11085e643e343a8f5c88ac"
              //
              "0000000000000000"
              // 0x26 -> 38 bytes
              "266a24aa21a9ed842a6d6672504c2b7abb796fdd7cfbd7262977b71b945452e17fbac69ed22bf8"
              // lock_time
              "00000000");

    ASSERT_EQ(sjob2.nVersion_, 536870912);
    ASSERT_EQ(sjob2.nBits_,    0x1a171448u);
    ASSERT_EQ(sjob2.nTime_,    1480834892);
    ASSERT_EQ(sjob2.minTime_,  1480831053);
    ASSERT_EQ(sjob2.coinbaseValue_, 319367518);
    ASSERT_GE(time(nullptr), jobId2Time(sjob2.jobId_));
  }
}
