#include "GpuTs.h"
#include "BytomPoW.h"
#include "sha3.h"
#include <time.h>
#include <cstdio>
#include <algorithm>
#include <iostream>
#include <map>
#include <cstring>

BytomMatList* matList_int8;
BytomMatListGpu* matListGpu_int8;

#define TEST_NUM 5

static inline void extend( uint32_t* exted, uint8_t *g_seed){

  sha3_ctx *ctx = ( sha3_ctx*)calloc( 1, sizeof( *ctx));
  uint8_t seedHash[ 4][ 32];

  std::copy( g_seed, g_seed + 32, seedHash[ 0]);

  for( int i = 0; i < 3; ++i) {

    rhash_sha3_256_init( ctx);
    rhash_sha3_update( ctx, seedHash[ i], 32);
    rhash_sha3_final( ctx, seedHash[ i+1]);
  }

  for( int i = 0; i < 32; ++i) {

    exted[ i] =  ( seedHash[ i/8][ ( i*4+3)%32]<<24) +
      ( seedHash[ i/8][ ( i*4+2)%32]<<16) +
      ( seedHash[ i/8][ ( i*4+1)%32]<<8) +
      seedHash[ i/8][ ( i*4)%32];
  }

  free( ctx);
}

static void init_seed(Words32 &seed, uint32_t _seed[32])
{
  for (int i = 0; i < 16; i++)
    seed.lo.w[i] = _seed[i];
  for (int i = 0; i < 16; i++)
    seed.hi.w[i] = _seed[16 + i];
}


uint8_t result[32] = {0};
uint8_t seedCache[32] = {0};
cublasHandle_t handle;

uint8_t *GpuTs(uint8_t blockheader[32], uint8_t seed[32]){
     if(memcmp(seedCache, seed, 32) != 0){
        uint32_t exted[32];
        extend(exted, seed);
        Words32 extSeed;
        init_seed(extSeed, exted);
        if (matList_int8 != nullptr) {
           delete  matList_int8;
        }
        matList_int8=new BytomMatList;
        initMatVec(matList_int8->matVec, extSeed);
        if (matListGpu_int8 != nullptr) {
                delete matListGpu_int8;
        }
        matListGpu_int8=new BytomMatListGpu;
        initMatVecGpu(matListGpu_int8, matList_int8);
        cublasStatus_t stat = cublasCreate(&handle);
        if (stat != CUBLAS_STATUS_SUCCESS){
          std::cerr<<"Fail to Create CuBlas Handle."<<std::endl;
          exit(EXIT_FAILURE);
        }
        memcpy(seedCache, seed, 32);
    }
    iter_mineBytom(blockheader, 32, result, handle);
    return result;
}

