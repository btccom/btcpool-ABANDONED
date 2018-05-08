package main

import "C"
import (
	"github.com/bytom/protocol/bc/types"
)

//export DecodeHeaderString
func DecodeHeaderString(text []byte) (uint64, uint64, uint64, uint64, uint64, uint64, uint64, uint64, uint64, uint64, uint64, uint64, uint64, uint64, uint64, uint64) {
	bh := types.BlockHeader{}
	bh.UnmarshalText(text)
	return bh.Version, bh.Height, bh.Timestamp, bh.Bits,
		bh.PreviousBlockHash.GetV0(), bh.PreviousBlockHash.GetV1(), bh.PreviousBlockHash.GetV2(), bh.PreviousBlockHash.GetV3(),
		bh.BlockCommitment.TransactionsMerkleRoot.GetV0(), bh.BlockCommitment.TransactionsMerkleRoot.GetV1(), bh.BlockCommitment.TransactionsMerkleRoot.GetV2(), bh.BlockCommitment.TransactionsMerkleRoot.GetV3(),
		bh.BlockCommitment.TransactionStatusHash.GetV0(), bh.BlockCommitment.TransactionStatusHash.GetV1(), bh.BlockCommitment.TransactionStatusHash.GetV2(), bh.BlockCommitment.TransactionStatusHash.GetV3()
}

//export DecodeBlockHeader
func DecodeBlockHeader(text []byte) (uint64, uint64, *C.char, uint64, uint64, *C.char, *C.char) {
	bh := types.BlockHeader{}
	bh.UnmarshalText(text)
	return bh.Version, bh.Height, C.CString(bh.PreviousBlockHash.String()), bh.Timestamp, bh.Bits, C.CString(bh.BlockCommitment.TransactionsMerkleRoot.String()), C.CString(bh.BlockCommitment.TransactionStatusHash.String())
}

func main() {
	// We need the main function to make possible
	// CGO compiler to compile the package as C shared library
}
