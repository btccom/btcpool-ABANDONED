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
#ifndef POOL_UTILS_H_
#define POOL_UTILS_H_

#include <string>
#include <sstream>
#include <vector>

#include <base58.h>
#include <util.h>
#include <utilstrencodings.h>
#include <streams.h>

#include "zmq.hpp"

#include "Common.h"

bool Hex2Bin(const char *in, size_t size, vector<char> &out);
bool Hex2Bin(const char *in, vector<char> &out);
void Bin2Hex(const uint8 *in, size_t len, string &str);
void Bin2Hex(const vector<char> &in, string &str);
void Bin2HexR(const vector<char> &in, string &str);

//bool DecodeBinTx(CTransaction& tx, const unsigned char *data, size_t len);
//bool DecodeBinBlk(CBlock& block, const unsigned char *data, size_t len);

std::string s_recv(zmq::socket_t & socket);
bool s_send(zmq::socket_t & socket, const std::string & string);
bool s_sendmore (zmq::socket_t & socket, const std::string & string);


bool httpGET (const char *url, string &response, long timeoutMs);
bool httpGET (const char *url, const char *userpwd,
              string &response, long timeoutMs);
bool httpPOST(const char *url, const char *userpwd, const char *postData,
              string &response, long timeoutMs, const char *contentType);
bool httpPOST(const char *url, const char *userpwd, const char *postData,
              string &response, long timeoutMs, const char *contentType, const char *agent);
bool bitcoindRpcCall(const char *url, const char *userpwd, const char *reqData,
                     string &response);
bool rpcCall(const char *url, const char *userpwd, const char *reqData, string &response, const char *agent); 

string date(const char *format, const time_t timestamp);
inline string date(const char *format) {
  return date(format, time(nullptr));
}
time_t str2time(const char *str, const char *format);
inline time_t str2time(const char *str) {
  return str2time(str, "%F %T");
}

void writeTime2File(const char *filename, uint32_t t);

class Strings {
public:
  static string Format(const char * fmt, ...);
  static void Append(string & dest, const char * fmt, ...);
};

string score2Str(double s);

// we use G, so never overflow
inline double share2HashrateG(uint64_t share, uint32_t timeDiff) {
  //    G: 1000000000.0
  // 2^32: 4294967296.0
  return share * (4294967296.0 / 1000000000.0 / timeDiff);
}
inline double share2HashrateT(uint64_t share, uint32_t timeDiff) {
  return share2HashrateG(share, timeDiff) / 1000.0;
}
inline double share2HashrateP(uint64_t share, uint32_t timeDiff) {
  return share2HashrateG(share, timeDiff) / 1000000.0;
}

bool fileExists(const char* file);

bool checkBitcoinRPC(const string &rpcAddr, const string &rpcUserpass);

#endif
