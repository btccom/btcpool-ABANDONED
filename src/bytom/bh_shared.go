package main

import "C"
import (
	"github.com/bytom/protocol/bc/types"
	"github.com/bytom/testutil"
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

//export EncodeBlockHeader
func EncodeBlockHeader(v, h uint64, prevBlockHashStr *C.char, timeStamp, nonce, bits uint64, transactionStatusHashStr, transactionsMerkleRootStr *C.char) (*C.char, *C.char) {
	bh := &types.BlockHeader{
		Version:           v,
		Height:            h,
		PreviousBlockHash: testutil.MustDecodeHash(C.GoString(prevBlockHashStr)),
		Timestamp:         timeStamp,
		Nonce:             nonce,
		Bits:              bits,
		BlockCommitment: types.BlockCommitment{
			TransactionStatusHash:  testutil.MustDecodeHash(C.GoString(transactionStatusHashStr)),
			TransactionsMerkleRoot: testutil.MustDecodeHash(C.GoString(transactionsMerkleRootStr)),
		},
	}

	buf, _ := bh.MarshalText()
	hash := bh.Hash()
	return C.CString(string(buf)), C.CString(hash.String())
}

func main() {
	// We need the main function to make possible
	// CGO compiler to compile the package as C shared library
	println("Test")
}
