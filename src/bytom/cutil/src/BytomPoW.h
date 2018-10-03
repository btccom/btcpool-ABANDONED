/* BytomPoW.h */
#ifndef BYTOMPOW_H
#define BYTOMPOW_H

#include "scrypt.h"
#include "sha3-allInOne.h"
#include <iostream>
#include <vector>
#include <time.h>
#include <assert.h>
#include <stdint.h>
// #include <x86intrin.h>
// #ifdef _USE_OPENMP
//     #include <omp.h>
// #endif
#include <cuda_runtime.h>
#include "cublas_v2.h"


#define FNV(v1,v2) int32_t( ((v1)*FNV_PRIME) ^ (v2) )
const int FNV_PRIME = 0x01000193;

//  flip the LTCMemory to be used for matrix copy
struct SortedLTCMemory
{
  uint32_t w[32][1024];
};

inline void sortLtcMemoryForMatrixCopy(LTCMemory& ltcMem, SortedLTCMemory& sortedLtcMem)
{
    // Words32 temp[1024];
    // for(int i = 0; i < 256; ++i)
    // {
    //     memcpy(&temp[i]         , &ltcMem.w32[i * 4 + 0], sizeof(Words32));
    //     memcpy(&temp[i + 256]   , &ltcMem.w32[i * 4 + 2], sizeof(Words32));
    //     memcpy(&temp[i + 512]   , &ltcMem.w32[i * 4 + 1], sizeof(Words32));
    //     memcpy(&temp[i + 768]   , &ltcMem.w32[i * 4 + 3], sizeof(Words32));
    // }

    // for(int i = 0; i < 32; ++i)
    // {
    //     for(int j = 0; j < 256; ++j)
    //     {
    //         const Words32& evenlow  = temp[j];
    //         const Words32& evenhigh = temp[j + 256];
    //         const Words32& oddlow   = temp[j + 512];
    //         const Words32& oddhigh  = temp[j + 768];

    //         sortedLtcMem.w[i][j]        = evenlow.get(i);
    //         sortedLtcMem.w[i][j + 256]  = evenhigh.get(i);
    //         sortedLtcMem.w[i][j + 512]  = oddlow.get(i);
    //         sortedLtcMem.w[i][j + 768]  = oddhigh.get(i);
    //     }
    // }

    for(int i = 0; i < 32; ++i)
    {
        for(int j = 0; j < 256; ++j)
        {
            int s = j * 4;
            const Words32& evenlow  = ltcMem.w32[s + 0];
            const Words32& oddlow   = ltcMem.w32[s + 1];
            const Words32& evenhigh = ltcMem.w32[s + 2];
            const Words32& oddhigh  = ltcMem.w32[s + 3];

            sortedLtcMem.w[i][j]        = evenlow.get(i);
            sortedLtcMem.w[i][j + 256]  = evenhigh.get(i);
            sortedLtcMem.w[i][j + 512]  = oddlow.get(i);
            sortedLtcMem.w[i][j + 768]  = oddhigh.get(i);
        }
    }

}

struct Mat256x256i8 {
    int8_t d[256][256];

    void toIdentityMatrix() {
        for(int i=0; i<256; i++) {
            for(int j=0; j<256; j++) {
                d[i][j]= (i==j)?1:0; // diagonal
            }
        }
    }

    void copyFrom(const Mat256x256i8& other) {
        for(int i=0; i<256; i++) {
            for(int j=0; j<256; j++) {
                this->d[j][i]=other.d[j][i];
            }
        }
    }

    Mat256x256i8() {
//        this->toIdentityMatrix();
    }

    Mat256x256i8(const Mat256x256i8& other) {
        this->copyFrom(other);
    }

    void copyFrom_helper(LTCMemory& ltcMem, int offset) {
        for(int j=0; j<32; j++) {
            int dim = j * 4;
            for(int i=0; i<256; i++) {
                const Words32& lo=ltcMem.get(i*4+offset);
                uint32_t i32 = lo.get(j);
                d[dim+0][i]=(i32>>0)&0xFF;
                d[dim+1][i]=(i32>>8)&0xFF;
                d[dim+2][i]=(i32>>16)&0xFF;
                d[dim+3][i]=(i32>>24)&0xFF;
            }
        }
        for(int j=0; j<32; j++) {
            int dim = (j + 32) * 4;
            for(int i=0; i<256; i++) {
                const Words32& hi=ltcMem.get(i*4+2+offset);
                uint32_t i32 = hi.get(j);
                d[dim+0][i]=(i32>>0)&0xFF;
                d[dim+1][i]=(i32>>8)&0xFF;
                d[dim+2][i]=(i32>>16)&0xFF;
                d[dim+3][i]=(i32>>24)&0xFF;
            }
        }
    }


    void copyFrom_helper(SortedLTCMemory& ltcMem, bool odd)
    {
        int starting = (int)odd * 512;
        for(int i = 0; i < 32; ++i)
        {
            int dim = i * 4;
            for(int j = 0; j < 256; ++j)
            {
                int dim2 = j + starting;
                uint32_t i32 = ltcMem.w[i][dim2];
                d[dim+0][j]=(i32>>0)&0xFF;
                d[dim+1][j]=(i32>>8)&0xFF;
                d[dim+2][j]=(i32>>16)&0xFF;
                d[dim+3][j]=(i32>>24)&0xFF;                
            }
            dim = (i + 32) * 4;
            for(int j = 0; j < 256; ++j)
            {
                int dim2 = j + starting + 256;
                uint32_t i32 = ltcMem.w[i][dim2];
                d[dim+0][j]=(i32>>0)&0xFF;
                d[dim+1][j]=(i32>>8)&0xFF;
                d[dim+2][j]=(i32>>16)&0xFF;
                d[dim+3][j]=(i32>>24)&0xFF;                
            }
        }
    }
    

    void copyFromEven(LTCMemory& ltcMem) {
        copyFrom_helper(ltcMem, 0);
    }

    void copyFromOdd(LTCMemory& ltcMem) {
        copyFrom_helper(ltcMem, 1);
    }

    void copyFromSortedEven(SortedLTCMemory& ltcMem) {
        copyFrom_helper(ltcMem, false);
    }

    void copyFromSortedOdd(SortedLTCMemory& ltcMem) {
        copyFrom_helper(ltcMem, true);
    }


    void add(Mat256x256i8& a, Mat256x256i8& b) {
        for(int i=0; i<256; i++) {
            for(int j=0; j<256; j++) {
                int tmp=int(a.d[i][j])+int(b.d[i][j]);
                this->d[i][j]=(tmp&0xFF);
            }
        }
    }
};

struct Arr256x64i32 {
    uint32_t d[256][64];

    void fillWithD0(uint32_t data[64]) {
        for(int i=0; i<64; i++) data[i]=d[0][i];
    }

    uint8_t* d0RawPtr() {
        return (uint8_t*)(d[0]);
    }

    Arr256x64i32(const Mat256x256i8& mat) {
        for(int j=0; j<256; j++) {
            for(int i=0; i<64; i++) {
                d[j][i] = ((uint32_t(uint8_t(mat.d[j][i  + 192]))) << 24) |
                          ((uint32_t(uint8_t(mat.d[j][i + 128]))) << 16) |
                          ((uint32_t(uint8_t(mat.d[j][i  + 64]))) << 8) |
                          ((uint32_t(uint8_t(mat.d[j][i ]))) << 0);
            }
        }
    }

    void reduceFNV() {
        for(int k=256; k>1; k=k/2) {
            for(int j=0; j<k/2; j++) {
                for(int i=0; i<64; i++) {
                    d[j][i] = FNV(d[j][i], d[j + k / 2][i]);
                }
            }
        }
    }
};

struct BytomMatList8 {
    Mat256x256i8* matVec;

    Mat256x256i8 at(int i) {
        return matVec[i];
    }

    BytomMatList8() {
        matVec = new Mat256x256i8[256];
    }

    ~BytomMatList8() {
        delete[] matVec;
    }

    void init(const Words32& X_in) {
        Words32 X = X_in;
        LTCMemory ltcMem;
        for(int i=0; i<128; i++) {
            ltcMem.scrypt(X);
            // matVec[2*i].copyFromEven(ltcMem);
            // matVec[2*i+1].copyFromOdd(ltcMem);
            SortedLTCMemory sortedLtcMem;
            sortLtcMemoryForMatrixCopy(ltcMem, sortedLtcMem);
            matVec[2*i].copyFromSortedEven(sortedLtcMem);
            matVec[2*i+1].copyFromSortedOdd(sortedLtcMem);
        }
    }
};

struct BytomMatListGpu {
  int8_t* matVecGpu;
  int8_t* at(int i) {
    return &matVecGpu[i * 256 * 256];
  }
  BytomMatListGpu() {
      int8_t* devPtr_i8;
      cudaMalloc ((void**)&devPtr_i8, 256*256*256*sizeof(*devPtr_i8));
      matVecGpu = devPtr_i8;
  }
  ~BytomMatListGpu() {
      cudaFree(matVecGpu);
  }
};


// struct BytomMatList8 {
//     std::vector<Mat256x256i8*> matVec;

//     Mat256x256i8 at(int i) {
//         return *(matVec[i]);
//     }

//     BytomMatList8() {
//         for(int i=0; i<256; i++) {
//             Mat256x256i8* ptr = new Mat256x256i8;
//             assert(ptr!=NULL);
//             matVec.push_back(ptr);
//         }
//     }

//     ~BytomMatList8() {
//         for(int i=0; i<256; i++) {
//             delete matVec[i];
//         }
//     }

//     void init(const Words32& X_in) {
//         Words32 X = X_in;
//         LTCMemory ltcMem;
//         for(int i=0; i<128; i++) {
//             ltcMem.scrypt(X);
//             matVec[2*i]->copyFromEven(ltcMem);
//             matVec[2*i+1]->copyFromOdd(ltcMem);
//         }
//     }
// };


// struct BytomMatListGpu {
//   std::vector<int8_t*> matVecGpu;
//   int8_t* at(int i) {
//     return matVecGpu[i];
//   }
//   BytomMatListGpu() {
//     for(int i=0; i<256; i++) {
//       int8_t* devPtr_i8;
//       cudaMalloc ((void**)&devPtr_i8, 256*256*sizeof(*devPtr_i8));
//       assert(devPtr_i8!=NULL);
//       matVecGpu.push_back(devPtr_i8);
//     }
//   }
//   ~BytomMatListGpu() {
//     for(int i=0; i<256; i++)
//       cudaFree(matVecGpu[i]);
//   }
// };


/*
struct BytomMatList16 {
    std::vector<Mat256x256i16*> matVec;

    Mat256x256i16 at(int i) {
        return *(matVec[i]);
    }

    BytomMatList16() {
        for(int i=0; i<256; i++) {
            Mat256x256i16* ptr=new Mat256x256i16;
            assert(ptr!=NULL);
            matVec.push_back(ptr);
        }
    }

    ~BytomMatList16() {
        for(int i=0; i<256; i++)
            delete matVec[i];
    }

    void init(const Words32& X_in) {
        Words32 X = X_in;
        LTCMemory ltcMem;
        for(int i=0; i<128; i++) {
            ltcMem.scrypt(X);
            matVec[2*i]->copyFromEven(ltcMem);
            matVec[2*i+1]->copyFromOdd(ltcMem);
        }
    }

    // void copyFrom(BytomMatList8& other) {
    //     for(int i=0; i<256; i++) {
    //         matVec[i]->copyFrom(*other.matVec[i]);
    //     }
    // }

    // void copyFrom(BytomMatList16& other) {
    //     for(int i=0; i<256; i++) {
    //         matVec[i]->copyFrom(*other.matVec[i]);
    //     }
    // }
};
*/

extern BytomMatList8* matList_int8;
extern BytomMatListGpu* matListGpu_int8;

// extern BytomMatList16* matList_int16;


extern void initMatVecGpu(BytomMatListGpu* matListGpu_int8, BytomMatList8* matList_int8);
extern void core_mineBytom_gpu(std::vector<uint8_t> fourSeq[4], BytomMatListGpu* matListGpu_int8, uint32_t data[64], cublasHandle_t handle);


inline void iter_mineBytom(
                const uint8_t *fixedMessage,
                uint32_t len,
                // uint8_t nonce[8],
                uint8_t result[32],
                cublasHandle_t handle)
{
    uint8_t sequence[32];
    sha3_ctx ctx;

    std::vector<uint8_t> fourSeq[4];
    for(int k=0; k<4; k++) { // The k-loop
        rhash_sha3_256_init(&ctx);
        rhash_sha3_update(&ctx, fixedMessage+(len*k/4),len/4);//分四轮消耗掉fixedMessage
        rhash_sha3_final(&ctx, sequence);
        for(int i=0; i<32; i++){
            fourSeq[k].push_back(sequence[i]);
        }
    }
    uint32_t data[64];
   
    core_mineBytom_gpu(fourSeq, matListGpu_int8, data, handle);

    rhash_sha3_256_init(&ctx);
    rhash_sha3_update(&ctx, (uint8_t*)data, 256);
    rhash_sha3_final(&ctx, result);
}



/*
inline void iter_mineBytom(
                        const uint8_t *fixedMessage,
                        uint32_t len,
                        // uint8_t nonce[8],
                        uint8_t result[32]) {
    Mat256x256i8 *resArr8=new Mat256x256i8[4];

    clock_t start, end;
    start = clock();
    // Itz faster using single thread ...
#ifdef _USE_OPENMP
#pragma omp parallel for simd
#endif
    for(int k=0; k<4; k++) { // The k-loop
        sha3_ctx *ctx = new sha3_ctx;
        Mat256x256i16 *mat16=new Mat256x256i16;
        Mat256x256i16 *tmp16=new Mat256x256i16;
        uint8_t sequence[32];
        rhash_sha3_256_init(ctx);
        rhash_sha3_update(ctx, fixedMessage+(len*k/4), len/4);//分四轮消耗掉fixedMessage
        rhash_sha3_final(ctx, sequence);
        tmp16->toIdentityMatrix();

        for(int j=0; j<2; j++) {
            // equivalent as tmp=tmp*matlist, i+=1 
            for(int i=0; i<32; i+=2) {
                // "mc = ma dot mb.T" in GoLang code
                mat16->mul(*tmp16, matList_int16->at(sequence[i]));
                // "ma = mc" in GoLang code
                tmp16->mul(*mat16, matList_int16->at(sequence[i+1]));
            }
        }
        // "res[k] = mc" in GoLang code
        tmp16->toMatI8(resArr8[k]); // 0.00018s
        delete mat16;
        delete tmp16;
        delete ctx;
    }

    // 3.7e-05s
    Mat256x256i8 *res8=new Mat256x256i8;
    res8->add(resArr8[0], resArr8[1]);
    res8->add(*res8, resArr8[2]);
    res8->add(*res8, resArr8[3]);

    end = clock();    
    // std::cout << "\tTime for getting MulMatix: "
    //           << (double)(end - start) / CLOCKS_PER_SEC * 1000 << "ms"
    //           << std::endl;

    Arr256x64i32 arr(*res8);
    arr.reduceFNV();
    sha3_ctx *ctx = new sha3_ctx;
    rhash_sha3_256_init(ctx);
    rhash_sha3_update(ctx, arr.d0RawPtr(), 256);
    rhash_sha3_final(ctx, result);

    delete res8;
    delete[] resArr8;
    delete ctx;
}
*/

inline void incrNonce(uint8_t nonce[8]) {
    for(int i=0; i<8; i++) {
        if(nonce[i]!=255) {
            nonce[i]++;
            break;
        } else {
            nonce[i]=0;
        }
    }
}

inline int countLeadingZero(uint8_t result[32]) {
    int count=0;
    for (int i=31; i>=0; i--) { // NOTE: reverse
        if (result[i] < 1) {
            count+=8;
        } else if (result[i]<2)  {
            count+=7;
            break;
        } else if (result[i]<4)  {
            count+=6;
            break;
        } else if (result[i]<8)  {
            count+=5;
            break;
        } else if (result[i]<16) {
            count+=4;
            break;
        } else if (result[i]<32) {
            count+=3;
            break;
        } else if (result[i]<64) {
            count+=2;
            break;
        } else if (result[i]<128) {
            count+=1;
            break;
        }
    }
    return count;
}

// inline int test_mineBytom(
//     const uint8_t *fixedMessage,
//     uint32_t len,
//     uint8_t nonce[32],
//     int count,
//     int leadingZeroThres)
// {
//   assert(len%4==0);
//   int step;
//   for(step=0; step<count; step++) {
//     uint8_t result[32];
//     //std::cerr<<"Mine step "<<step<<std::endl;
//     iter_mineBytom(fixedMessage,100,nonce,result);
//     std::cerr<<"Mine step "<<step<<std::endl;
//     for (int i = 0; i < 32; i++) {
//       printf("%02x ", result[i]);
//       if (i % 8 == 7)
//         printf("\n");
//     }
//     if (countLeadingZero(result) > leadingZeroThres)
//       return step;
//     incrNonce(nonce);
//   }
//   return step;
// }


#endif

