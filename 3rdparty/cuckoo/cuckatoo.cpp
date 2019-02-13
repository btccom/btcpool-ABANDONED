#include "cuckatoo.h"
#include "siphash.h"

// generate edge endpoint in cuck(at)oo graph without partition bit
static uint64_t sip_node(siphash_keys &keys, uint64_t edge, uint64_t uorv, uint64_t edge_mask) {
  return keys.siphash24(2 * edge + uorv) & edge_mask;
}

// verify that edges are ascending and form a cycle in header-generated graph
bool verify_cuckatoo(const std::vector<uint64_t> &edges, siphash_keys &keys, uint32_t edge_bits) {
  uint64_t xor0, xor1;
  size_t proof_size = edges.size();
  std::vector<uint64_t> uvs(2 * proof_size);
  xor0 = xor1 = (proof_size / 2) & 1;
  uint64_t edge_size = static_cast<uint64_t>(1) << edge_bits;
  uint64_t edge_mask = edge_size - 1;

  for (size_t n = 0; n < proof_size; n++) {
    if (edges[n] > edge_mask)
      return false;
    if (n && edges[n] <= edges[n-1])
      return false;
    xor0 ^= uvs[2*n  ] = sip_node(keys, edges[n], 0, edge_mask);
    xor1 ^= uvs[2*n+1] = sip_node(keys, edges[n], 1, edge_mask);
  }
  if (xor0|xor1)              // optional check for obviously bad proofs
    return false;
  size_t n = 0, i = 0, j;
  do {                        // follow cycle
    for (size_t k = j = i; (k = (k + 2) % (2 * proof_size)) != i; ) {
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