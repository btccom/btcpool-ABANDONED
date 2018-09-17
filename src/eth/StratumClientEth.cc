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

#include "StratumClientEth.h"
#include "Utils.h"

////////////////////////////// StratumClientEth ////////////////////////////
StratumClientEth::StratumClientEth(struct event_base *base, const string &workerFullName) :
StratumClient(base, workerFullName),
header_(rand())
{
}

string StratumClientEth::constructShare()
{
  // {"id": 4, "method": "mining.submit",
  // "params": ["0x7b9d694c26a210b9f0d35bb9bfdd70a413351111.fatrat1117",
  // "ae778d304393d441bf8e1c47237261675caa3827997f671d8e5ec3bd5d862503",
  // "0x4cc7c01bfbe51c67","0xae778d304393d441bf8e1c47237261675caa3827997f671d8e5ec3bd5d862503",
  // "0x52fdd9e9a796903c6b88af4192717e77d9a9c6fa6a1366540b65e6bcfa9069aa"]}
  string s = Strings::Format("{\"id\": 4, \"method\": \"mining.submit\", "
                      "\"params\": [\"%s\",\"%s\",\"0x%08x%08x\",\"0x%s\",\"0x%s\"]}\n",
                      "ccc",
                      header_.GetHex().c_str(),
                      extraNonce2_ >> 32,
                      extraNonce2_,
                      header_.GetHex().c_str(),
                      header_.GetHex().c_str());

  extraNonce2_++;
  header_++;
  return s;
}

void StratumClientEth::handleLine(const string &line) {
  DLOG(INFO) << "recv(" << line.size() << "): " << line;

  JsonNode jnode;
  if (!JsonNode::parse(line.data(), line.data() + line.size(), jnode)) {
    LOG(ERROR) << "decode line fail, not a json string";
    return;
  }
  JsonNode jresult  = jnode["result"];
  JsonNode jerror   = jnode["error"];
  JsonNode jmethod  = jnode["method"];

  if (jmethod.type() == Utilities::JS::type::Str) {
    JsonNode jparams  = jnode["params"];
    auto jparamsArr = jparams.array();

    if (jmethod.str() == "mining.notify") {
      latestJobId_ = jparamsArr[0].str();
      DLOG(INFO) << "latestJobId_: " << latestJobId_;
    }
    else if (jmethod.str() == "mining.set_difficulty") {
      latestDiff_ = jparamsArr[0].uint64();
      DLOG(INFO) << "latestDiff_: " << latestDiff_;
    }
    else
    {
      LOG(ERROR) << "unknown method: " << line;
    }
    return;
  }

  if (state_ == AUTHENTICATED) {
    //
    // {"error": null, "id": 2, "result": true}
    //
    if (jerror.type()  != Utilities::JS::type::Null ||
        jresult.type() != Utilities::JS::type::Bool ||
        jresult.boolean() != true) {
//      LOG(ERROR) << "json result is null, err: " << jerror.str() << ", line: " << line;
    }
    return;
  }

  if (state_ == CONNECTED) {
    // mining.authorize
    state_ = SUBSCRIBED;
    string s = Strings::Format("{\"id\": 1, \"method\": \"mining.authorize\","
                               "\"params\": [\"\%s\", \"\"]}\n",
                               workerFullName_.c_str());
    sendData(s);
    return;
  }

  if (state_ == SUBSCRIBED && jresult.boolean() == true) {
    state_ = AUTHENTICATED;
    return;
  }
}
