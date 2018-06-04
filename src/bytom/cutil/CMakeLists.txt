cmake_minimum_required(VERSION 3.5)

# set(CMAKE_CXX_COMPILER "/usr/local/cuda/bin/g++")

include_directories(${CUDA_INCLUDE_DIRECTORIES})

file(GLOB BytomPoW src/*.cpp src/*.cu)

CUDA_ADD_LIBRARY(GpuTs STATIC
  ${BytomPoW}
  )

target_compile_features(GpuTs PUBLIC cxx_std_11)

# copy file for testing bytom code
configure_file(src/GpuTs.h src/GpuTs.h COPYONLY )
configure_file(test/nonceutil.go test/nonceutil.go COPYONLY )
configure_file(test/nonceutil_test.go test/nonceutil_test.go COPYONLY )