#include "cuckarooz.h"
#include "siphash.h"
#include <iostream>
using namespace std;

static const uint64_t EDGE_BLOCK_BITS = 6;
static const uint64_t EDGE_BLOCK_SIZE = 1 << EDGE_BLOCK_BITS;
static const uint64_t EDGE_BLOCK_MASK = EDGE_BLOCK_SIZE - 1;

// fills buffer with EDGE_BLOCK_SIZE siphash outputs for block containing edge in cuckaroo graph
// return siphash output for given edge

static uint64_t SipHashBlock(siphash_keys &keys, uint64_t edge, bool xorAll) {
  uint64_t nonce0 = ~EDGE_BLOCK_MASK & edge;
	uint64_t nonceI = edge & EDGE_BLOCK_MASK;

  siphash_state<> shs(keys);
  std::vector<uint64_t> nonceHash(EDGE_BLOCK_SIZE, 0);

  for (uint64_t i = 0; i < EDGE_BLOCK_SIZE; i++) {
    shs.hash24(nonce0 + i);
    nonceHash[i] = shs.xor_lanes();
  }

  uint64_t xOr = nonceHash[nonceI];
  uint64_t xOrFrom = 0;

  if(xorAll || nonceI == EDGE_BLOCK_MASK) {
    xOrFrom = nonceI + 1;
  } else {
    xOrFrom = EDGE_BLOCK_MASK;
  }

  for(uint64_t i = xOrFrom; i < EDGE_BLOCK_SIZE; i ++) {
    xOr ^=  nonceHash[i];
  }
  return xOr;
}


// verify that edges are ascending and form a cycle in header-generated graph
bool verify_cuckarooz(const std::vector<uint64_t> &edges, siphash_keys &keys, uint32_t edge_bits) {
  // uint64_t sips[EDGE_BLOCK_SIZE];
  uint64_t proof_size = edges.size();
  std::vector<uint64_t> uvs(2 * proof_size);
  uint64_t edge_size = static_cast<uint64_t>(1) << edge_bits;
  uint64_t edge_mask = edge_size - 1;
  uint64_t node_mask = edge_mask * 2;
  uint64_t xoruv = 0;

  for (uint64_t n = 0; n < proof_size; n++) {
    if (edges[n] > edge_mask)
      return false;
    if (n > 0&& edges[n] <= edges[n-1])
      return false;

    uint64_t edge = SipHashBlock(keys, edges[n], true);
    uvs[2*n  ] = edge & node_mask;
    uvs[2*n+1] = (edge >> 32) & node_mask;
    xoruv ^= uvs[2*n] ^ uvs[2*n+1];
  }
  if (xoruv != 0)
    return false;
  uint64_t n = 0, i = 0, j = 0;
  do {
    uint64_t k = j = i;
    for (;;) {
      k = (k + 1) % (2 * proof_size);
      if(k == i) break;

      if(uvs[k] == uvs[i]) {
        if(j != i) return false;
        j = k;
      }
    }

    if (j == i) return false;  // no matching endpoint
    i = j^1;
    n++;
  } while (i != 0);           // must cycle back to start or we would have found branch
  return n == proof_size;
}