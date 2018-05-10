#ifndef BYTOMPOW_H
#define BYTOMPOW_H
#include "scrypt.h"
#include "sha3.h"
#include <iostream>
#include <assert.h>
#include <vector>
#include <cuda_runtime.h>
#include "cublas_v2.h"




const int FNV_PRIME = 0x01000193;
#define FNV(v1,v2) int32_t( ((v1)*FNV_PRIME) ^ (v2) )

struct Mat256x256i8 {
  int8_t d[256][256];
  void toIdentityMatrix() {
    for(int i=0; i<256; i++) {
      for(int j=0; j<256; j++) {
        d[j][i]=0;
      }
    }
    for(int i=0; i<256; i++) {
      d[i][i]=1;
    }
  }
  void copyFrom(const Mat256x256i8& other) {
    for(int i=0; i<256; i++) {
      for(int j=0; j<256; j++) {
        this->d[j][i]=other.d[j][i];
      }
    }
  }
  void copyFrom(int8_t (&d)[256][256]) {
    for(int i=0; i<256; i++) {
      for(int j=0; j<256; j++) {
        this->d[j][i]=d[j][i];
      }
    }
  }
  Mat256x256i8() {
    this->toIdentityMatrix();
  }

  void mul(const Mat256x256i8& a, const Mat256x256i8& b) {
    for(int i=0; i<256; i++) {
      for(int j=0; j<256; j++) {
        int tmp=0;
        for(int k=0; k<256; k++) {
          tmp+=int(a.d[i][k])*int(b.d[k][j]);
        }
        this->d[i][j]=((tmp&0xFF)+ ((tmp>>8)&0xFF))&0xFF;
      }
    }
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
          d[j][i]=FNV(d[j][i], d[j+k/2][i]);
        }
      }
    }
  }
};

struct BytomMatList {
  std::vector<Mat256x256i8*> matVec;
  Mat256x256i8 at(int i) {
    return *(matVec[i]);
  }
  BytomMatList() {
    for(int i=0; i<256; i++) {
      Mat256x256i8* ptr=new Mat256x256i8;
      assert(ptr!=NULL);
      matVec.push_back(ptr);
    }
  }
  ~BytomMatList() {
    for(int i=0; i<256; i++)
      delete matVec[i];
  }
};

struct BytomMatListGpu {
  std::vector<int8_t*> matVecGpu;
  int8_t* at(int i) {
    return matVecGpu[i];
  }
  BytomMatListGpu() {
    for(int i=0; i<256; i++) {
      int8_t* devPtr_i8;
      cudaMalloc ((void**)&devPtr_i8, 256*256*sizeof(*devPtr_i8));
      assert(devPtr_i8!=NULL);
      matVecGpu.push_back(devPtr_i8);
    }
  }
  ~BytomMatListGpu() {
    for(int i=0; i<256; i++)
      cudaFree(matVecGpu[i]);
  }
};

extern void iter_mineBytom(const uint8_t *fixedMessage, uint32_t len, uint8_t result[32], cublasHandle_t handle);

extern void initMatVec(std::vector<Mat256x256i8*>& matVec, const Words32& X_in);
extern BytomMatList* matList_int8;

extern void initMatVecGpu(BytomMatListGpu* matListGpu_int8, BytomMatList* matList_int8);
extern void core_mineBytom_gpu(std::vector<uint8_t> fourSeq[4], BytomMatListGpu* matListGpu_int8, uint32_t data[64], cublasHandle_t handle);
extern BytomMatListGpu* matListGpu_int8;

#endif
