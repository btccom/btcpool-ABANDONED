.PHONY: clean

TARGET=tensority
cxx_compiler = g++-6
build_flags = -ccbin $(cxx_compiler) -std=c++11


$(TARGET): GpuTs.a
	# go build -ldflags "-linkmode external -extldflags -static" $(TARGET).go
	cd .. && go test


GpuTs.a:
    # clang++ -o $@ -c $^ -std=c++11 -pthread -mavx2 -O3 -fPIC
	nvcc $(build_flags) -o BytomPoW.cu.o -c BytomPoW.cu -I /opt/OpenBLAS/include/ -L/opt/OpenBLAS/lib -lopenblas -lpthread -I /usr/local/cuda/include -L /usr/local/cuda/lib64 -lcudart -lcublas
	nvcc $(build_flags) -o BytomPoW.cpp.o -c BytomPoW.cpp -I /opt/OpenBLAS/include/ -L/opt/OpenBLAS/lib -lopenblas -lpthread -I /usr/local/cuda/include -L /usr/local/cuda/lib64 -lcudart -lcublas
	nvcc $(build_flags) -o byte_order.c.o -c byte_order.c -I /opt/OpenBLAS/include/ -L/opt/OpenBLAS/lib -lopenblas -lpthread -I /usr/local/cuda/include -L /usr/local/cuda/lib64 -lcudart -lcublas
	nvcc $(build_flags) -o sha3.c.o -c sha3.c -I /opt/OpenBLAS/include/ -L/opt/OpenBLAS/lib -lopenblas -lpthread -I /usr/local/cuda/include -L /usr/local/cuda/lib64 -lcudart -lcublas
	nvcc $(build_flags) -o GpuTs.cpp.o -c GpuTs.cpp -I /opt/OpenBLAS/include/ -L/opt/OpenBLAS/lib -lopenblas -lpthread -I /usr/local/cuda/include -L /usr/local/cuda/lib64 -lcudart -lcublas
	ar rs $@ BytomPoW.cu.o BytomPoW.cpp.o byte_order.c.o sha3.c.o GpuTs.cpp.o

clean:
	rm -f *.o *.so *.a $(TARGET)
