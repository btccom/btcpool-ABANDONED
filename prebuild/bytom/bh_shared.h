/* Created by "go tool cgo" - DO NOT EDIT. */

/* package command-line-arguments */


#line 1 "cgo-builtin-prolog"

#include <stddef.h> /* for ptrdiff_t below */

#ifndef GO_CGO_EXPORT_PROLOGUE_H
#define GO_CGO_EXPORT_PROLOGUE_H

typedef struct { const char *p; ptrdiff_t n; } _GoString_;

#endif

/* Start of preamble from import "C" comments.  */




/* End of preamble from import "C" comments.  */


/* Start of boilerplate cgo prologue.  */
#line 1 "cgo-gcc-export-header-prolog"

#ifndef GO_CGO_PROLOGUE_H
#define GO_CGO_PROLOGUE_H

typedef signed char GoInt8;
typedef unsigned char GoUint8;
typedef short GoInt16;
typedef unsigned short GoUint16;
typedef int GoInt32;
typedef unsigned int GoUint32;
typedef long long GoInt64;
typedef unsigned long long GoUint64;
typedef GoInt64 GoInt;
typedef GoUint64 GoUint;
typedef __SIZE_TYPE__ GoUintptr;
typedef float GoFloat32;
typedef double GoFloat64;
typedef float _Complex GoComplex64;
typedef double _Complex GoComplex128;

/*
  static assertion to make sure the file is being used on architecture
  at least with matching size of GoInt.
*/
typedef char _check_for_64_bit_pointer_matching_GoInt[sizeof(void*)==64/8 ? 1:-1];

typedef _GoString_ GoString;
typedef void *GoMap;
typedef void *GoChan;
typedef struct { void *t; void *v; } GoInterface;
typedef struct { void *data; GoInt len; GoInt cap; } GoSlice;

#endif

/* End of boilerplate cgo prologue.  */

#ifdef __cplusplus
extern "C" {
#endif


/* Return type for DecodeBlockHeader */
struct DecodeBlockHeader_return {
	GoUint64 r0;
	GoUint64 r1;
	char* r2;
	GoUint64 r3;
	GoUint64 r4;
	char* r5;
	char* r6;
};

extern struct DecodeBlockHeader_return DecodeBlockHeader(GoSlice p0);

/* Return type for EncodeBlockHeader */
struct EncodeBlockHeader_return {
	char* r0;
	char* r1;
};

extern struct EncodeBlockHeader_return EncodeBlockHeader(GoUint64 p0, GoUint64 p1, char* p2, GoUint64 p3, GoUint64 p4, GoUint64 p5, char* p6, char* p7);

extern GoUint8 CheckProofOfWork(GoSlice p0, GoUint64 p1);

extern void ProofOfWorkHashCPU(GoSlice p0, GoSlice p1, GoSlice p2);

extern GoUint8 CheckProofOfWorkCPU(GoSlice p0, GoSlice p1, GoUint64 p2);

extern void CalculateTargetBinaryByDifficulty(GoUint64 p0, GoSlice p1);

extern GoUint64 CalculateTargetCompactByDifficulty(GoUint64 p0);

extern GoUint64 CalculateDifficultyByTargetCompact(GoUint64 p0);

#ifdef __cplusplus
}
#endif
