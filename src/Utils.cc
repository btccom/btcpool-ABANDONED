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

bool Hex2BinReverse(const char *in, size_t size, vector<char> &out) {
  out.clear();
  out.reserve(size / 2);

  uint8_t h, l;
  // skip space, 0x
  const char *psz = in + size - 1;
  while (isspace(*psz))
    psz--;

  // convert
  while (psz > in) {
    if (*psz == 'x')
      break;
    l = _hex2bin_char(*psz--);
    h = _hex2bin_char(*psz--);

    out.push_back((h << 4) | l);
  }
  return true;
}

bool Hex2Bin(const char *in, size_t size, vector<char> &out) {
  out.clear();
  out.reserve(size / 2);

  uint8_t h, l;
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
  out.reserve(strlen(in) / 2);

  uint8_t h, l;
  // skip space, 0x
  const char *psz = in;
  while (isspace(*psz))
    psz++;
  if (psz[0] == '0' && tolower(psz[1]) == 'x')
    psz += 2;

  if (strlen(psz) % 2 == 1) {
    return false;
  }

  // convert
  while (*psz != '\0' && *(psz + 1) != '\0') {
    h = _hex2bin_char(*psz++);
    l = _hex2bin_char(*psz++);
    out.push_back((h << 4) | l);
  }
  return true;
}

void Bin2Hex(const uint8_t *in, size_t len, string &str) {
  str.clear();
  const uint8_t *p = in;
  while (len--) {
    str.push_back(_hexchars[p[0] >> 4]);
    str.push_back(_hexchars[p[0] & 0xf]);
    ++p;
  }
}

void Bin2Hex(const vector<uint8_t> &in, string &str) {
  Bin2Hex(in.data(), in.size(), str);
}

void Bin2Hex(const vector<char> &in, string &str) {
  Bin2Hex((uint8_t *)in.data(), in.size(), str);
}

void Bin2HexR(const uint8_t *in, size_t len, string &str) {
  vector<char> r;
  r.resize(len);
  for (size_t i = 0; i < len; ++i) {
    r[i] = in[len - 1 - i];
  }
  Bin2Hex(r, str);
}

void Bin2HexR(const vector<char> &in, string &str) {
  Bin2HexR((const uint8_t *)in.data(), in.size(), str);
}

//  Receive 0MQ string from socket and convert into string
std::string s_recv(zmq::socket_t &socket) {
  zmq::message_t message;
  socket.recv(&message);

  return std::string(static_cast<char *>(message.data()), message.size());
}

//  Convert string to 0MQ string and send to socket
bool s_send(zmq::socket_t &socket, const std::string &string) {
  zmq::message_t message(string.size());
  memcpy(message.data(), string.data(), string.size());

  bool rc = socket.send(message);
  return (rc);
}

//  Sends string as 0MQ string, as multipart non-terminal
bool s_sendmore(zmq::socket_t &socket, const std::string &string) {
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
CurlWriteChunkCallback(void *contents, size_t size, size_t nmemb, void *userp) {
  size_t realsize = size * nmemb;
  struct CurlChunk *mem = (struct CurlChunk *)userp;

  mem->memory = (char *)realloc(mem->memory, mem->size + realsize + 1);
  if (mem->memory == NULL) {
    /* out of memory! */
    printf("not enough memory (realloc returned NULL)\n");
    return 0;
  }

  memcpy(&(mem->memory[mem->size]), contents, realsize);
  mem->size += realsize;
  mem->memory[mem->size] = 0;

  return realsize;
}

// This may be ugly but I really do not want to modify every places calling HTTP
// methods...
static bool sslVerifyPeer = true;

void setSslVerifyPeer(bool verifyPeer) {
  sslVerifyPeer = verifyPeer;
}

bool httpGET(const char *url, string &response, long timeoutMs) {
  return httpPOST(url, nullptr, nullptr, response, timeoutMs, nullptr);
}

bool httpGET(
    const char *url, const char *userpwd, string &response, long timeoutMs) {
  return httpPOST(url, userpwd, nullptr, response, timeoutMs, nullptr);
}

bool httpPOSTImpl(
    const char *url,
    const char *userpwd,
    const char *postData,
    int len,
    string &response,
    long timeoutMs,
    const char *mineType,
    const char *agent) {
  struct curl_slist *headers = NULL;
  CURLcode status;
  long code;
  CURL *curl = curl_easy_init();
  struct CurlChunk chunk;
  if (!curl) {
    return false;
  }

  chunk.memory =
      (char *)malloc(1); /* will be grown as needed by the realloc above */
  chunk.size = 0; /* no data at this point */

  if (mineType != nullptr) {
    string mineHeader = string("Content-Type: ") + string(mineType);
    headers = curl_slist_append(headers, mineHeader.c_str());
  }

  curl_easy_setopt(curl, CURLOPT_URL, url);

  if (postData != nullptr) {
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, len);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData);

    // RSK doesn't support 'Expect: 100-Continue' in 'HTTP/1.1'.
    // setting this header to empty will disable it in curl
    headers = curl_slist_append(headers, "Expect:");
  }

  if (headers != nullptr) {
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  }

  if (userpwd != nullptr)
    curl_easy_setopt(curl, CURLOPT_USERPWD, userpwd);

  curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_TRY);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, sslVerifyPeer);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, agent);

  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeoutMs);

  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteChunkCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);

  status = curl_easy_perform(curl);
  if (status != 0) {
    LOG(ERROR) << "unable to request data from: " << url
               << ", error: " << curl_easy_strerror(status);
    goto error;
  }

  if (chunk.size > 0)
    response.assign(chunk.memory, chunk.size);

  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
  // status code 200 - 208 indicates ok
  // sia returns 204 as success
  if (code < 200 || code > 208) {
    LOG(ERROR) << "server responded with code: " << code;
    goto error;
  }

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

bool httpPOST(
    const char *url,
    const char *userpwd,
    const char *postData,
    string &response,
    long timeoutMs,
    const char *mineType,
    const char *agent) {
  return httpPOSTImpl(
      url,
      userpwd,
      postData,
      postData != nullptr ? strlen(postData) : 0,
      response,
      timeoutMs,
      mineType,
      agent);
}

bool httpPOST(
    const char *url,
    const char *userpwd,
    const char *postData,
    string &response,
    long timeoutMs,
    const char *mineType) {
  return httpPOST(
      url, userpwd, postData, response, timeoutMs, mineType, "curl");
}

bool blockchainNodeRpcCall(
    const char *url,
    const char *userpwd,
    const char *reqData,
    string &response) {
  return httpPOST(
      url,
      userpwd,
      reqData,
      response,
      5000 /* timeout ms */,
      "application/json");
}

bool rpcCall(
    const char *url,
    const char *userpwd,
    const char *reqData,
    int len,
    string &response,
    const char *agent) {
  return httpPOSTImpl(
      url, userpwd, reqData, len, response, 5000, "application/json", agent);
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
  if (!fp) {
    return;
  }
  fprintf(fp, "%u", t);
  fclose(fp);
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
  return Strings::Format(f, s);
}

bool fileExists(const char *file) {
  struct stat buf;
  return (stat(file, &buf) == 0);
}

bool fileNonEmpty(const char *file) {
  struct stat buf;
  return (stat(file, &buf) == 0) && (buf.st_size > 0);
}

string
getStatsFilePath(const char *chainType, const string &dataDir, time_t ts) {
  bool needSlash = false;
  if (dataDir.length() > 0 && *dataDir.rbegin() != '/') {
    needSlash = true;
  }
  // filename: sharelog-2016-07-12.bin
  return Strings::Format(
      "%s%ssharelog%s-%s.bin",
      dataDir,
      needSlash ? "/" : "",
      chainType,
      date("%F", ts));
}

// A 37-character character set.
// a-z, A-Z: 0-25
// 0-9:      26-35
// others:   36
static const uint8_t kAlphaNumRankBase = 37;
static const uint8_t kAlphaNumRankTable[256] = {
    36 /* 0 */,   36 /* 1 */,   36 /* 2 */,   36 /* 3 */,
    36 /* 4 */,   36 /* 5 */,   36 /* 6 */,   36 /* 7 */,
    36 /* 8 */,   36 /* 9 */,   36 /* 10 */,  36 /* 11 */,
    36 /* 12 */,  36 /* 13 */,  36 /* 14 */,  36 /* 15 */,
    36 /* 16 */,  36 /* 17 */,  36 /* 18 */,  36 /* 19 */,
    36 /* 20 */,  36 /* 21 */,  36 /* 22 */,  36 /* 23 */,
    36 /* 24 */,  36 /* 25 */,  36 /* 26 */,  36 /* 27 */,
    36 /* 28 */,  36 /* 29 */,  36 /* 30 */,  36 /* 31 */,
    36 /* ' ' */, 36 /* '!' */, 36 /* '"' */, 36 /* '#' */,
    36 /* '$' */, 36 /* '%' */, 36 /* '&' */, 36 /* '\'' */,
    36 /* '(' */, 36 /* ')' */, 36 /* '*' */, 36 /* '+' */,
    36 /* ',' */, 36 /* '-' */, 36 /* '.' */, 36 /* '/' */,
    26 /* '0' */, 27 /* '1' */, 28 /* '2' */, 29 /* '3' */,
    30 /* '4' */, 31 /* '5' */, 32 /* '6' */, 33 /* '7' */,
    34 /* '8' */, 35 /* '9' */, 36 /* ':' */, 36 /* ';' */,
    36 /* '<' */, 36 /* '=' */, 36 /* '>' */, 36 /* '?' */,
    36 /* '@' */, 0 /* 'A' */,  1 /* 'B' */,  2 /* 'C' */,
    3 /* 'D' */,  4 /* 'E' */,  5 /* 'F' */,  6 /* 'G' */,
    7 /* 'H' */,  8 /* 'I' */,  9 /* 'J' */,  10 /* 'K' */,
    11 /* 'L' */, 12 /* 'M' */, 13 /* 'N' */, 14 /* 'O' */,
    15 /* 'P' */, 16 /* 'Q' */, 17 /* 'R' */, 18 /* 'S' */,
    19 /* 'T' */, 20 /* 'U' */, 21 /* 'V' */, 22 /* 'W' */,
    23 /* 'X' */, 24 /* 'Y' */, 25 /* 'Z' */, 36 /* '[' */,
    36 /* '\' */, 36 /* ']' */, 36 /* '^' */, 36 /* '_' */,
    36 /* '`' */, 0 /* 'a' */,  1 /* 'b' */,  2 /* 'c' */,
    3 /* 'd' */,  4 /* 'e' */,  5 /* 'f' */,  6 /* 'g' */,
    7 /* 'h' */,  8 /* 'i' */,  9 /* 'j' */,  10 /* 'k' */,
    11 /* 'l' */, 12 /* 'm' */, 13 /* 'n' */, 14 /* 'o' */,
    15 /* 'p' */, 16 /* 'q' */, 17 /* 'r' */, 18 /* 's' */,
    19 /* 't' */, 20 /* 'u' */, 21 /* 'v' */, 22 /* 'w' */,
    23 /* 'x' */, 24 /* 'y' */, 25 /* 'z' */, 36 /* '{' */,
    36 /* '|' */, 36 /* '}' */, 36 /* '~' */, 36 /* 127 */,
    36 /* 128 */, 36 /* 129 */, 36 /* 130 */, 36 /* 131 */,
    36 /* 132 */, 36 /* 133 */, 36 /* 134 */, 36 /* 135 */,
    36 /* 136 */, 36 /* 137 */, 36 /* 138 */, 36 /* 139 */,
    36 /* 140 */, 36 /* 141 */, 36 /* 142 */, 36 /* 143 */,
    36 /* 144 */, 36 /* 145 */, 36 /* 146 */, 36 /* 147 */,
    36 /* 148 */, 36 /* 149 */, 36 /* 150 */, 36 /* 151 */,
    36 /* 152 */, 36 /* 153 */, 36 /* 154 */, 36 /* 155 */,
    36 /* 156 */, 36 /* 157 */, 36 /* 158 */, 36 /* 159 */,
    36 /* 160 */, 36 /* 161 */, 36 /* 162 */, 36 /* 163 */,
    36 /* 164 */, 36 /* 165 */, 36 /* 166 */, 36 /* 167 */,
    36 /* 168 */, 36 /* 169 */, 36 /* 170 */, 36 /* 171 */,
    36 /* 172 */, 36 /* 173 */, 36 /* 174 */, 36 /* 175 */,
    36 /* 176 */, 36 /* 177 */, 36 /* 178 */, 36 /* 179 */,
    36 /* 180 */, 36 /* 181 */, 36 /* 182 */, 36 /* 183 */,
    36 /* 184 */, 36 /* 185 */, 36 /* 186 */, 36 /* 187 */,
    36 /* 188 */, 36 /* 189 */, 36 /* 190 */, 36 /* 191 */,
    36 /* 192 */, 36 /* 193 */, 36 /* 194 */, 36 /* 195 */,
    36 /* 196 */, 36 /* 197 */, 36 /* 198 */, 36 /* 199 */,
    36 /* 200 */, 36 /* 201 */, 36 /* 202 */, 36 /* 203 */,
    36 /* 204 */, 36 /* 205 */, 36 /* 206 */, 36 /* 207 */,
    36 /* 208 */, 36 /* 209 */, 36 /* 210 */, 36 /* 211 */,
    36 /* 212 */, 36 /* 213 */, 36 /* 214 */, 36 /* 215 */,
    36 /* 216 */, 36 /* 217 */, 36 /* 218 */, 36 /* 219 */,
    36 /* 220 */, 36 /* 221 */, 36 /* 222 */, 36 /* 223 */,
    36 /* 224 */, 36 /* 225 */, 36 /* 226 */, 36 /* 227 */,
    36 /* 228 */, 36 /* 229 */, 36 /* 230 */, 36 /* 231 */,
    36 /* 232 */, 36 /* 233 */, 36 /* 234 */, 36 /* 235 */,
    36 /* 236 */, 36 /* 237 */, 36 /* 238 */, 36 /* 239 */,
    36 /* 240 */, 36 /* 241 */, 36 /* 242 */, 36 /* 243 */,
    36 /* 244 */, 36 /* 245 */, 36 /* 246 */, 36 /* 247 */,
    36 /* 248 */, 36 /* 249 */, 36 /* 250 */, 36 /* 251 */,
    36 /* 252 */, 36 /* 253 */, 36 /* 254 */, 36 /* 255 */
};

uint64_t getAlphaNumRank(const string &str, size_t significand) {
  uint64_t r = 0;
  size_t i = 0;
  size_t strSize = (str.size() > significand) ? significand : str.size();

  for (; i < strSize; i++) {
    r = r * kAlphaNumRankBase + kAlphaNumRankTable[(uint8_t)str[i]];
  }

  // Make different length strings have the same level of ranks
  for (; i < significand; i++) {
    r *= kAlphaNumRankBase;
  }

  return r;
}

bool isNiceHashAgent(const string &clientAgent) {
  if (clientAgent.length() < 9) {
    return false;
  }
  string agent = clientAgent;
  // tolower
  std::transform(agent.begin(), agent.end(), agent.begin(), ::tolower);
  if (agent.substr(0, 9) == "nicehash/") {
    return true;
  }
  return false;
}

IdGenerator::IdGenerator(uint8_t serverId)
  : lastTimestamp_{0}
  , lastIdLow_{serverId} {
}

uint64_t IdGenerator::next() {
  // Generate a monotonic job id
  uint32_t currentJobTimestamp = time(nullptr);
  if (currentJobTimestamp <= lastTimestamp_) {
    lastIdLow_ += 0x00000100;
  } else {
    lastIdLow_ &= 0x000000FF;
    lastTimestamp_ = currentJobTimestamp;
  }
  return (static_cast<uint64_t>(currentJobTimestamp) << 32) | lastIdLow_;
}
