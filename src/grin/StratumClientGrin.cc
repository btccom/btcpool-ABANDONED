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

#include "StratumClientGrin.h"

/*
 The MIT License (MIT)

 Copyright (c) [2018] [BTC.COM]

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

#include "StratumClientGrin.h"

#include <fmt/format.h>

#include <libconfig.h++>

StratumClientGrin::StratumClientGrin(
    bool enableTLS,
    struct event_base *base,
    const string &workerFullName,
    const string &workerPasswd,
    const libconfig::Config &config)
  : StratumClient{enableTLS, base, workerFullName, workerPasswd, config}
  , id_{0}
  , height_{0}
  , edgeBits_{29} {
  config.lookupValue("simulator.edge_bits", edgeBits_);
  if (edgeBits_ < 29 || edgeBits_ == 30) {
    LOG(FATAL) << "Edge bits can only be 29 or 31+";
  }
}

void StratumClientGrin::sendHelloData() {
  auto s = fmt::format(
      "{{"
      "\"id\":\"0\","
      "\"jsonrpc\":\"2.0\","
      "\"method\":\"login\","
      "\"params\":{{"
      "\"login\":\"{}\","
      "\"pass\":\"{}\","
      "\"agent\":\"__simulator__/0.1\""
      "}}}}\n",
      workerFullName_,
      workerPasswd_);
  sendData(s);
}

string StratumClientGrin::constructShare() {
  return fmt::format(
      "{{\"id\":\"0\","
      "\"jsonrpc\":\"2.0\","
      "\"method\":\"submit\","
      "\"params\":{{"
      "\"edge_bits\":{},"
      "\"height\":{},"
      "\"job_id\":{},"
      "\"nonce\":{},"
      "\"pow\":["
      "4210040,10141596,13269632,24291934,28079062,84254573,84493890,"
      "100560174,100657333,120128285,130518226,140371663,142109188,159800646,"
      "163323737,171019100,176840047,191220010,192245584,198941444,209276164,"
      "216952635,217795152,225662613,230166736,231315079,248639876,263910393,"
      "293995691,298361937,326412694,330363619,414572127,424798984,426489226,"
      "466671748,466924466,490048497,495035248,496623057,502828197,532838434"
      "]}}}}\n",
      edgeBits_,
      height_,
      id_,
      extraNonce2_++);
}

void StratumClientGrin::handleLine(const string &line) {
  DLOG(INFO) << "recv(" << line.size() << "): " << line;

  JsonNode jnode;
  if (!JsonNode::parse(line.data(), line.data() + line.size(), jnode)) {
    LOG(ERROR) << "decode line fail, not a json string";
    return;
  }

  JsonNode jmethod = jnode["method"];
  JsonNode jparams = jnode["params"];

  if (jmethod.type() == Utilities::JS::type::Str) {
    if (jmethod.str() == "job") {
      // {
      //    "id":"Stratum",
      //    "jsonrpc":"2.0",
      //    "method":"job",
      //    "params":{
      //       "difficulty":1,
      //       "height":16375,
      //       "job_id":5,
      //       "pre_pow":"..."
      //    }
      // }
      id_ = jparams["job_id"].uint32();
      height_ = jparams["height"].uint64();

      state_ = AUTHENTICATED;
      return;
    }
  }
}
