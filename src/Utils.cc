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
#include "Utils.h"
#include "utilities_js.hpp"

#include <util.h>
#include <streams.h>

#include <stdarg.h>
#include <sys/stat.h>

#include <curl/curl.h>
#include <glog/logging.h>

static const char _hexchars[] = "0123456789abcdef";

static inline int _hex2bin_char(const char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return (c - 'a') + 10;
  if (c >= 'A' && c <= 'F')
    return (c - 'A') + 10;
  return -1;
}

bool Hex2Bin(const char *in, size_t size, vector<char> &out) {
  out.clear();
  out.reserve(size/2);

  uint8 h, l;
  // skip space, 0x
  const char *psz = in;
  while (isspace(*psz))
    psz++;
  if (psz[0] == '0' && tolower(psz[1]) == 'x')
    psz += 2;

  // convert
  while (psz + 2 <= (char *)in + size) {
    h = _hex2bin_char(*psz++);
    l = _hex2bin_char(*psz++);
    out.push_back((h << 4) | l);
  }
  return true;

}

bool Hex2Bin(const char *in, vector<char> &out) {
  out.clear();
  out.reserve(strlen(in)/2);

  uint8 h, l;
  // skip space, 0x
  const char *psz = in;
  while (isspace(*psz))
    psz++;
  if (psz[0] == '0' && tolower(psz[1]) == 'x')
    psz += 2;

  if (strlen(psz) % 2 == 1) { return false; }

  // convert
  while (*psz != '\0' && *(psz + 1) != '\0') {
    h = _hex2bin_char(*psz++);
    l = _hex2bin_char(*psz++);
    out.push_back((h << 4) | l);
  }
  return true;
}

void Bin2Hex(const uint8 *in, size_t len, string &str) {
  str.clear();
  const uint8 *p = in;
  while (len--) {
    str.push_back(_hexchars[p[0] >> 4]);
    str.push_back(_hexchars[p[0] & 0xf]);
    ++p;
  }
}

void Bin2Hex(const vector<char> &in, string &str) {
  Bin2Hex((uint8 *)in.data(), in.size(), str);
}

void Bin2HexR(const vector<char> &in, string &str) {
  vector<char> r;
  for (auto it = in.rbegin(); it != in.rend(); ++it) {
    r.push_back(*it);
  }
  Bin2Hex(r, str);
}

//  Receive 0MQ string from socket and convert into string
std::string s_recv (zmq::socket_t & socket) {
  zmq::message_t message;
  socket.recv(&message);

  return std::string(static_cast<char*>(message.data()), message.size());
}

//  Convert string to 0MQ string and send to socket
bool s_send(zmq::socket_t & socket, const std::string & string) {
  zmq::message_t message(string.size());
  memcpy(message.data(), string.data(), string.size());

  bool rc = socket.send(message);
  return (rc);
}

//  Sends string as 0MQ string, as multipart non-terminal
bool s_sendmore (zmq::socket_t & socket, const std::string & string) {
  zmq::message_t message(string.size());
  memcpy(message.data(), string.data(), string.size());

  bool rc = socket.send(message, ZMQ_SNDMORE);
  return (rc);
}



struct CurlChunk {
  char *memory;
  size_t size;
};

static size_t
CurlWriteChunkCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
  size_t realsize = size * nmemb;
  struct CurlChunk *mem = (struct CurlChunk *)userp;

  mem->memory = (char *)realloc(mem->memory, mem->size + realsize + 1);
  if(mem->memory == NULL) {
    /* out of memory! */
    printf("not enough memory (realloc returned NULL)\n");
    return 0;
  }

  memcpy(&(mem->memory[mem->size]), contents, realsize);
  mem->size += realsize;
  mem->memory[mem->size] = 0;

  return realsize;
}

bool httpGET(const char *url, string &response, long timeoutMs) {
  return httpPOST(url, nullptr, nullptr, response, timeoutMs, nullptr);
}

bool httpGET(const char *url, const char *userpwd,
             string &response, long timeoutMs) {
  return httpPOST(url, userpwd, nullptr, response, timeoutMs, nullptr);
}

bool httpPOST(const char *url, const char *userpwd, const char *postData,
              string &response, long timeoutMs, const char *mineType) {
  struct curl_slist *headers = NULL;
  CURLcode status;
  long code;
  CURL *curl = curl_easy_init();
  struct CurlChunk chunk;
  if (!curl) {
    return false;
  }

  chunk.memory = (char *)malloc(1);  /* will be grown as needed by the realloc above */
  chunk.size   = 0;          /* no data at this point */

  // RSK doesn't support 'Expect: 100-Continue' in 'HTTP/1.1'.
  // So switch to 'HTTP/1.0'.
  curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_0);

  if (mineType != nullptr) {
    string mineHeader = string("Content-Type: ") + string(mineType);
    headers = curl_slist_append(headers, mineHeader.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  }

  curl_easy_setopt(curl, CURLOPT_URL, url);

  if (postData != nullptr) {
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(postData));
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS,    postData);
  }

  if (userpwd != nullptr)
    curl_easy_setopt(curl, CURLOPT_USERPWD, userpwd);

  curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_TRY);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "curl");

  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeoutMs);

  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteChunkCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA,     (void *)&chunk);

  status = curl_easy_perform(curl);
  if (status != 0) {
    LOG(ERROR) << "unable to request data from: " << url << ", error: " << curl_easy_strerror(status);
    goto error;
  }

  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
  if (code != 200) {
    LOG(ERROR) << "server responded with code: " << code;
    goto error;
  }

  response.assign(chunk.memory, chunk.size);

  curl_easy_cleanup(curl);
  curl_slist_free_all(headers);
  free(chunk.memory);
  return true;


error:
  if (curl)
    curl_easy_cleanup(curl);
  if (headers)
    curl_slist_free_all(headers);

  free(chunk.memory);
  return false;
}

bool bitcoindRpcCall(const char *url, const char *userpwd, const char *reqData,
                     string &response) {
  return httpPOST(url, userpwd, reqData, response, 5000/* timeout ms */, "application/json");
}


//
// %y	Year, last two digits (00-99)	01
// %Y	Year	2001
// %m	Month as a decimal number (01-12)	08
// %d	Day of the month, zero-padded (01-31)	23
// %H	Hour in 24h format (00-23)	14
// %I	Hour in 12h format (01-12)	02
// %M	Minute (00-59)	55
// %S	Second (00-61)	02
//
// %D	Short MM/DD/YY date, equivalent to %m/%d/%y	08/23/01
// %F	Short YYYY-MM-DD date, equivalent to %Y-%m-%d	2001-08-23
// %T	ISO 8601 time format (HH:MM:SS), equivalent to %H:%M:%S	14:55:02
//
string date(const char *format, const time_t timestamp) {
  char buffer[80] = {0};
  struct tm tm;
  time_t ts = timestamp;
  gmtime_r(&ts, &tm);
  strftime(buffer, sizeof(buffer), format, &tm);
  return string(buffer);
}

time_t str2time(const char *str, const char *format) {
  struct tm tm;
  strptime(str, format, &tm);
  return timegm(&tm);
}

void writeTime2File(const char *filename, uint32_t t) {
  FILE *fp = fopen(filename, "w");
  if (!fp) { return; }
  fprintf(fp, "%u", t);
  fclose(fp);
}

string Strings::Format(const char * fmt, ...) {
  char tmp[512];
  string dest;
  va_list al;
  va_start(al, fmt);
  int len = vsnprintf(tmp, 512, fmt, al);
  va_end(al);
  if (len>511) {
    char * destbuff = new char[len+1];
    va_start(al, fmt);
    len = vsnprintf(destbuff, len+1, fmt, al);
    va_end(al);
    dest.append(destbuff, len);
    delete[] destbuff;
  } else {
    dest.append(tmp, len);
  }
  return dest;
}

void Strings::Append(string & dest, const char * fmt, ...) {
  char tmp[512];
  va_list al;
  va_start(al, fmt);
  int len = vsnprintf(tmp, 512, fmt, al);
  va_end(al);
  if (len>511) {
    char * destbuff = new char[len+1];
    va_start(al, fmt);
    len = vsnprintf(destbuff, len+1, fmt, al);
    va_end(al);
    dest.append(destbuff, len);
    delete[] destbuff;
  } else {
    dest.append(tmp, len);
  }
}

string score2Str(double s) {
  if (s <= 0.0) {
    return "0";
  }

  string f;
  int p = -1;
  if (s >= 1.0) {
    p = 15 - (int)ceil(log10(s)) - 1;
  } else {
    p = 15 + -1 * (int)floor(log10(s)) - 1;
  }

  assert(p >= 0);
  f = Strings::Format("%%.%df", p > 25 ? 25 : p);
  return Strings::Format(f.c_str(), s);
}

bool fileExists(const char* file) {
  struct stat buf;
  return (stat(file, &buf) == 0);
}

bool checkBitcoinRPC(const string &rpcAddr, const string &rpcUserpass) {
  string response;
  string request = "{\"jsonrpc\":\"1.0\",\"id\":\"1\",\"method\":\"getnetworkinfo\",\"params\":[]}";
  bool res = bitcoindRpcCall(rpcAddr.c_str(), rpcUserpass.c_str(),
                             request.c_str(), response);
  if (!res) {
    LOG(ERROR) << "rpc call failure";
    return false;
  }

  LOG(INFO) << "getnetworkinfo: " << response;

  JsonNode r;
  if (!JsonNode::parse(response.c_str(),
                       response.c_str() + response.length(), r)) {
    LOG(ERROR) << "decode getnetworkinfo failure";
    return false;
  }

  // check if the method not found
  if (r["result"].type() != Utilities::JS::type::Obj) {
    LOG(INFO) << "node doesn't support getnetworkinfo, try getinfo";

    request = "{\"jsonrpc\":\"1.0\",\"id\":\"1\",\"method\":\"getinfo\",\"params\":[]}";
    res = bitcoindRpcCall(rpcAddr.c_str(), rpcUserpass.c_str(),
                          request.c_str(), response);
    if (!res) {
      LOG(ERROR) << "rpc call failure";
      return false;
    }

    LOG(INFO) << "getinfo: " << response;

    if (!JsonNode::parse(response.c_str(),
                         response.c_str() + response.length(), r)) {
      LOG(ERROR) << "decode getinfo failure";
      return false;
    }
  }

  // check fields & connections
  if (r["result"].type() != Utilities::JS::type::Obj ||
      r["result"]["connections"].type() != Utilities::JS::type::Int) {
    LOG(ERROR) << "getnetworkinfo missing some fields";
    return false;
  }
  // if (r["result"]["connections"].int32() <= 0) {
  //   LOG(ERROR) << "node connections is zero";
  //   return false;
  // }

  return true;
}
