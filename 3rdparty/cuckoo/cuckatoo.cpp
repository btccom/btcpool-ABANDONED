#include "cuckatoo.h"
#include "siphash.h"
#include "graph.hpp"

#include <thread>
#include <mutex>
#include <glog/logging.h>
// generate edge endpoint in cuck(at)oo graph without partition bit
static uint64_t sip_node(siphash_keys &keys, uint64_t edge, uint64_t uorv, uint64_t edge_mask) {
  return keys.siphash24(2 * edge + uorv) & edge_mask;
}

// verify that edges are ascending and form a cycle in header-generated graph
bool verify_cuckatoo(const std::vector<uint64_t> &edges, siphash_keys &keys, uint32_t edge_bits) {
  uint64_t xor0, xor1;
  uint64_t proof_size = edges.size();
  std::vector<uint64_t> uvs(2 * proof_size);
  xor0 = xor1 = (proof_size / 2) & 1;
  uint64_t edge_size = static_cast<uint64_t>(1) << edge_bits;
  uint64_t edge_mask = edge_size - 1;

  for (uint64_t n = 0; n < proof_size; n++) {
    if (edges[n] > edge_mask)
      return false;
    if (n && edges[n] <= edges[n-1])
      return false;
    xor0 ^= uvs[2*n  ] = sip_node(keys, edges[n], 0, edge_mask);
    xor1 ^= uvs[2*n+1] = sip_node(keys, edges[n], 1, edge_mask);
  }
  if (xor0|xor1)              // optional check for obviously bad proofs
    return false;
  uint64_t n = 0, i = 0, j;
  do {                        // follow cycle
    for (uint64_t k = j = i; (k = (k + 2) % (2 * proof_size)) != i; ) {
      if (uvs[k]>>1 == uvs[i]>>1) { // find other edge endpoint matching one at i
        if (j != i)           // already found one before
          return false;
        j = k;
      }
    }
    if (j == i || uvs[j] == uvs[i])
      return false;  // no matching endpoint
    i = j^1;
    n++;
  } while (i != 0);           // must cycle back to start or we would have found branch
  return n == proof_size;
}


static uint32_t sip_node_ae(siphash_keys &keys, uint32_t edge, uint32_t uorv, uint32_t edge_mask) {
  uint32_t siphash = keys.siphash24ae(2 * edge + uorv) & edge_mask;
  return (siphash << 1) | uorv;
}


bool verify_cuckatoo_ae(const std::vector<uint32_t> &edges, siphash_keys &keys, uint32_t edge_bits) {
  uint32_t xor0, xor1;
  uint32_t proof_size = edges.size();
  std::vector<uint32_t> uvs(2 * proof_size);
  xor0 = xor1 = 0; //(proof_size / 2) & 1;
  uint32_t edge_size = static_cast<uint32_t>(1) << edge_bits;
  uint32_t edge_mask = edge_size - 1;

  for (uint32_t n = 0; n < proof_size; n++) {
    if (edges[n] > edge_mask)
      return false;
    if (n && edges[n] <= edges[n-1])
      return false;
    xor0 ^= uvs[2*(proof_size-n-1)  ] = sip_node_ae(keys, edges[n], 0, edge_mask);
    xor1 ^= uvs[2*(proof_size-n-1)+1] = sip_node_ae(keys, edges[n], 1, edge_mask);
  }
  if (xor0|xor1)              // optional check for obviously bad proofs
    return false;
  uint32_t n = 0, i = 0, j;
  do {                        // follow cycle
    for (uint32_t k = j = i; (k = (k + 2) % (2 * proof_size)) != i; ) {
      if (uvs[k]>>1 == uvs[i]>>1) { // find other edge endpoint matching one at i
        if (j != i)           // already found one before
          return false;
        j = k;
      }
    }
    if (j == i)
      return false;  // no matching endpoint
    i = j^1;
    n++;
  } while (i != 0);           // must cycle back to start or we would have found branch
  return n == proof_size;
}

bool find_pow_ae(std::vector<uint32_t> &pow/*[output]*/, siphash_keys &sip_keys/*[input]*/, uint32_t easiness/*[input]*/) {
  //uint32_t edge_bits = 29;//in ae ths EDGEBITS is 29
  static std::mutex lock_for_find_pow;
  std::lock_guard<std::mutex> lock(lock_for_find_pow);
  uint32_t edge_size = static_cast<uint32_t>(1) << 29;
  uint32_t edge_mask = edge_size - 1;
  static graph<uint32_t> cg(edge_size, edge_size, 4);
  cg.reset();
  for (uint32_t nonce = 0; nonce < easiness; nonce++) {
    uint32_t u = sip_node_ae(sip_keys, nonce, 0, edge_mask);
    uint32_t v = sip_node_ae(sip_keys, nonce, 1, edge_mask);
    cg.add_edge(u, v);
  }
  pow.resize(42);
  for (uint32_t s=0; s < cg.nsols; s++) {
      for(u32 j=0; j < 42; j++) {
        pow[j] = (cg.sols[s][j]);
      }
    if (verify_cuckatoo_ae(pow, sip_keys, 29)) {
      return true;
    } 
  }
  return false;
}
