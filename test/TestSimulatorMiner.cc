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

#include "StratumSession.h"
#include "StratumClient.h"

#include "utilities_js.hpp"

#include <glog/logging.h>
#include <libconfig.h++>

using namespace libconfig;

TEST(SIMULATOR, miner) {
  // config file name: simulator.cfg
  const char *conf = "simulator.cfg";
  Config cfg;
  try
  {
    cfg.readFile(conf);
  } catch(const FileIOException &fioex) {
    std::cerr << "I/O error while reading file: " << conf << std::endl;
    return;
  } catch(const ParseException &pex) {
    std::cerr << "Parse error at " << pex.getFile() << ":" << pex.getLine()
    << " - " << pex.getError() << std::endl;
    return;
  }

  //----------------------
  int32_t ssPort = 3333;
  cfg.lookupValue("simulator.ss_port", ssPort);
  string ssHost = cfg.lookup("simulator.ss_ip");
  string userName = cfg.lookup("simulator.username");

  uint32_t extraNonce1 = 0u, nTime = time(nullptr), nNonce = time(nullptr);
  uint64_t extraNonce2 = 0ull, latestDiff = 0;
  string latestJobId;
  JsonNode jnode, jresult, jerror, jparams, jmethod;

  string sbuf, line;
  TCPClientWrapper conn;
  ASSERT_TRUE(conn.connect(ssHost.c_str(), ssPort));

  // req: mining.subscribe
  {
    sbuf = "{\"id\":1,\"method\":\"mining.subscribe\",\"params\":[\"__simulator__/0.1\"]}\n";
    // send 1 byte each time, this will trigger read events several times
    for (size_t i = 0; i < sbuf.size(); i++) {
      conn.send(sbuf.substr(i, 1));
      usleep(10000);
    }

    //
    // {"id":1,"result":[[["mining.set_difficulty","01000002"],
    //                    ["mining.notify","01000002"]],"01000002",8],"error":null}
    //
    conn.getLine(line);
    ASSERT_TRUE(JsonNode::parse(line.data(), line.data() + line.size(), jnode));
    jresult = jnode["result"];
    jerror  = jnode["error"];
    ASSERT_EQ(jerror.type(), Utilities::JS::type::Null);

    auto resArr = jresult.array();
    if (resArr.size() != 3) {
      LOG(ERROR) << "result element's number is NOT 3: " << line;
      return;
    }
    extraNonce1 = resArr[1].uint32_hex();
    LOG(INFO) << "extraNonce1: " << extraNonce1;
    ASSERT_NE(extraNonce1, 0);
    ASSERT_EQ(resArr[2].int32(), 8);
  }

  // req: mining.authorize
  {
    sbuf = Strings::Format("{\"id\": 1, \"method\": \"mining.authorize\","
                               "\"params\": [\"\%s.simulator_test\", \"\"]}\n",
                               userName.c_str());
    conn.send(sbuf);
    conn.getLine(line);

    ASSERT_TRUE(JsonNode::parse(line.data(), line.data() + line.size(), jnode));
    jresult = jnode["result"];
    jerror  = jnode["error"];
    ASSERT_TRUE(jresult.boolean());
    ASSERT_EQ(jerror.type(), Utilities::JS::type::Null);
  }

  // rep: mining.set_difficulty
  {
    conn.getLine(line);

    ASSERT_TRUE(JsonNode::parse(line.data(), line.data() + line.size(), jnode));
    jparams = jnode["params"];
    jmethod = jnode["method"];

    ASSERT_EQ(jmethod.str(), "mining.set_difficulty");
    auto jparamsArr = jparams.array();
    latestDiff = jparamsArr[0].uint64();
    LOG(INFO) << "latestDiff: " << latestDiff;
  }

  // rep: mining.notify
  {
    conn.getLine(line);

    ASSERT_TRUE(JsonNode::parse(line.data(), line.data() + line.size(), jnode));
    jparams = jnode["params"];
    jmethod = jnode["method"];

    ASSERT_EQ(jmethod.str(), "mining.notify");
    auto jparamsArr = jparams.array();
    latestJobId = jparamsArr[0].str();
    LOG(INFO) << "latestJobId: " << latestJobId;
  }

  // req: mining.submit
  {
    sbuf = Strings::Format("{\"params\": [\"%s.simulator_test\",\"%s\",\"%016llx\",\"%08x\",\"%08x\"]"
                        ",\"id\":4,\"method\": \"mining.submit\"}\n",
                        userName.c_str(),
                        latestJobId.c_str(),
                        extraNonce2,
                        nTime, nNonce);
    conn.send(sbuf);
    conn.getLine(line);
    ASSERT_TRUE(JsonNode::parse(line.data(), line.data() + line.size(), jnode));

    jresult = jnode["result"];
    jerror  = jnode["error"];
    ASSERT_TRUE(jresult.boolean());
    ASSERT_TRUE(jerror.type() == Utilities::JS::type::Null);
  }

  // req: mining.submit, duplicate
  //   {"id":4,"result":null,"error":(22,"Duplicate share",null)}
  {
    sbuf = Strings::Format("{\"params\": [\"%s.simulator_test\",\"%s\",\"%016llx\",\"%08x\",\"%08x\"]"
                           ",\"id\":4,\"method\": \"mining.submit\"}\n",
                           userName.c_str(),
                           latestJobId.c_str(),
                           extraNonce2,
                           nTime, nNonce);
    conn.send(sbuf);
    conn.getLine(line);
    ASSERT_TRUE(JsonNode::parse(line.data(), line.data() + line.size(), jnode));
    ASSERT_TRUE(jnode["error"].type() != Utilities::JS::type::Null);
  }

  // req: mining.submit, time too old
  //   {"id":4,"result":null,"error":(31,"Time too old",null)}
  {
    nTime = time(nullptr) - 86400;
    sbuf = Strings::Format("{\"params\": [\"%s.simulator_test\",\"%s\",\"%016llx\",\"%08x\",\"%08x\"]"
                           ",\"id\":4,\"method\": \"mining.submit\"}\n",
                           userName.c_str(),
                           latestJobId.c_str(),
                           ++extraNonce2,
                           nTime, nNonce);
    conn.send(sbuf);
    conn.getLine(line);
    ASSERT_TRUE(JsonNode::parse(line.data(), line.data() + line.size(), jnode));
    ASSERT_TRUE(jnode["error"].type() != Utilities::JS::type::Null);
  }

  // req: mining.submit, time too new
  //   {"id":4,"result":null,"error":(32,"Time too new",null)}
  {
    nTime = time(nullptr) + 3600*2;
    sbuf = Strings::Format("{\"params\": [\"%s.simulator_test\",\"%s\",\"%016llx\",\"%08x\",\"%08x\"]"
                           ",\"id\":4,\"method\": \"mining.submit\"}\n",
                           userName.c_str(),
                           latestJobId.c_str(),
                           ++extraNonce2,
                           nTime, nNonce);
    conn.send(sbuf);
    conn.getLine(line);
    ASSERT_TRUE(JsonNode::parse(line.data(), line.data() + line.size(), jnode));
    ASSERT_TRUE(jnode["error"].type() != Utilities::JS::type::Null);
  }

  // req: mining.submit, job not found
  //   {"id":4,"result":null,"error":(21,"Job not found (=stale)",null)}
  {
    nTime = time(nullptr);
    sbuf = Strings::Format("{\"params\": [\"%s.simulator_test\",\"%s\",\"%016llx\",\"%08x\",\"%08x\"]"
                           ",\"id\":4,\"method\": \"mining.submit\"}\n",
                           userName.c_str(),
                           "s982asd2das",
                           ++extraNonce2,
                           nTime, nNonce);
    conn.send(sbuf);
    conn.getLine(line);
    ASSERT_TRUE(JsonNode::parse(line.data(), line.data() + line.size(), jnode));
    ASSERT_TRUE(jnode["error"].type() != Utilities::JS::type::Null);
  }

}



