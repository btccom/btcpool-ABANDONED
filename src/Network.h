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
#ifndef BPOOL_NETWORK_H_
#define BPOOL_NETWORK_H_

#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <ifaddrs.h>
#include <string>
#include <vector>
#include <map>

///////////////////////////////////// IPv4/IPv6 compatible address structure
///////////////////////////////////////
union IpAddress {
  // all datas are big endian
  uint8_t addrUint8[16];
  uint16_t addrUint16[8];
  uint32_t addrUint32[4];
  uint64_t addrUint64[2];
  // use addrIpv4[3] to store the IPv4 addr
  struct in_addr addrIpv4[4];
  struct in6_addr addrIpv6;

  // memory mapping:
  // addrUint8  | 0| 1| 2| 3| 4| 5| 6| 7| 8| 9|10|11|12|13|14|15|
  // addrUint16 |  0  |  1  |  2  |  3  |  4  |  5  |  6  |  7  |
  // addrUint32 |     0     |     1     |     2     |     3     |
  // addrUint64 |           0           |           1           |
  // addrIpv4   | don't use | don't use | don't use |     3     |
  // addrIpv6   |                      all                      |

  IpAddress() {
    addrUint64[0] = 0;
    addrUint64[1] = 0;
  }

  // just for init
  IpAddress(uint64_t initNum) {
    addrUint64[0] = initNum;
    addrUint64[1] = initNum;
  }

  bool fromString(const std::string &ipStr) {
    if (isIpv4(ipStr)) {
      addrUint32[0] = 0;
      addrUint32[1] = 0;
      addrUint32[2] = 0;
      return inet_pton(AF_INET, ipStr.c_str(), (void *)&addrIpv4[3]);
    } else {
      return inet_pton(AF_INET, ipStr.c_str(), (void *)&addrIpv6);
    }
  }

  std::string toString() const {
    const char *pStr;

    if (isIpv4()) {
      char str[INET_ADDRSTRLEN];
      pStr = inet_ntop(AF_INET, (void *)&(addrIpv4[3]), str, sizeof(str));
    } else {
      char str[INET6_ADDRSTRLEN];
      pStr = inet_ntop(AF_INET6, &addrIpv6, str, sizeof(str));
    }

    return std::string(pStr);
  }

  void fromInAddr(const struct in_addr &inAddr) {
    addrUint32[0] = 0;
    addrUint32[1] = 0;
    addrUint32[2] = 0;
    addrIpv4[3] = inAddr;
  }

  void fromInAddr(const struct in6_addr &inAddr) { addrIpv6 = inAddr; }

  void fromIpv4Int(const uint32_t ipv4Int) {
    addrUint32[0] = 0;
    addrUint32[1] = 0;
    addrUint32[2] = 0;
    addrUint32[3] = ipv4Int;
  }

  uint32_t toIpv4Int() const { return addrUint32[3]; }

  bool isIpv4() const {
    if (addrUint32[0] == 0 && addrUint32[1] == 0) {
      // IPv4 compatible address
      // ::w.x.y.z
      if (addrUint32[2] == 0) {
        return true;
      }
      // IPv4 mapping address
      // ::ffff:w.x.y.z
      if (addrUint16[4] == 0 && addrUint16[5] == 0xffff) {
        return true;
      }
    }
    return false;
  }

  static bool isIpv4(const std::string &ipStr) {
    if (ipStr.find(':') == ipStr.npos) {
      return true;
    }
    return false;
  }

  static void getIpPortFromStruct(
      const struct sockaddr *sa, std::string &ip, uint16_t &port) {
    switch (sa->sa_family) {
    case AF_INET:
      ip.resize(INET_ADDRSTRLEN);
      inet_ntop(
          AF_INET,
          &(((struct sockaddr_in *)sa)->sin_addr),
          (char *)ip.data(),
          ip.size());
      port = ((struct sockaddr_in *)sa)->sin_port;
      break;

    case AF_INET6:
      ip.resize(INET6_ADDRSTRLEN);
      inet_ntop(
          AF_INET6,
          &(((struct sockaddr_in6 *)sa)->sin6_addr),
          (char *)ip.data(),
          ip.size());
      port = ((struct sockaddr_in6 *)sa)->sin6_port;
      break;
    }

    size_t pos = ip.find('\0');
    if (pos != ip.npos) {
      ip.resize(pos);
    }
  }

  static std::
      map<std::string /*interface name*/, std::vector<std::string> /*ip list*/>
      getInterfaceAddrs() {
    std::map<
        std::string /*interface name*/,
        std::vector<std::string> /*ip list*/>
        result;

    struct ifaddrs *ifList;
    if (getifaddrs(&ifList) < 0) {
      return result;
    }

    std::string ip;
    uint16_t port;
    for (struct ifaddrs *ifa = ifList; ifa != nullptr; ifa = ifa->ifa_next) {
      if (ifa->ifa_addr != nullptr) {
        getIpPortFromStruct(ifa->ifa_addr, ip, port);
        if (!ip.empty() && ip != "127.0.0.1" && ip != "::1") {
          result[ifa->ifa_name].push_back(ip);
        }
      }
    }

    return result;
  }

  static std::string getHostName() {
    std::string hostname(256, '\0');
    gethostname((char *)hostname.data(), hostname.size());
    hostname.resize(strlen(hostname.c_str())); // remove trailing '\0'
    return hostname;
  }
};

// IpAddress should be 16 bytes
static_assert(
    sizeof(IpAddress) == 16, "union IpAddress should not large than 16 bytes");

#endif // BPOOL_NETWORK_H_
