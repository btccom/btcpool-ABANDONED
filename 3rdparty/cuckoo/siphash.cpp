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