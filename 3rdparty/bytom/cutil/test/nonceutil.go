package cutil

// #cgo CFLAGS: -I /usr/local/cuda/include -I ../
// #cgo LDFLAGS: -L../ -lGpuTs -lstdc++ -L /usr/local/cuda/lib64 -lcudart -lcublas
// #include <stdint.h>
// #include "./src/GpuTs.h"
import "C"

import (
	"unsafe"

	"github.com/bytom/mining/tensority"
	"github.com/bytom/protocol/bc"
)

func Hash(blockHeader, seed *bc.Hash) *bc.Hash {
	bhBytes := blockHeader.Bytes()
	sdBytes := seed.Bytes()

	bhPtr := (*C.uint8_t)(unsafe.Pointer(&bhBytes[0]))
	seedPtr := (*C.uint8_t)(unsafe.Pointer(&sdBytes[0]))

	resPtr := C.GpuTs(bhPtr, seedPtr)

	res := bc.NewHash(*(*[32]byte)(unsafe.Pointer(resPtr)))
	return &res
}

func HashCPU(hash, seed *bc.Hash) *bc.Hash {
	compareHash := tensority.AIHash.Hash(hash, seed)
	return compareHash
}
