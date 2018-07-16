package main

import "C"
import (
	"math/big"

	"github.com/bytom/consensus/difficulty"
	"github.com/bytom/mining/tensority"
	"github.com/bytom/protocol/bc"
	"github.com/bytom/protocol/bc/types"
	"github.com/bytom/testutil"
)

const maxBits = 0x1c7FFFFFFFFFFFFF

// //export DecodeHeaderString
// func DecodeHeaderString(text []byte) (uint64, uint64, uint64, uint64, uint64, uint64, uint64, uint64, uint64, uint64, uint64, uint64, uint64, uint64, uint64, uint64) {
// 	bh := types.BlockHeader{}
// 	bh.UnmarshalText(text)
// 	return bh.Version, bh.Height, bh.Timestamp, bh.Bits,
// 		bh.PreviousBlockHash.GetV0(), bh.PreviousBlockHash.GetV1(), bh.PreviousBlockHash.GetV2(), bh.PreviousBlockHash.GetV3(),
// 		bh.BlockCommitment.TransactionsMerkleRoot.GetV0(), bh.BlockCommitment.TransactionsMerkleRoot.GetV1(), bh.BlockCommitment.TransactionsMerkleRoot.GetV2(), bh.BlockCommitment.TransactionsMerkleRoot.GetV3(),
// 		bh.BlockCommitment.TransactionStatusHash.GetV0(), bh.BlockCommitment.TransactionStatusHash.GetV1(), bh.BlockCommitment.TransactionStatusHash.GetV2(), bh.BlockCommitment.TransactionStatusHash.GetV3()
// }

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

	// log.Printf("bh: %v\n", bh)

	buf, _ := bh.MarshalText()
	hash := bh.Hash()
	return C.CString(string(buf)), C.CString(hash.String())
}

//export CheckProofOfWork
func CheckProofOfWork(compareHash []byte, bits uint64) bool {
	x := [32]byte{}
	copy(x[:], compareHash[:32])
	ch := bc.NewHash(x)
	return difficulty.HashToBig(&ch).Cmp(difficulty.CompactToBig(bits)) <= 0
}

//export ProofOfWorkHashCPU
func ProofOfWorkHashCPU(hash, seed []byte, outHash []byte) {
	xHash := [32]byte{}
	copy(xHash[:], hash[:32])
	xSeed := [32]byte{}
	copy(xSeed[:], seed[:32])
	hhash := bc.NewHash(xHash)
	hseed := bc.NewHash(xSeed)

	compareHash := tensority.AIHash.Hash(&hhash, &hseed)
	copy(outHash[:], compareHash.Bytes()[:32])
}

//export CheckProofOfWorkCPU
func CheckProofOfWorkCPU(hash, seed []byte, bits uint64) bool {
	xHash := [32]byte{}
	copy(xHash[:], hash[:32])
	xSeed := [32]byte{}
	copy(xSeed[:], seed[:32])
	hhash := bc.NewHash(xHash)
	hseed := bc.NewHash(xSeed)

	compareHash := tensority.AIHash.Hash(&hhash, &hseed)
	// log.Printf("Proof hash: 0x%s", compareHash.String())
	return difficulty.HashToBig(compareHash).Cmp(difficulty.CompactToBig(bits)) <= 0
}

func StringToBig(h string) *big.Int {
	n := new(big.Int)
	n.SetString(h, 0)
	return n
}

var Diff1 = StringToBig("0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF")

func CalculateTargetBigIntByDifficulty(diff uint64) *big.Int {
	diffBig := new(big.Int).SetUint64(diff)
	targetBig := new(big.Int).Div(Diff1, diffBig)
	return targetBig
}

//export CalculateTargetBinaryByDifficulty
func CalculateTargetBinaryByDifficulty(diff uint64, out []byte) {
	targetCompact := CalculateTargetCompactByDifficulty(diff)
	targetBig := difficulty.CompactToBig(targetCompact)
	targetBytes := targetBig.Bytes()
	bytesLen := len(targetBytes)
	startIdx := 32 - bytesLen
	for i := 0; i < startIdx; i++ {
		out[i] = 0
	}
	copy(out[startIdx:32], targetBytes[:bytesLen])
	// log.Printf("diff %d - compact %d - big %v - bytes %v len %d - out %v len %d", diff, targetCompact, *targetBig, targetBytes, len(targetBytes), out, len(out))
}

//export CalculateTargetCompactByDifficulty
func CalculateTargetCompactByDifficulty(diff uint64) uint64 {
	targetBig := CalculateTargetBigIntByDifficulty(diff)
	return difficulty.BigToCompact(targetBig)
}

//export CalculateDifficultyByTargetCompact
func CalculateDifficultyByTargetCompact(target uint64) uint64 {
	targetBig := difficulty.CompactToBig(target)
	diffBig := new(big.Int).Div(Diff1, targetBig)
	return diffBig.Uint64()
	// return difficulty.BigToCompact(diffBig)
}

func main() {
	// We need the main function to make possible
	// CGO compiler to compile the package as C shared library
	println("Test")
}
