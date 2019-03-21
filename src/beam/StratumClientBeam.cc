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

#include "StratumClientBeam.h"
#include "Utils.h"

////////////////////////////// StratumClientBeam ////////////////////////////
void StratumClientBeam::sendHelloData() {
  sendData(Strings::Format(
      "{"
      "\"method\":\"login\","
      "\"api_key\":\"%s\","
      "\"id\":\"login\","
      "\"jsonrpc\":\"2.0\""
      "}\n",
      workerFullName_));
  state_ = SUBSCRIBED;
}

string StratumClientBeam::constructShare() {
  string s = Strings::Format(
      "{\"method\":\"solution\",\"id\":\"%s\",\"nonce\":\"%016x\",\"output\":"
      "\"1dc4ae5dd850fa32ef3316042735113cdb25be2b2a046b2acaa237e1076b66fee6204d"
      "2339a3ae742d4f3d4efb6a473eeb58e85620a5befb4d93854d8bf6074a464670ffac6fba"
      "b57c73ff09280f4bdf592129f5b74ed85224a19b6908daae110e878a34ff2cfb2b\","
      "\"jsonrpc\":\"2.0\"}\n",
      id_,
      extraNonce2_);

  extraNonce2_++;
  return s;
}

void StratumClientBeam::handleLine(const string &line) {
  DLOG(INFO) << "recv(" << line.size() << "): " << line;

  JsonNode jnode;
  if (!JsonNode::parse(line.data(), line.data() + line.size(), jnode)) {
    LOG(ERROR) << "decode line fail, not a json string";
    return;
  }

  JsonNode jmethod = jnode["method"];

  if (jmethod.type() == Utilities::JS::type::Str) {
    // {"difficulty":465715110,"height":68758,"id":"49","input":"6782137d65c53b1685659e21a1dc10f12bba0082357c455d127fae346d90669a","jsonrpc":"2.0","method":"job"}
    if (jmethod.str() == "job") {
      id_ = jnode["id"].str();
      input_ = jnode["input"].str();
      height_ = jnode["height"].uint32();
      bits_ = jnode["difficulty"].uint32();

      DLOG(INFO) << "job id: " << id_ << ", height: " << height_
                 << ", input: " << input_
                 << ", bits: " << Strings::Format("%08x", bits_);

      state_ = AUTHENTICATED;
      return;
    }
  }
}
