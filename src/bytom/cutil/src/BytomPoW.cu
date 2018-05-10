#include "BytomPoW.h"
#include <cuda_runtime.h>
#include "cublas_v2.h"

void initMatVecGpu(BytomMatListGpu* matListGpu_int8, BytomMatList* matList_int8) {
  for(int i=0; i<matList_int8->matVec.size(); i++) {
    int8_t* hostPtr_i8 = (int8_t*)(matList_int8->at(i).d);
    int8_t* devPtr_i8 = (int8_t*)(matListGpu_int8->at(i));
    cublasStatus_t stat = cublasSetMatrix (256, 256, sizeof(*devPtr_i8), hostPtr_i8, 256, devPtr_i8, 256);
    if (stat != CUBLAS_STATUS_SUCCESS) {
      std::cerr<<"Fail to Set CuBlas Matrix."<<std::endl;
      exit(EXIT_FAILURE);
    }
  }
}

static inline void converInt32ToInt8(int32_t (&a)[256][256], int8_t (&b)[256][256]) {
  for(int i=0; i<256; i++) {
    for(int j=0; j<256; j++) {
      b[i][j]=((a[i][j]&0xFF)+ ((a[i][j]>>8)&0xFF))&0xFF;
    }
  }
}

__global__ void converInt32ToInt8_gpu(int32_t* a, int8_t* b) {
  int32_t data_i32 = a[blockIdx.x * blockDim.x + threadIdx.x];
  int8_t data_i8 = ((data_i32&0xFF)+ ((data_i32>>8)&0xFF))&0xFF;
  b[blockIdx.x * blockDim.x + threadIdx.x] = data_i8;
}

void core_mineBytom_gpu(
		std::vector<uint8_t> fourSeq[4],
		BytomMatListGpu* matListGpu_int8,
		uint32_t data[64],
        cublasHandle_t handle) {

  Mat256x256i8 *idt=new Mat256x256i8;
  Mat256x256i8 *mat=new Mat256x256i8;
  Mat256x256i8 *tmp=new Mat256x256i8;
  Mat256x256i8 *res=new Mat256x256i8[4];
  idt->toIdentityMatrix();

  int8_t* devIdt_i8;
  int8_t* devTmp_i8;
  int32_t* devTmp_i32;
  cudaMalloc ((void**)&devIdt_i8, 256*256*sizeof(*devIdt_i8));
  cudaMalloc ((void**)&devTmp_i8, 256*256*sizeof(*devTmp_i8));
  cudaMalloc ((void**)&devTmp_i32, 256*256*sizeof(*devTmp_i32));
  cublasStatus_t stat = cublasSetMatrix (256, 256, sizeof(*devIdt_i8), idt->d, 256, devIdt_i8, 256); //HKKUO: A). Memory Set
  const int alpha = 1;
  const int beta = 0;

  for(int k=0; k<4; k++) {
    for(int j=0; j<2; j++) {
      for(int i=0; i<32; i+=2) {
        if (j==0 && i==0)
          stat = cublasGemmEx(handle,
                              CUBLAS_OP_N,
                              CUBLAS_OP_N,
                              256,
                              256,
                              256,
                              &alpha,
                              matListGpu_int8->at(fourSeq[k][i]),
                              CUDA_R_8I,
                              256,
                              devIdt_i8,
                              CUDA_R_8I,
                              256,
                              &beta,
                              devTmp_i32,
                              CUDA_R_32I,
                              256,
                              CUDA_R_32I,
                              CUBLAS_GEMM_DFALT);  //HKKUO: B). General Matrix Multiplication (GEMM)
        else
          stat = cublasGemmEx(handle,
                              CUBLAS_OP_N,
                              CUBLAS_OP_N,
                              256,
                              256,
                              256,
                              &alpha,
                              matListGpu_int8->at(fourSeq[k][i]),
                              CUDA_R_8I,
                              256,
                              devTmp_i8,
                              CUDA_R_8I,
                              256,
                              &beta,
                              devTmp_i32,
                              CUDA_R_32I,
                              256,
                              CUDA_R_32I,
                              CUBLAS_GEMM_DFALT);  //HKKUO: B). General Matrix Multiplication (GEMM)
        if (stat != CUBLAS_STATUS_SUCCESS) {
          std::cerr<<"Fail to Run CuBlas GemmEx."<<std::endl;
          exit(EXIT_FAILURE);
        }
        converInt32ToInt8_gpu<<<256, 256>>>(devTmp_i32, devTmp_i8);
        stat = cublasGemmEx(handle,
                            CUBLAS_OP_N,
                            CUBLAS_OP_N,
                            256,
                            256,
                            256,
                            &alpha,
                            matListGpu_int8->at(fourSeq[k][i+1]),
                            CUDA_R_8I,
                            256,
                            devTmp_i8,
                            CUDA_R_8I,
                            256,
                            &beta,
                            devTmp_i32,
                            CUDA_R_32I,
                            256,
                            CUDA_R_32I,
                            CUBLAS_GEMM_DFALT);  //HKKUO: B). General Matrix Multiplication (GEMM)
        if (stat != CUBLAS_STATUS_SUCCESS) {
          std::cerr<<"Fail to Run CuBlas GemmEx."<<std::endl;
          exit(EXIT_FAILURE);
        }
        converInt32ToInt8_gpu<<<256, 256>>>(devTmp_i32, devTmp_i8);
      }
    }
    stat = cublasGetMatrix (256, 256, sizeof(*devTmp_i8), devTmp_i8, 256, res[k].d, 256);
  }

  mat->add(res[0], res[1]);  //HKKUO: C). Matrix Addition
  tmp->add(*mat, res[2]);    //HKKUO: C). Matrix Addition
  mat->add(*tmp, res[3]);    //HKKUO: C). Matrix Addition

  Arr256x64i32 arr(*mat);
  arr.reduceFNV();           //HKKUO: D). Reduction
  arr.fillWithD0(data);      //HKKUO: E). Memory Set
  delete idt;
  delete mat;
  delete tmp;
  delete[] res;
  cudaFree(devIdt_i8);
  cudaFree(devTmp_i8);
  cudaFree(devTmp_i32);
}
