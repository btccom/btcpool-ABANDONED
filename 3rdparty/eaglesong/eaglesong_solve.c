#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "eaglesong_solve.h"
#include "portable_endian.h"

#define INPUT_LEN (32)
#define ROUND (43)
#define RATE (256)
#define M (INPUT_LEN >> 2)
#define LEN (RATE >> 3)
#define DELIMITER (0x06)
#define OUTPUT_LENGTH (256 >> 3)
#define N 100000

#define ROL32(a,b) (((a)<<(b))|((a)>>(32-(b))))
#define ROL_ADD(a,b) a += b; a = ROL32(a, 8); b = a + ROL32(b, 24);
#define ROL_XOR(t, a, b, k) t^ROL32(t, a)^ROL32(t, b)^injection_constants[k]

uint32_t injection_constants[] = INJECT_MAT;

#define EaglesongPermutation() { \
    for(int i = 0, k=0; i < ROUND ; ++i ) { \
        tmp = s0^s4^s12^s15; s0 = tmp^s5^s6^s7; s1 = tmp^s1^s8^s13; \
        tmp = s1^s2^s6^s14; s2 = tmp^s7^s8^s9; s3 = tmp^s3^s10^s15; \
        tmp = s0^s3^s4^s8; s4 = tmp^s9^s10^s11; s5 = tmp^s1^s5^s12; \
        tmp = s2^s5^s6^s10; s6 = tmp^s11^s12^s13; s7 = tmp^s3^s7^s14; \
        tmp = s4^s7^s8^s12; s8 = tmp^s13^s14^s15; s9 = tmp^s0^s5^s9; \
        tmp = s6^s9^s10^s14; s10 = tmp^s0^s1^s15; s11 = tmp^s2^s7^s11; \
        tmp = s0^s8^s11^s12; s12 = tmp^s1^s2^s3; s13 = tmp^s4^s9^s13; \
        tmp = s3^s5^s13^s14; s14 = tmp^s2^s4^s10; s15 = tmp^s0^s1^s6^s7^s8^s9^s15; \
        s0 = ROL_XOR(s0, 2, 4, k); ++k; s1 = ROL_XOR(s1, 13, 22, k); ++k; ROL_ADD(s0, s1); \
        s2 = ROL_XOR(s2, 4, 19, k); ++k; s3 = ROL_XOR(s3, 3, 14, k); ++k; ROL_ADD(s2, s3); \
        s4 = ROL_XOR(s4, 27, 31, k); ++k; s5 = ROL_XOR(s5, 3, 8, k); ++k; ROL_ADD(s4, s5); \
        s6 = ROL_XOR(s6, 17, 26, k); ++k; s7 = ROL_XOR(s7, 3, 12, k); ++k; ROL_ADD(s6, s7); \
        s8 = ROL_XOR(s8, 18, 22, k); ++k; s9 = ROL_XOR(s9, 12, 18, k); ++k; ROL_ADD(s8, s9); \
        s10 = ROL_XOR(s10, 4, 7, k); ++k; s11 = ROL_XOR(s11, 4, 31, k); ++k; ROL_ADD(s10, s11); \
        s12 = ROL_XOR(s12, 12, 27, k); ++k; s13 = ROL_XOR(s13, 7, 17, k); ++k; ROL_ADD(s12, s13); \
        s14 = ROL_XOR(s14, 7, 8, k); ++k; s15 = ROL_XOR(s15, 1, 13, k); ++k; ROL_ADD(s14, s15); \
    } \
}

#define squeeze(s, k) {\
    ((uint32_t *)output)[k] = htole32(s); \
}

uint32_t c_solve(uint8_t *input, uint8_t *target, uint64_t *nonce) {
    uint32_t s0,s1,s2,s3,s4,s5,s6,s7,s8,s9,s10,s11,s12,s13,s14,s15;
    uint32_t state[11];
    uint32_t tmp;
    uint8_t output[32];
    RAND_bytes((uint8_t*) &(state[0]), 4);
    RAND_bytes((uint8_t*) &(state[1]), 4);
    
    // absorbing
    for(int j = 0, k=0; j <= M; ++j) {
        uint32_t sum = 0;
        for(int v=0; v < 4; ++v) {
            if(k < INPUT_LEN) {
                sum = (sum << 8) ^ input[k];
            } else if(k == INPUT_LEN) {
                sum = (sum << 8) ^ DELIMITER;
            }
            ++k;
        }
        state[j+2] = sum;
    }

    for(uint32_t i=0; i<N; ++i) {
        s0 = state[0] ^ i;
        s1 = state[1]; s2 = state[2]; s3 = state[3];
        s4 = state[4]; s5 = state[5]; s6 = state[6]; s7 = state[7];
        s8 = s9 = s10 = s11 = s12 = s13 = s14 = s15 = 0;
        
        EaglesongPermutation();
        
        s0 ^= state[8]; s1 ^= state[9]; s2 ^= state[10];
        
        EaglesongPermutation();

        squeeze(s0, 0); squeeze(s1, 1); squeeze(s2, 2); squeeze(s3, 3);
        squeeze(s4, 4); squeeze(s5, 5); squeeze(s6, 6); squeeze(s7, 7);

        for(int k=0; k<32; ++k) {
            if(output[k] < target[k]) {
                *nonce = le32toh(htobe32(state[1]));
                *nonce = (*nonce << 32) ^ le32toh(htobe32((state[0]^i)));
                return i;
            } else if(output[k] > target[k]) {
                break;
            }
        }
    }

    return N;
}