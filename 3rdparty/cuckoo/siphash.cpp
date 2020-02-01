#include "siphash.h"

// set siphash keys from 32 byte char array
void siphash_keys::setkeys(const char *keybuf) {
  k0 = htole64(((uint64_t *)keybuf)[0]);
  k1 = htole64(((uint64_t *)keybuf)[1]);
  k2 = htole64(((uint64_t *)keybuf)[2]);
  k3 = htole64(((uint64_t *)keybuf)[3]);
}

uint64_t siphash_keys::siphash24(const uint64_t nonce) const {
  siphash_state<> v(*this);
  v.hash24(nonce);
  return v.xor_lanes();
}

// uint64_t siphash_keys::siphash24ae(const uint64_t nonce) const {
//   siphash_state v(*this);
//   v.hash24(nonce);
//   return v.rotl(v.xor_lanes(), 17);
// }

#define ROTL(x,b) (uint64_t)( ((x) << (b)) | ( (x) >> (64 - (b))) )
#define SIPROUND \
  do { \
    v0 += v1; v2 += v3; v1 = ROTL(v1,13); \
    v3 = ROTL(v3,16); v1 ^= v0; v3 ^= v2; \
    v0 = ROTL(v0,32); v2 += v1; v0 += v3; \
    v1 = ROTL(v1,17);   v3 = ROTL(v3,21); \
    v1 ^= v2; v3 ^= v0; v2 = ROTL(v2,32); \
  } while(0)

// SipHash-2-4 without standard IV xor and specialized to precomputed key and 8 byte nonces
uint64_t siphash_keys::siphash24ae(const uint64_t nonce) const{
  uint64_t v0 = k0, v1 = k1, v2 = k2, v3 = k3 ^ nonce;
  SIPROUND; SIPROUND;
  v0 ^= nonce;
  v2 ^= 0xff;
  SIPROUND; SIPROUND; SIPROUND; SIPROUND;
  return ROTL(((v0 ^ v1) ^ (v2  ^ v3)), 17);
}