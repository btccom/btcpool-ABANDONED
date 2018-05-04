package main

import "C"
import (
	"github.com/bytom/protocol/bc/types"
)

//export DecodeHeaderString
func DecodeHeaderString(text []byte) (uint64, uint64, uint64, uint64) {
	bh := &types.BlockHeader{}
	bh.UnmarshalText(text)
	return bh.Version, bh.Height, bh.Timestamp, bh.Bits
}

func main() {
	// We need the main function to make possible
	// CGO compiler to compile the package as C shared library
}
