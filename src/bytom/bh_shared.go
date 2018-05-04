package main

import "C"
import (
	"github.com/bytom/protocol/bc/types"
)

//export DecodeHeaderString
func DecodeHeaderString(text []byte) (uint64, uint64, string, uint64, string, string, uint64) {
	bh := &types.BlockHeader{}
	bh.UnmarshalText(text)
	return bh.Version, bh.Height, bh.PreviousBlockHash.String(), bh.Timestamp, bh.TransactionsMerkleRoot.String(), bh.TransactionStatusHash.String(), bh.Bits
}

func main() {
	// We need the main function to make possible
	// CGO compiler to compile the package as C shared library
}
