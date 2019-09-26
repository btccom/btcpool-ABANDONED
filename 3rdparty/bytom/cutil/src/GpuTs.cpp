#include <iostream>
#include <cstdio>
#include <map>
#include <mutex>
#include "GpuTs.h"
#include "BytomPoW.h"
#include "seed.h"
#include <cuda_runtime.h>
#include "cublas_v2.h"
#include <chrono>

using namespace std;

BytomMatList8* matList_int8;
BytomMatListGpu* matListGpu_int8;
uint8_t result[32] = {0};
map <vector<uint8_t>, BytomMatListGpu*> seedCache;
static const int cacheSize = 42; //"Answer to the Ultimate Question of Life, the Universe, and Everything"
mutex mtx;

uint8_t *GpuTs(uint8_t blockheader[32], uint8_t seed[32]){
    mtx.lock();

    vector<uint8_t> seedVec(seed, seed + 32);

    if(seedCache.find(seedVec) != seedCache.end()) {
        // printf("\t---%s---\n", "Seed already exists in the cache.");
        matListGpu_int8 = seedCache[seedVec];
    } else {
        uint32_t exted[32];
        extend(exted, seed); // extends seed to exted

        Words32 extSeed;
        init_seed(extSeed, exted);

        matList_int8 = new BytomMatList8;
        matList_int8->init(extSeed);

        matListGpu_int8=new BytomMatListGpu;

        initMatVecGpu(matListGpu_int8, matList_int8);

        seedCache.insert(make_pair(seedVec, matListGpu_int8));

        delete matList_int8;
    }
    // auto d6 = s7 - s6;
    // std::cout << "d6 duration: " << chrono::duration_cast<chrono::microseconds>(d6).count() << " micros\n";

    cublasHandle_t handle;
    cublasStatus_t stat = cublasCreate(&handle);
    if (stat != CUBLAS_STATUS_SUCCESS){
        std::cerr<<"Fail to Create CuBlas Handle."<<std::endl;
        exit(EXIT_FAILURE);
    }

    iter_mineBytom(blockheader, 32, result, handle);

    if(seedCache.size() > cacheSize) {
        for(map<vector<uint8_t>, BytomMatListGpu*>::iterator it=seedCache.begin(); it!=seedCache.end(); ++it){
            delete it->second; 
        }
        seedCache.clear();
        cudaDeviceReset();
    }

    mtx.unlock();
    
    return result;
}
