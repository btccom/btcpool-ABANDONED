package cutil

// #cgo CFLAGS: -I /usr/local/cuda/include
// #cgo LDFLAGS: -L./src/ -l:GpuTs.a -lstdc++ -L /usr/local/cuda/lib64 -lcudart -lcublas
// #include <stdint.h>
// #include "./src/GpuTs.h"
import "C"

import(
	"unsafe"

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
