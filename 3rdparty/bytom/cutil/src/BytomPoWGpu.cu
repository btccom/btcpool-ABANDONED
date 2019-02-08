#include "BytomPoW.h"
#include <cuda.h>
#include <cuda_runtime.h>
#include "cublas_v2.h"

void initMatVecGpu(BytomMatListGpu* matListGpu_int8, BytomMatList8* matList_int8) {
  cudaMemcpy(matListGpu_int8->matVecGpu, matList_int8->matVec, 256 * 256 * 256, cudaMemcpyHostToDevice);
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
  cublasStatus_t stat = cublasSetMatrix (256, 256, sizeof(*devIdt_i8), idt->d, 256, devIdt_i8, 256);
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
                              CUBLAS_GEMM_DFALT);
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
                              CUBLAS_GEMM_DFALT); 
        if (stat != CUBLAS_STATUS_SUCCESS) {
          std::cout<<"Fail to Run CuBlas GemmEx.1"<<std::endl;
          std::cout<<stat<<std::endl;
          std::cout<<"skip"<<std::endl;
          return;
          // exit(EXIT_FAILURE);
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
                            CUBLAS_GEMM_DFALT); 
        if (stat != CUBLAS_STATUS_SUCCESS) {
          std::cerr<<"Fail to Run CuBlas GemmEx.2"<<std::endl;
          exit(EXIT_FAILURE);
        }
        converInt32ToInt8_gpu<<<256, 256>>>(devTmp_i32, devTmp_i8);
      }
    }
    stat = cublasGetMatrix (256, 256, sizeof(*devTmp_i8), devTmp_i8, 256, res[k].d, 256);
  }

  mat->add(res[0], res[1]);  
  tmp->add(*mat, res[2]);    
  mat->add(*tmp, res[3]);    

  Arr256x64i32 arr(*mat);
  arr.reduceFNV();           
  arr.fillWithD0(data);      
  
  delete idt;
  delete mat;
  delete tmp;
  delete[] res;

  cudaFree(devIdt_i8);
  cudaFree(devTmp_i8);
  cudaFree(devTmp_i32);
  cublasDestroy(handle);
}
