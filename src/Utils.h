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
#ifndef UTILS_H_
#define UTILS_H_

#include <string>
#include <sstream>
#include <vector>

#include "bitcoin/base58.h"
#include "bitcoin/core.h"
#include "bitcoin/util.h"

#include "zmq.hpp"

#include "Common.h"

string EncodeHexTx(const CTransaction &tx);
string EncodeHexBlock(const CBlock &block);

bool Hex2Bin(const char *in, size_t size, vector<char> &out);
bool Hex2Bin(const char *in, vector<char> &out);
void Bin2Hex(const uint8 *in, size_t len, string &str);
void Bin2Hex(const vector<char> &in, string &str);
void Bin2HexR(const vector<char> &in, string &str);

bool DecodeHexTx(CTransaction& tx, const std::string& strHexTx);
bool DecodeBinTx(CTransaction& tx, const unsigned char *data, size_t len);

bool DecodeHexBlk(CBlock& block, const std::string& strHexBlk);
bool DecodeBinBlk(CBlock& block, const unsigned char *data, size_t len);

std::string s_recv(zmq::socket_t & socket);
bool s_send(zmq::socket_t & socket, const std::string & string);
bool s_sendmore (zmq::socket_t & socket, const std::string & string);

bool bitcoindRpcCall(const char *url, const char *userpwd, const char *reqData,
                     string &response);

string date(const char *format, const time_t timestamp);
inline string date(const char *format) {
  return date(format, time(nullptr));
}

#endif
