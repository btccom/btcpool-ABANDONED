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

#include "bitcoin/BitcoinUtils.h"
#include "bitcoin/StratumBitcoin.h"
#include "rsk/RskWork.h"
#include "vcash/VcashWork.h"
#include "beam/StratumBeam.h"

#include <chainparams.h>
#include <hash.h>
#include <script/script.h>
#include <uint256.h>
#include <util.h>

#ifdef INCLUDE_BTC_KEY_IO_H
#include <key_io.h> //  IsValidDestinationString for bch is not in this file.
#endif

#include <stdint.h>

TEST(Stratum, jobId2Time) {
  uint64_t jobId;

  // jobId:   timestamp    +    gbtHash
  //          ----------      ----------
  //           uint32_t        uint32_t
  //  const string jobIdStr = Strings::Format("%08x%s", (uint32_t)time(nullptr),
  //                                          gbtHash.ToString().substr(0,
  //                                          8));
  jobId = (1469002809ull << 32) | 0x00000000FFFFFFFFull;
  ASSERT_EQ(jobId2Time(jobId), 1469002809u);

  jobId = (1469002809ull << 32) | 0x0000000000000000ull;
  ASSERT_EQ(jobId2Time(jobId), 1469002809u);
}

TEST(Stratum, Share) {
  ShareBitcoin s;

  ASSERT_EQ(s.isValid(), false);
  ASSERT_EQ(s.score(), 0);
  ASSERT_EQ(
      s.toString(),
      "share(jobId: 0, ip: 0.0.0.0, userId: 0, workerId: 0, time: 0/1970-01-01 "
      "00:00:00, height: 0, blkBits: 00000000/inf, shareDiff: 0, nonce: "
      "00000000, sessionId: 00000000, versionMask: 00000000, status: 0/Share "
      "rejected)");
  IpAddress ip;
  ip.fromIpv4Int(htonl(167772161));

  s.set_ip(ip.toString()); // 167772161 : 10.0.0.1
  ASSERT_EQ(
      s.toString(),
      "share(jobId: 0, ip: 10.0.0.1, userId: 0, workerId: 0, time: "
      "0/1970-01-01 00:00:00, height: 0, blkBits: 00000000/inf, shareDiff: 0, "
      "nonce: 00000000, sessionId: 00000000, versionMask: 00000000, status: "
      "0/Share rejected)");
}

TEST(Stratum, Share2) {
  ShareBitcoin s;

  s.set_blkbits(BitcoinDifficulty::GetDiffOneBits());
  s.set_sharediff(1ll);
  ASSERT_EQ(s.score(), 1ll);

  s.set_blkbits(0x18050edcu);
  s.set_sharediff(UINT32_MAX);
#ifdef CHAIN_TYPE_LTC
  ASSERT_EQ(score2Str(s.score()), "0.000000301487541987111");
#elif defined(CHAIN_TYPE_ZEC)
  ASSERT_EQ(score2Str(s.score()), "0.0000000376854395858095");
#else
  ASSERT_EQ(score2Str(s.score()), "0.0197582875516673");
#endif
}

TEST(Stratum, StratumWorker) {
  StratumWorker w(3);
  uint64_t u;
  int64_t workerId;

  // test userid with multi chains
  ASSERT_EQ(w.userIds_.size(), 3u);

  w.setChainIdAndUserId(0, INT32_MAX);
  ASSERT_EQ(w.userId(), INT32_MAX);

  w.setChainIdAndUserId(1, 0);
  ASSERT_EQ(w.userId(), 0);

  w.setChainIdAndUserId(2, 123);
  ASSERT_EQ(w.userId(), 123);

  w.setChainIdAndUserId(1, 456);
  ASSERT_EQ(w.userId(), 456);

  ASSERT_EQ(w.userId(0), INT32_MAX);
  ASSERT_EQ(w.userId(1), 456);
  ASSERT_EQ(w.userId(2), 123);

  // test username split
  ASSERT_EQ(StratumWorker::getUserName("abcd"), "abcd");
  ASSERT_EQ(
      StratumWorker::getUserName("abcdabcdabcdabcdabcdabcdabcd"),
      "abcdabcdabcdabcdabcdabcdabcd");
  ASSERT_EQ(StratumWorker::getUserName("abcd."), "abcd");
  ASSERT_EQ(StratumWorker::getUserName("abcd.123"), "abcd");
  ASSERT_EQ(StratumWorker::getUserName("abcd.123.456"), "abcd");

  //
  // echo -n '123' |openssl dgst -sha256 -binary |openssl dgst -sha256
  //
  w.setNames("abcd.123", [](string &) {});
  ASSERT_EQ(w.fullName_, "abcd.123");
  ASSERT_EQ(w.userName_, "abcd");
  ASSERT_EQ(w.workerName_, "123");
  // '123' dsha256 :
  // 5a77d1e9612d350b3734f6282259b7ff0a3f87d62cfef5f35e91a5604c0490a3
  //       uint256 :
  //       a390044c60a5915ef3f5fe2cd6873f0affb7592228f634370b352d61e9d1775a
  u = strtoull("a390044c60a5915e", nullptr, 16);
  memcpy((uint8_t *)&workerId, (uint8_t *)&u, 8);
  ASSERT_EQ(w.workerHashId_, workerId);

  w.setNames("abcdefg", [](string &) {});
  ASSERT_EQ(w.fullName_, "abcdefg.__default__");
  ASSERT_EQ(w.userName_, "abcdefg");
  ASSERT_EQ(w.workerName_, "__default__");
  // '__default__' dsha256 :
  // e00f302bc411fde77d954283be6904911742f2ac76c8e79abef5dff4e6a19770
  //               uint256 : 7097a1e6f4dff5be
  u = strtoull("7097a1e6f4dff5be", nullptr, 16);
  memcpy((uint8_t *)&workerId, (uint8_t *)&u, 8);
  ASSERT_EQ(w.workerHashId_, workerId);

  // check allow chars
  w.setNames("abcdefg.azAZ09-._:|^/", [](string &) {});
  ASSERT_EQ(w.workerName_, "azAZ09-._:|^/");
  ASSERT_EQ(w.fullName_, "abcdefg.azAZ09-._:|^/");

  // some of them are bad chars
  w.setNames("abcdefg.~!@#$%^&*()+={}|[]\\<>?,./", [](string &) {});
  ASSERT_EQ(w.workerName_, "^|./");
  ASSERT_EQ(w.fullName_, "abcdefg.^|./");

  // all bad chars
  w.setNames("abcdefg.~!@#$%&*()+={}[]\\<>?,", [](string &) {});
  ASSERT_EQ(w.workerName_, "__default__");
  ASSERT_EQ(w.fullName_, "abcdefg.__default__");

  w.setNames(
      "ckt1qyq98fl86kcmqkdv3cf6u8pgf6f99j93q37sq8sr05.0x1",
      [](string &) {},
      false,
      "",
      true);
  ASSERT_EQ(w.fullName_, "ckt1qyq98fl86kcmqkdv3cf6u8pgf6f99j93q37sq8sr05.0x1");
  ASSERT_EQ(w.userName_, "ckt1qyq98fl86kcmqkdv3cf6u8pgf6f99j93q37sq8sr05");
  ASSERT_EQ(w.workerName_, "0x1");

  w.setNames(
      "ckt1qyqdmeuqrsrnm7e5vnrmruzmsp4m9wacf6vsmcwugu",
      [](string &) {},
      false,
      "",
      true);
  ASSERT_EQ(
      w.fullName_,
      "ckt1qyqdmeuqrsrnm7e5vnrmruzmsp4m9wacf6vsmcwugu.__default__");
  ASSERT_EQ(w.userName_, "ckt1qyqdmeuqrsrnm7e5vnrmruzmsp4m9wacf6vsmcwugu");
  ASSERT_EQ(w.workerName_, "__default__");
}

#ifdef CHAIN_TYPE_LTC
TEST(JobMaker, LitecoinAddress) {
  // main net
  SelectParams(CBaseChainParams::MAIN);
  ASSERT_EQ(
      BitcoinUtils::IsValidDestinationString(
          "LNECQRGAYTEdfPqmPehVjB71HSccAJLRPK"),
      true);
  ASSERT_EQ(
      BitcoinUtils::IsValidDestinationString(
          "3Lms3onWvriv6AdKanUYDytgWyuFcFM7nU"),
      true);

  // test net
  SelectParams(CBaseChainParams::TESTNET);
  ASSERT_EQ(
      BitcoinUtils::IsValidDestinationString(
          "mqpqVxqAkaW8r6KewK6wmTrSjDuiDjhvt5"),
      true);
  ASSERT_EQ(
      BitcoinUtils::IsValidDestinationString(
          "mjPkDNakVA4w4hJZ6WF7p8yKUV2merhyCM"),
      true);
}
#elif defined(CHAIN_TYPE_ZEC)
TEST(JobMaker, ZCashAddress) {
  // main net
  SelectParams(CBaseChainParams::MAIN);
  ASSERT_EQ(
      BitcoinUtils::IsValidDestinationString(
          "t1eJBAt6eVpqPUYKxRB51dqAGsfLSJcU4rS"),
      true);

  // test net
  SelectParams(CBaseChainParams::TESTNET);
  ASSERT_EQ(
      BitcoinUtils::IsValidDestinationString(
          "tmSjdKFY4N23N9as5pd4APLtrTpjvQvXF8R"),
      true);
}
#else
TEST(JobMaker, BitcoinAddress) {
  // main net
  SelectParams(CBaseChainParams::MAIN);
  ASSERT_EQ(
      BitcoinUtils::IsValidDestinationString(
          "1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa"),
      true);
  ASSERT_EQ(
      BitcoinUtils::IsValidDestinationString(
          "1A1zP1eP5QGefi2DMPPfTL5SLmv7DivfNa"),
      false);

#ifdef CHAIN_TYPE_BTC
  ASSERT_EQ(
      BitcoinUtils::IsValidDestinationString(
          "bc1qw508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t4"),
      true);
  ASSERT_EQ(
      BitcoinUtils::IsValidDestinationString(
          "bc1qw508c6qejxtdg4y5r3zarvary4c5xw7kv8f3t4"),
      false);
  ASSERT_EQ(
      BitcoinUtils::IsValidDestinationString(
          "bc1qrp33g0q5c5txsp9arysrx4k6zdkfs4nce4xj0gdcccefvpysxf3qccfmv3"),
      true);
  ASSERT_EQ(
      BitcoinUtils::IsValidDestinationString(
          "bc1qrp33g0q5c5txsp8arysrx4k6zdkfs4nde4xj0gdcccefvpysxf3qccfmv3"),
      false);
#endif

#ifdef CHAIN_TYPE_BCH
  ASSERT_EQ(
      BitcoinUtils::IsValidDestinationString(
          "bitcoincash:qp3wjpa3tjlj042z2wv7hahsldgwhwy0rq9sywjpyy"),
      true);
  ASSERT_EQ(
      BitcoinUtils::IsValidDestinationString(
          "bitcoincash:qp3wjpa3tjlj142z2wv7hahsldgwhwy0rq9sywjpyy"),
      false);
#endif

  // test net
  SelectParams(CBaseChainParams::TESTNET);
  ASSERT_EQ(
      BitcoinUtils::IsValidDestinationString(
          "myxopLJB19oFtNBdrAxD5Z34Aw6P8o9P8U"),
      true);
  ASSERT_EQ(
      BitcoinUtils::IsValidDestinationString(
          "myxopLJB19oFtNBdrADD5Z34Aw6P8o9P8U"),
      false);

#ifdef CHAIN_TYPE_BTC
  ASSERT_EQ(
      BitcoinUtils::IsValidDestinationString(
          "tb1qw508d6qejxtdg4y5r3zarvary0c5xw7kxpjzsx"),
      true);
  ASSERT_EQ(
      BitcoinUtils::IsValidDestinationString(
          "tb1qw508d6qejxtdg6y5r3zarvary0c5xw7kxpjzsx"),
      false);
  ASSERT_EQ(
      BitcoinUtils::IsValidDestinationString(
          "tb1qrp33g0q5c5txsp9arysrx4k6zdkfs4nce4xj0gdcccefvpysxf3q0sl5k7"),
      true);
  ASSERT_EQ(
      BitcoinUtils::IsValidDestinationString(
          "tb1qrp33g0q5c5txsp9arysrx4k6zdkgs4nce4xj0gdcccefvpysxf3q0sl5k7"),
      false);
#endif

#ifdef CHAIN_TYPE_BCH
  ASSERT_EQ(
      BitcoinUtils::IsValidDestinationString(
          "bchtest:qr99vqygcra4umcz374pzzz7vslrgw50ts58trd220"),
      true);
  ASSERT_EQ(
      BitcoinUtils::IsValidDestinationString(
          "bchtest:qr99vqygcra5umcz374pzzz7vslrgw50ts58trd220"),
      false);
#endif
}
#endif

#ifndef CHAIN_TYPE_ZEC
TEST(Stratum, StratumJobBitcoin) {
  StratumJobBitcoin sjob;
  string poolCoinbaseInfo = "/BTC.COM/";
  uint32_t blockVersion = 0;
  bool res;

  {
    string gbt = R"EOF(
        {
            "result": {
                "capabilities": ["proposal"],
                "version": 536870912,
                "previousblockhash": "000000004f2ea239532b2e77bb46c03b86643caac3fe92959a31fd2d03979c34",
                "transactions": [{
                    "data": "01000000010291939c5ae8191c2e7d4ce8eba7d6616a66482e3200037cb8b8c2d0af45b445000000006a47304402204df709d9e149804e358de4b082e41d8bb21b3c9d347241b728b1362aafcb153602200d06d9b6f2eca899f43dcd62ec2efb2d9ce2e10adf02738bb908420d7db93ede012103cae98ab925e20dd6ae1f76e767e9e99bc47b3844095c68600af9c775104fb36cffffffff0290f1770b000000001976a91400dc5fd62f6ee48eb8ecda749eaec6824a780fdd88aca08601000000000017a914eb65573e5dd52d3d950396ccbe1a47daf8f400338700000000",
                    "hash": "bd36bd4fff574b573152e7d4f64adf2bb1c9ab0080a12f8544c351f65aca79ff",
                    "depends": [],
                    "fee": 10000,
                    "sigops": 1
                }],
                "coinbaseaux": {
                    "flags": ""
                },
                "coinbasevalue": 312659655,
                "longpollid": "000000004f2ea239532b2e77bb46c03b86643caac3fe92959a31fd2d03979c341911",
                "target": "000000000000018ae20000000000000000000000000000000000000000000000",
                "mintime": 1469001544,
                "mutable": ["time", "transactions", "prevblock"],
                "noncerange": "00000000ffffffff",
                "sigoplimit": 20000,
                "sizelimit": 1000000,
                "curtime": 1469006933,
                "bits": "1a018ae2",
                "height": 898487
            }
        }
    )EOF";

    blockVersion = 0;
    SelectParams(CBaseChainParams::TESTNET);
    ASSERT_EQ(
        BitcoinUtils::IsValidDestinationString(
            "myxopLJB19oFtNBdrAxD5Z34Aw6P8o9P8U"),
        true);

    CTxDestination poolPayoutAddrTestnet =
        BitcoinUtils::DecodeDestination("myxopLJB19oFtNBdrAxD5Z34Aw6P8o9P8U");
    vector<SubPoolInfo> subPool;
    res = sjob.initFromGbt(
        gbt.c_str(),
        poolCoinbaseInfo,
        poolPayoutAddrTestnet,
        subPool,
        blockVersion,
        "",
        RskWork(),
        VcashWork(),
        false,
        false);
    ASSERT_EQ(res, true);

    const string jsonStr = sjob.serializeToJson();
    StratumJobBitcoin sjob2;
    res = sjob2.unserializeFromJson(jsonStr.c_str(), jsonStr.length());
    ASSERT_EQ(res, true);

    ASSERT_EQ(
        sjob2.prevHash_,
        uint256S("000000004f2ea239532b2e77bb46c03b86643caac3fe92959a31fd2d03979"
                 "c34"));
    ASSERT_EQ(
        sjob2.prevHashBeStr_,
        "03979c349a31fd2dc3fe929586643caabb46c03b532b2e774f2ea23900000000");
    ASSERT_EQ(sjob2.height_, 898487);
    // 46 bytes, 5 bytes (timestamp), 9 bytes (poolCoinbaseInfo)
    // 02000000010000000000000000000000000000000000000000000000000000000000000000ffffffff1e03b7b50d
    // 0402363d58 2f4254432e434f4d2f
    ASSERT_EQ(
        sjob2.coinbase1_.substr(0, 92),
        "0200000001000000000000000000000000000000000000000000000000000000000000"
        "0000ffffffff1e03b7b50d");
    ASSERT_EQ(sjob2.coinbase1_.substr(102, 18), "2f4254432e434f4d2f");
    // 0402363d58 -> 0x583d3602 = 1480406530 = 2016-11-29 16:02:10
    uint32_t ts =
        (uint32_t)strtoull(sjob2.coinbase1_.substr(94, 8).c_str(), nullptr, 16);
    ts = HToBe(ts);
    ASSERT_EQ(ts == time(nullptr) || ts + 1 == time(nullptr), true);

    ASSERT_EQ(
        sjob2.coinbase2_,
        "ffffffff" // sequence
        "01" // 1 output
        // c7cea21200000000 -> 0000000012a2cec7 -> 312659655
        "c7cea21200000000"
        // 0x19 -> 25 bytes
        "1976a914ca560088c0fb5e6f028faa11085e643e343a8f5c88ac"
        // lock_time
        "00000000");
    ASSERT_EQ(sjob2.merkleBranch_.size(), 1U);
    ASSERT_EQ(
        sjob2.merkleBranch_[0],
        uint256S("bd36bd4fff574b573152e7d4f64adf2bb1c9ab0080a12f8544c351f65aca7"
                 "9ff"));
    ASSERT_EQ(sjob2.nVersion_, 536870912);
    ASSERT_EQ(sjob2.nBits_, 436308706U);
    ASSERT_EQ(sjob2.nTime_, 1469006933U);
    ASSERT_EQ(sjob2.minTime_, 1469001544U);
    ASSERT_EQ(sjob2.coinbaseValue_, 312659655);
    ASSERT_GE(time(nullptr), jobId2Time(sjob2.jobId_));
  }
}
#endif

#ifdef CHAIN_TYPE_BTC
TEST(Stratum, StratumJobWithWitnessCommitment) {
  StratumJobBitcoin sjob;
  string poolCoinbaseInfo = "/BTC.COM/";
  uint32_t blockVersion = 0;
  bool res;

  {
    string gbt = R"EOF(
        {
            "result": {
                "capabilities": ["proposal"],
                "version": 536870912,
                "rules": ["csv", "!segwit"],
                "vbavailable": {},
                "vbrequired": 0,
                "previousblockhash": "0000000000000047e5bda122407654b25d52e0f3eeb00c152f631f70e9803772",
                "transactions": [{
                    "data": "0100000002449f651247d5c09d3020c30616cb1807c268e2c2346d1de28442b89ef34c976d000000006a47304402203eae3868946a312ba712f9c9a259738fee6e3163b05d206e0f5b6c7980161756022017827f248432f7313769f120fb3b7a65137bf93496a1ae7d6a775879fbdfb8cd0121027d7b71dab3bb16582c97fc0ccedeacd8f75ebee62fa9c388290294ee3bc3e935feffffffcbc82a21497f8db8d57d054fefea52aba502a074ed984efc81ec2ef211194aa6010000006a47304402207f5462295e52fb4213f1e63802d8fe9ec020ac8b760535800564694ea87566a802205ee01096fc9268eac483136ce082506ac951a7dbc9e4ae24dca07ca2a1fdf2f30121023b86e60ef66fe8ace403a0d77d27c80ba9ba5404ee796c47c03c73748e59d125feffffff0286c35b00000000001976a914ab29f668d284fd2d65cec5f098432c4ece01055488ac8093dc14000000001976a914ac19d3fd17710e6b9a331022fe92c693fdf6659588ac8dd70f00",
                    "txid": "c284853b65e7887c5fd9b635a932e2e0594d19849b22914a8e6fb180fea0954f",
                    "hash": "c284853b65e7887c5fd9b635a932e2e0594d19849b22914a8e6fb180fea0954f",
                    "depends": [],
                    "fee": 37400,
                    "sigops": 8,
                    "weight": 1488
                }, {
                    "data": "0100000001043f5e73755b5c6919b4e361f4cae84c8805452de3df265a6e2d3d71cbcb385501000000da0047304402202b14552521cd689556d2e44d914caf2195da37b80de4f8cd0fad9adf7ef768ef022026fcddd992f447c39c48c3ce50c5960e2f086ebad455159ffc3e36a5624af2f501483045022100f2b893e495f41b22cd83df6908c2fa4f917fd7bce9f8da14e6ab362042e11f7d022075bc2451e1cf2ae2daec0f109a3aceb6558418863070f5e84c945262018503240147522102632178d046673c9729d828cfee388e121f497707f810c131e0d3fc0fe0bd66d62103a0951ec7d3a9da9de171617026442fcd30f34d66100fab539853b43f508787d452aeffffffff0240420f000000000017a9143e9a6b79be836762c8ef591cf16b76af1327ced58790dfdf8c0000000017a9148ce5408cfeaddb7ccb2545ded41ef478109454848700000000",
                    "txid": "28b1a5c2f0bb667aea38e760b6d55163abc9be9f1f830d9969edfab902d17a0f",
                    "hash": "28b1a5c2f0bb667aea38e760b6d55163abc9be9f1f830d9969edfab902d17a0f",
                    "depends": [],
                    "fee": 20000,
                    "sigops": 8,
                    "weight": 1332
                }, {
                    "data": "01000000013faf73481d6b96c2385b9a4300f8974b1b30c34be30000c7dcef11f68662de4501000000db00483045022100f9881f4c867b5545f6d7a730ae26f598107171d0f68b860bd973dbb855e073a002207b511ead1f8be8a55c542ce5d7e91acfb697c7fa2acd2f322b47f177875bffc901483045022100a37aa9998b9867633ab6484ad08b299de738a86ae997133d827717e7ed73d953022011e3f99d1bd1856f6a7dc0bf611de6d1b2efb60c14fc5931ba09da01558757f60147522102632178d046673c9729d828cfee388e121f497707f810c131e0d3fc0fe0bd66d62103a0951ec7d3a9da9de171617026442fcd30f34d66100fab539853b43f508787d452aeffffffff0240420f000000000017a9148d57003ecbaa310a365f8422602cc507a702197e87806868a90000000017a9148ce5408cfeaddb7ccb2545ded41ef478109454848700000000",
                    "txid": "67878210e268d87b4e6587db8c6e367457cea04820f33f01d626adbe5619b3dd",
                    "hash": "67878210e268d87b4e6587db8c6e367457cea04820f33f01d626adbe5619b3dd",
                    "depends": [],
                    "fee": 20000,
                    "sigops": 8,
                    "weight": 1336
                }],
                "coinbaseaux": {
                    "flags": ""
                },
                "coinbasevalue": 319367518,
                "longpollid": "0000000000000047e5bda122407654b25d52e0f3eeb00c152f631f70e9803772604597",
                "target": "0000000000001714480000000000000000000000000000000000000000000000",
                "mintime": 1480831053,
                "mutable": ["time", "transactions", "prevblock"],
                "noncerange": "00000000ffffffff",
                "sigoplimit": 80000,
                "sizelimit": 4000000,
                "weightlimit": 4000000,
                "curtime": 1480834892,
                "bits": "1a171448",
                "height": 1038222,
                "default_witness_commitment": "6a24aa21a9ed842a6d6672504c2b7abb796fdd7cfbd7262977b71b945452e17fbac69ed22bf8"
            }
        }
    )EOF";

    blockVersion = 0;
    SelectParams(CBaseChainParams::TESTNET);
    ASSERT_EQ(
        BitcoinUtils::IsValidDestinationString(
            "myxopLJB19oFtNBdrAxD5Z34Aw6P8o9P8U"),
        true);

    CTxDestination poolPayoutAddrTestnet =
        BitcoinUtils::DecodeDestination("myxopLJB19oFtNBdrAxD5Z34Aw6P8o9P8U");
    vector<SubPoolInfo> subPool;
    res = sjob.initFromGbt(
        gbt.c_str(),
        poolCoinbaseInfo,
        poolPayoutAddrTestnet,
        subPool,
        blockVersion,
        "",
        RskWork(),
        VcashWork(),
        false,
        false);
    ASSERT_EQ(res, true);

    const string jsonStr = sjob.serializeToJson();
    StratumJobBitcoin sjob2;
    res = sjob2.unserializeFromJson(jsonStr.c_str(), jsonStr.length());
    ASSERT_EQ(res, true);

    ASSERT_EQ(
        sjob2.prevHash_,
        uint256S("0000000000000047e5bda122407654b25d52e0f3eeb00c152f631f70e9803"
                 "772"));
    ASSERT_EQ(
        sjob2.prevHashBeStr_,
        "e98037722f631f70eeb00c155d52e0f3407654b2e5bda1220000004700000000");
    ASSERT_EQ(sjob2.height_, 1038222);

    ASSERT_EQ(
        sjob2.coinbase2_,
        "ffffffff" // sequence
        "02" // 2 outputs
        // 5e29091300000000 -> 000000001309295e -> 319367518
        "5e29091300000000"
        // 0x19 -> 25 bytes
        "1976a914ca560088c0fb5e6f028faa11085e643e343a8f5c88ac"
        //
        "0000000000000000"
        // 0x26 -> 38 bytes
        "266a24aa21a9ed842a6d6672504c2b7abb796fdd7cfbd7262977b71b945452e17fbac6"
        "9ed22bf8"
        // lock_time
        "00000000");

    ASSERT_EQ(sjob2.nVersion_, 536870912);
    ASSERT_EQ(sjob2.nBits_, 0x1a171448U);
    ASSERT_EQ(sjob2.nTime_, 1480834892U);
    ASSERT_EQ(sjob2.minTime_, 1480831053U);
    ASSERT_EQ(sjob2.coinbaseValue_, 319367518);
    ASSERT_GE(time(nullptr), jobId2Time(sjob2.jobId_));
  }
}
#endif

#ifdef CHAIN_TYPE_BTC
TEST(Stratum, StratumJobWithSegwitPayoutAddr) {
  StratumJobBitcoin sjob;
  string poolCoinbaseInfo = "/BTC.COM/";
  uint32_t blockVersion = 0;
  bool res;

  {
    string gbt = R"EOF(
        {
            "result": {
                "capabilities": ["proposal"],
                "version": 536870912,
                "rules": ["csv", "!segwit"],
                "vbavailable": {},
                "vbrequired": 0,
                "previousblockhash": "0000000000000047e5bda122407654b25d52e0f3eeb00c152f631f70e9803772",
                "transactions": [{
                    "data": "0100000002449f651247d5c09d3020c30616cb1807c268e2c2346d1de28442b89ef34c976d000000006a47304402203eae3868946a312ba712f9c9a259738fee6e3163b05d206e0f5b6c7980161756022017827f248432f7313769f120fb3b7a65137bf93496a1ae7d6a775879fbdfb8cd0121027d7b71dab3bb16582c97fc0ccedeacd8f75ebee62fa9c388290294ee3bc3e935feffffffcbc82a21497f8db8d57d054fefea52aba502a074ed984efc81ec2ef211194aa6010000006a47304402207f5462295e52fb4213f1e63802d8fe9ec020ac8b760535800564694ea87566a802205ee01096fc9268eac483136ce082506ac951a7dbc9e4ae24dca07ca2a1fdf2f30121023b86e60ef66fe8ace403a0d77d27c80ba9ba5404ee796c47c03c73748e59d125feffffff0286c35b00000000001976a914ab29f668d284fd2d65cec5f098432c4ece01055488ac8093dc14000000001976a914ac19d3fd17710e6b9a331022fe92c693fdf6659588ac8dd70f00",
                    "txid": "c284853b65e7887c5fd9b635a932e2e0594d19849b22914a8e6fb180fea0954f",
                    "hash": "c284853b65e7887c5fd9b635a932e2e0594d19849b22914a8e6fb180fea0954f",
                    "depends": [],
                    "fee": 37400,
                    "sigops": 8,
                    "weight": 1488
                }, {
                    "data": "0100000001043f5e73755b5c6919b4e361f4cae84c8805452de3df265a6e2d3d71cbcb385501000000da0047304402202b14552521cd689556d2e44d914caf2195da37b80de4f8cd0fad9adf7ef768ef022026fcddd992f447c39c48c3ce50c5960e2f086ebad455159ffc3e36a5624af2f501483045022100f2b893e495f41b22cd83df6908c2fa4f917fd7bce9f8da14e6ab362042e11f7d022075bc2451e1cf2ae2daec0f109a3aceb6558418863070f5e84c945262018503240147522102632178d046673c9729d828cfee388e121f497707f810c131e0d3fc0fe0bd66d62103a0951ec7d3a9da9de171617026442fcd30f34d66100fab539853b43f508787d452aeffffffff0240420f000000000017a9143e9a6b79be836762c8ef591cf16b76af1327ced58790dfdf8c0000000017a9148ce5408cfeaddb7ccb2545ded41ef478109454848700000000",
                    "txid": "28b1a5c2f0bb667aea38e760b6d55163abc9be9f1f830d9969edfab902d17a0f",
                    "hash": "28b1a5c2f0bb667aea38e760b6d55163abc9be9f1f830d9969edfab902d17a0f",
                    "depends": [],
                    "fee": 20000,
                    "sigops": 8,
                    "weight": 1332
                }, {
                    "data": "01000000013faf73481d6b96c2385b9a4300f8974b1b30c34be30000c7dcef11f68662de4501000000db00483045022100f9881f4c867b5545f6d7a730ae26f598107171d0f68b860bd973dbb855e073a002207b511ead1f8be8a55c542ce5d7e91acfb697c7fa2acd2f322b47f177875bffc901483045022100a37aa9998b9867633ab6484ad08b299de738a86ae997133d827717e7ed73d953022011e3f99d1bd1856f6a7dc0bf611de6d1b2efb60c14fc5931ba09da01558757f60147522102632178d046673c9729d828cfee388e121f497707f810c131e0d3fc0fe0bd66d62103a0951ec7d3a9da9de171617026442fcd30f34d66100fab539853b43f508787d452aeffffffff0240420f000000000017a9148d57003ecbaa310a365f8422602cc507a702197e87806868a90000000017a9148ce5408cfeaddb7ccb2545ded41ef478109454848700000000",
                    "txid": "67878210e268d87b4e6587db8c6e367457cea04820f33f01d626adbe5619b3dd",
                    "hash": "67878210e268d87b4e6587db8c6e367457cea04820f33f01d626adbe5619b3dd",
                    "depends": [],
                    "fee": 20000,
                    "sigops": 8,
                    "weight": 1336
                }],
                "coinbaseaux": {
                    "flags": ""
                },
                "coinbasevalue": 319367518,
                "longpollid": "0000000000000047e5bda122407654b25d52e0f3eeb00c152f631f70e9803772604597",
                "target": "0000000000001714480000000000000000000000000000000000000000000000",
                "mintime": 1480831053,
                "mutable": ["time", "transactions", "prevblock"],
                "noncerange": "00000000ffffffff",
                "sigoplimit": 80000,
                "sizelimit": 4000000,
                "weightlimit": 4000000,
                "curtime": 1480834892,
                "bits": "1a171448",
                "height": 1038222,
                "default_witness_commitment": "6a24aa21a9ed842a6d6672504c2b7abb796fdd7cfbd7262977b71b945452e17fbac69ed22bf8"
            }
        }
    )EOF";

    blockVersion = 0;
    SelectParams(CBaseChainParams::TESTNET);
    ASSERT_EQ(
        BitcoinUtils::IsValidDestinationString(
            "tb1qrp33g0q5c5txsp9arysrx4k6zdkfs4nce4xj0gdcccefvpysxf3q0sl5k7"),
        true);
    CTxDestination poolPayoutAddrTestnet = BitcoinUtils::DecodeDestination(
        "tb1qrp33g0q5c5txsp9arysrx4k6zdkfs4nce4xj0gdcccefvpysxf3q0sl5k7");
    vector<SubPoolInfo> subPool;
    res = sjob.initFromGbt(
        gbt.c_str(),
        poolCoinbaseInfo,
        poolPayoutAddrTestnet,
        subPool,
        blockVersion,
        "",
        RskWork(),
        VcashWork(),
        false,
        false);
    ASSERT_EQ(res, true);

    const string jsonStr = sjob.serializeToJson();
    StratumJobBitcoin sjob2;
    res = sjob2.unserializeFromJson(jsonStr.c_str(), jsonStr.length());
    ASSERT_EQ(res, true);

    ASSERT_EQ(
        sjob2.prevHash_,
        uint256S("0000000000000047e5bda122407654b25d52e0f3eeb00c152f631f70e9803"
                 "772"));
    ASSERT_EQ(
        sjob2.prevHashBeStr_,
        "e98037722f631f70eeb00c155d52e0f3407654b2e5bda1220000004700000000");
    ASSERT_EQ(sjob2.height_, 1038222);

    ASSERT_EQ(
        sjob2.coinbase2_,
        "ffffffff" // sequence
        "02" // 2 outputs
        // 5e29091300000000 -> 000000001309295e -> 319367518
        "5e29091300000000"
        // 0x22 -> 34 bytes
        "2200201863143c14c5166804bd19203356da136c985678cd4d27a1b8c6329604903262"
        //
        "0000000000000000"
        // 0x26 -> 38 bytes
        "266a24aa21a9ed842a6d6672504c2b7abb796fdd7cfbd7262977b71b945452e17fbac6"
        "9ed22bf8"
        // lock_time
        "00000000");

    ASSERT_EQ(sjob2.nVersion_, 536870912);
    ASSERT_EQ(sjob2.nBits_, 0x1a171448U);
    ASSERT_EQ(sjob2.nTime_, 1480834892U);
    ASSERT_EQ(sjob2.minTime_, 1480831053U);
    ASSERT_EQ(sjob2.coinbaseValue_, 319367518);
    ASSERT_GE(time(nullptr), jobId2Time(sjob2.jobId_));
  }
}
#endif

#ifdef CHAIN_TYPE_BTC
TEST(Stratum, StratumJobWithRskWork) {
  StratumJobBitcoin sjob;
  RskWork rskWork;
  string poolCoinbaseInfo = "/BTC.COM/";
  uint32_t blockVersion = 0;

  {
    string gbt = R"EOF(
        {
            "result": {
                "capabilities": ["proposal"],
                "version": 536870912,
                "previousblockhash": "000000004f2ea239532b2e77bb46c03b86643caac3fe92959a31fd2d03979c34",
                "transactions": [{
                    "data": "01000000010291939c5ae8191c2e7d4ce8eba7d6616a66482e3200037cb8b8c2d0af45b445000000006a47304402204df709d9e149804e358de4b082e41d8bb21b3c9d347241b728b1362aafcb153602200d06d9b6f2eca899f43dcd62ec2efb2d9ce2e10adf02738bb908420d7db93ede012103cae98ab925e20dd6ae1f76e767e9e99bc47b3844095c68600af9c775104fb36cffffffff0290f1770b000000001976a91400dc5fd62f6ee48eb8ecda749eaec6824a780fdd88aca08601000000000017a914eb65573e5dd52d3d950396ccbe1a47daf8f400338700000000",
                    "hash": "bd36bd4fff574b573152e7d4f64adf2bb1c9ab0080a12f8544c351f65aca79ff",
                    "depends": [],
                    "fee": 10000,
                    "sigops": 1
                }],
                "coinbaseaux": {
                    "flags": ""
                },
                "coinbasevalue": 312659655,
                "longpollid": "000000004f2ea239532b2e77bb46c03b86643caac3fe92959a31fd2d03979c341911",
                "target": "000000000000018ae20000000000000000000000000000000000000000000000",
                "mintime": 1469001544,
                "mutable": ["time", "transactions", "prevblock"],
                "noncerange": "00000000ffffffff",
                "sigoplimit": 20000,
                "sizelimit": 1000000,
                "curtime": 1469006933,
                "bits": "1a018ae2",
                "height": 898487
            }
        }
    )EOF";

    uint32_t creationTime = (uint32_t)time(nullptr);
    string rawgw;
    rawgw = Strings::Format(
        R"EOF(
            {
                "created_at_ts": %u,
                "rskdRpcAddress": "http://10.0.2.2:4444",
                "rskdRpcUserPwd": "user:pass",
                "target": "0x5555555555555555555555555555555555555555555555555555555555555555",
                "parentBlockHash": "0x13532f616f89e3ac2e0a9ef7363be28e7f2ca39764684995fb30c0d96e664ae4",
                "blockHashForMergedMining": "0xe6b0a8e84e0ce68471ca28db4f51b71139b0ab78ae1c3e0ae8364604e9f8a15d",
                "feesPaidToMiner": "0",
                "notify": "true"
            }
        )EOF",
        creationTime);

    blockVersion = 0;
    SelectParams(CBaseChainParams::TESTNET);

    bool resInitRskWork = rskWork.initFromGw(rawgw);

    ASSERT_TRUE(resInitRskWork);

    ASSERT_EQ(rskWork.isInitialized(), true);
    ASSERT_EQ(rskWork.getCreatedAt(), creationTime);
    ASSERT_EQ(
        rskWork.getBlockHash(),
        "0xe6b0a8e84e0ce68471ca28db4f51b71139b0ab78ae1c3e0ae8364604e9f8a15d");
    ASSERT_EQ(
        rskWork.getTarget(),
        "0x5555555555555555555555555555555555555555555555555555555555555555");
    ASSERT_EQ(rskWork.getFees(), "0");
    ASSERT_EQ(rskWork.getRpcAddress(), "http://10.0.2.2:4444");
    ASSERT_EQ(rskWork.getRpcUserPwd(), "user:pass");
    ASSERT_EQ(rskWork.getNotifyFlag(), true);

    CTxDestination poolPayoutAddrTestnet =
        BitcoinUtils::DecodeDestination("myxopLJB19oFtNBdrAxD5Z34Aw6P8o9P8U");
    vector<SubPoolInfo> subPool;
    sjob.initFromGbt(
        gbt.c_str(),
        poolCoinbaseInfo,
        poolPayoutAddrTestnet,
        subPool,
        blockVersion,
        "",
        rskWork,
        VcashWork(),
        true,
        false);

    // check rsk required data copied properly to the stratum job
    ASSERT_EQ(
        sjob.blockHashForMergedMining_,
        "0xe6b0a8e84e0ce68471ca28db4f51b71139b0ab78ae1c3e0ae8364604e9f8a15d");
    ASSERT_EQ(
        sjob.rskNetworkTarget_,
        uint256S("0x55555555555555555555555555555555555555555555555555555555555"
                 "55555"));
    ASSERT_EQ(sjob.feesForMiner_, "0");
    ASSERT_EQ(sjob.rskdRpcAddress_, "http://10.0.2.2:4444");
    ASSERT_EQ(sjob.rskdRpcUserPwd_, "user:pass");
    ASSERT_EQ(sjob.isMergedMiningCleanJob_, true);

    // check rsk merged mining tag present in the coinbase
    // Hex("RSKBLOCK:") = 0x52534b424c4f434b3a
    string rskTagHex =
        "52534b424c4f434b3ae6b0a8e84e0ce68471ca28db4f51b71139b0ab78ae1c3e0ae836"
        "4604e9f8a15d";
    size_t rskTagPos = sjob.coinbase2_.find(rskTagHex);
    ASSERT_NE(rskTagPos, string::npos);

    ASSERT_EQ(
        sjob.prevHash_,
        uint256S("000000004f2ea239532b2e77bb46c03b86643caac3fe92959a31fd2d03979"
                 "c34"));
    ASSERT_EQ(
        sjob.prevHashBeStr_,
        "03979c349a31fd2dc3fe929586643caabb46c03b532b2e774f2ea23900000000");
    ASSERT_EQ(sjob.height_, 898487);
    // 46 bytes, 5 bytes (timestamp), 9 bytes (poolCoinbaseInfo)
    // 02000000010000000000000000000000000000000000000000000000000000000000000000ffffffff1e03b7b50d
    // 0402363d58 2f4254432e434f4d2f
    ASSERT_EQ(
        sjob.coinbase1_.substr(0, 92),
        "0200000001000000000000000000000000000000000000000000000000000000000000"
        "0000ffffffff1e03b7b50d");
    ASSERT_EQ(sjob.coinbase1_.substr(102, 18), "2f4254432e434f4d2f");
    // 0402363d58 -> 0x583d3602 = 1480406530 = 2016-11-29 16:02:10
    uint32_t ts =
        (uint32_t)strtoull(sjob.coinbase1_.substr(94, 8).c_str(), nullptr, 16);
    ts = HToBe(ts);
    ASSERT_EQ(ts == time(nullptr) || ts + 1 == time(nullptr), true);

    ASSERT_EQ(
        sjob.coinbase2_,
        "ffffffff" // sequence
        "02" // 2 outputs. Rsk tag is stored in an additional CTxOut besides the
             // cb's standard output

        // c7cea21200000000 -> 0000000012a2cec7 -> 312659655
        "c7cea21200000000"
        // 0x19 -> 25 bytes of first output script
        "1976a914ca560088c0fb5e6f028faa11085e643e343a8f5c88ac"

        // rsk tx out value
        "0000000000000000"
        // 0x2b = 43 bytes of second output script containing the rsk merged
        // mining tag; 0x6a = OP_RETURN
        "2b6a2952534b424c4f434b3ae6b0a8e84e0ce68471ca28db4f51b71139b0ab78ae1c3e"
        "0ae8"
        "364604e9f8a15d"

        // lock_time
        "00000000");
    ASSERT_EQ(sjob.merkleBranch_.size(), 1U);
    ASSERT_EQ(
        sjob.merkleBranch_[0],
        uint256S("bd36bd4fff574b573152e7d4f64adf2bb1c9ab0080a12f8544c351f65aca7"
                 "9ff"));
    ASSERT_EQ(sjob.nVersion_, 536870912);
    ASSERT_EQ(sjob.nBits_, 436308706U);
    ASSERT_EQ(sjob.nTime_, 1469006933U);
    ASSERT_EQ(sjob.minTime_, 1469001544U);
    ASSERT_EQ(sjob.coinbaseValue_, 312659655);
    ASSERT_GE(time(nullptr), jobId2Time(sjob.jobId_));
  }
}
#endif

TEST(Stratum, StratumJobBeam) {
  string sjobStr = R"EOF(
    {
        "jobId": 6641843982926067808,
        "chain": "BEAM",
        "height": 34678,
        "blockBits": "04411246",
        "input": "28fe7ed7673b153ace1d5f9c52c3e108ec55a48d07ed499bdf6e7d2049049e7a",
        "rpcAddress": "http://127.0.0.1:8332",
        "rpcUserPwd": "aaabbbccc"
    }
  )EOF";

  // remove blank characters
  sjobStr.erase(
      std::remove_if(
          sjobStr.begin(),
          sjobStr.end(),
          [](unsigned char x) { return std::isspace(x); }),
      sjobStr.end());

  StratumJobBeam sjob;
  ASSERT_EQ(sjob.unserializeFromJson(sjobStr.c_str(), sjobStr.size()), true);
  ASSERT_EQ(sjob.serializeToJson(), sjobStr);
}
