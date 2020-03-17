#include "CommonTellor.h"
#include "keccak/keccak.h"
#include "hash.h"
#include "sushu.h"

uint256
tellor::GetTellorPowHash(string challenge, string publicaddress, string nonce) {

  auto delPrefix = [](std::string &hexString) -> string {
    return hexString.find("0x") != string::npos ? hexString.substr(2)
                                                : hexString;
  };
  std::string prefix = delPrefix(challenge) + delPrefix(publicaddress) + nonce;

  std::vector<char> prefixbin;
  Hex2Bin(prefix.c_str(), prefixbin);
  tellor_hash256 hashkeccakbin =
      tellor_keccak256((uint8_t *)prefixbin.data(), prefixbin.size());

  uint256 hashkeccakuint256;
  memcpy(&hashkeccakuint256, hashkeccakbin.bytes, 32);

  auto Cripemd160Hash = [](uint256 &input) -> uint160 {
    uint160 result;
    static const unsigned char pblank[1] = {};
    CRIPEMD160()
        .Write(
            input.IsNull() ? pblank : (const unsigned char *)input.begin(),
            input.size())
        .Finalize((unsigned char *)&result);
    return result;
  };

  uint160 cripemd160hash = Cripemd160Hash(hashkeccakuint256);

  auto Sha256Hash = [](uint160 &input) -> uint256 {
    static const unsigned char pblank[1] = {};
    uint256 result;
    CSHA256()
        .Write(
            input.IsNull() ? pblank : (const unsigned char *)input.begin(),
            input.size())
        .Finalize((unsigned char *)&result);
    std::reverse(result.begin(), result.end());
    return result;
  };

  return Sha256Hash(cripemd160hash);
}

static uint64_t GetPrimeFactors(const uint64_t input, uint64_t begin) {
  uint64_t n = input, resultSmall = 1;
  for (auto factor : sushu) {
    while (n % factor == 0) {
      n = n / factor;
      if (factor > begin) // if current factor > min-rate return input/factor
        return input / factor;
      resultSmall *= factor;
      if (resultSmall >= begin)
        return n > resultSmall ? n : resultSmall;
    }
  }
  return input / resultSmall;
}

uint64_t tellor::calcCurDiff(uint64_t networkdiff, uint64_t min) {

  uint64_t currdiff = GetPrimeFactors(networkdiff, min);
  DLOG(INFO) << "net work diff : " << networkdiff << " mindiff : " << min
             << " calc diff :" << currdiff;
  return currdiff;
}