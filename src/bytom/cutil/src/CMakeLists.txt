cmake_minimum_required(VERSION 3.5)

# set(CMAKE_CXX_COMPILER "/usr/local/cuda/bin/g++")

include_directories("/usr/local/cuda/include")

CUDA_ADD_LIBRARY(GpuTs STATIC
  BytomPoW.cpp
  GpuTs.cpp
  BytomPoW.cu
  byte_order.c
  sha3.c
  )

# # Request that particles be built with -std=c++11
# # As this is a public compile feature anything that links to particles
# # will also build with -std=c++11
target_compile_features(GpuTs PUBLIC cxx_std_11)
