// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2013 Andrea Mazzoleni

#if defined(__x86_64__)

#ifndef sc_numVars
#define sc_numVars 12
#endif
#ifndef sc_blockSize
#define sc_blockSize (sc_numVars * 8)
#endif
#ifndef sc_const
#define sc_const 0xdeadbeefdeadbeefLL
#endif

#define ASM_END_STEP(s1, s11, s2, r, r_diff) \
	"paddq " s1 ", " s11 "\n\t" \
	"pxor " s11 ", " s2 "\n\t" \
	"movdqa " s1 ", %%xmm13\n\t" \
	"psllq $" #r ", " s1 "\n\t" \
	"psrlq $" #r_diff ", %%xmm13\n\t" \
	"por %%xmm13, " s1 "\n\t"

#define ASM_END_PARTIAL \
	ASM_END_STEP("%%xmm1", "%%xmm11", "%%xmm2", 44, 20) \
	ASM_END_STEP("%%xmm2", "%%xmm0", "%%xmm3", 15, 49) \
	ASM_END_STEP("%%xmm3", "%%xmm1", "%%xmm4", 34, 30) \
	ASM_END_STEP("%%xmm4", "%%xmm2", "%%xmm5", 21, 43) \
	ASM_END_STEP("%%xmm5", "%%xmm3", "%%xmm6", 38, 26) \
	ASM_END_STEP("%%xmm6", "%%xmm4", "%%xmm7", 33, 31) \
	ASM_END_STEP("%%xmm7", "%%xmm5", "%%xmm8", 10, 54) \
	ASM_END_STEP("%%xmm8", "%%xmm6", "%%xmm9", 13, 51) \
	ASM_END_STEP("%%xmm9", "%%xmm7", "%%xmm10", 38, 26) \
	ASM_END_STEP("%%xmm10", "%%xmm8", "%%xmm11", 53, 11) \
	ASM_END_STEP("%%xmm11", "%%xmm9", "%%xmm0", 42, 22) \
	ASM_END_STEP("%%xmm0", "%%xmm10", "%%xmm1", 54, 10)

void SpookyHash128SSE(
	const void* data0, const void* data1,
	size_t size,
	const uint8_t* seed0, const uint8_t* seed1,
	uint8_t* digest0, uint8_t* digest1
)
{
	uint64_t buf0[sc_numVars];
	uint64_t buf1[sc_numVars];
	size_t nblocks;
	const uint8_t* blocks0;
	const uint8_t* blocks1;
	const uint8_t* end0;
	size_t size_remainder;
	static const uint64_t sc_const_val = sc_const;

	nblocks = size / sc_blockSize;
	blocks0 = (const uint8_t*)data0;
	blocks1 = (const uint8_t*)data1;
	end0 = blocks0 + nblocks * sc_blockSize;

	/* tail data is computed beforehand */
	size_remainder = size - nblocks * sc_blockSize;
	memcpy(buf0, blocks0 + nblocks * sc_blockSize, size_remainder);
	memset(((uint8_t*)buf0) + size_remainder, 0, sc_blockSize - size_remainder);
	((uint8_t*)buf0)[sc_blockSize - 1] = size_remainder;

	memcpy(buf1, blocks1 + nblocks * sc_blockSize, size_remainder);
	memset(((uint8_t*)buf1) + size_remainder, 0, sc_blockSize - size_remainder);
	((uint8_t*)buf1)[sc_blockSize - 1] = size_remainder;

	__asm__ __volatile__ (
		/* Load seed0 into XMM registers */
		"movq (%[seed0]), %%xmm9\n\t"
		"movhps (%[seed1]), %%xmm9\n\t"
		"movq 8(%[seed0]), %%xmm10\n\t"
		"movhps 8(%[seed1]), %%xmm10\n\t"

		/* Load sc_const into XMM15 and duplicate it */
		"movq (%[sc_const_ptr]), %%xmm15\n\t"
		"punpcklqdq %%xmm15, %%xmm15\n\t"

		/* Initialize h0..h11 */
		"movdqa %%xmm9, %%xmm0\n\t"
		"movdqa %%xmm9, %%xmm3\n\t"
		"movdqa %%xmm9, %%xmm6\n\t"
		"movdqa %%xmm10, %%xmm1\n\t"
		"movdqa %%xmm10, %%xmm4\n\t"
		"movdqa %%xmm10, %%xmm7\n\t"
		"movdqa %%xmm15, %%xmm2\n\t"
		"movdqa %%xmm15, %%xmm5\n\t"
		"movdqa %%xmm15, %%xmm8\n\t"
		"movdqa %%xmm15, %%xmm11\n\t"

		/* Main loop */
		"1:\n\t"
		"cmp %[end0], %[blocks0]\n\t"
		"jae 2f\n\t"

		/* Step 0 */
		"movq 0*8(%[blocks0]), %%xmm12\n\t"
		"movhps 0*8(%[blocks1]), %%xmm12\n\t"
		"paddq %%xmm12, %%xmm0\n\t"
		"pxor %%xmm10, %%xmm2\n\t"
		"pxor %%xmm0, %%xmm11\n\t"
		"movdqa %%xmm0, %%xmm13\n\t"
		"psllq $11, %%xmm0\n\t"
		"psrlq $53, %%xmm13\n\t"
		"por %%xmm13, %%xmm0\n\t"
		"paddq %%xmm1, %%xmm11\n\t"

		/* Step 1 */
		"movq 1*8(%[blocks0]), %%xmm12\n\t"
		"movhps 1*8(%[blocks1]), %%xmm12\n\t"
		"paddq %%xmm12, %%xmm1\n\t"
		"pxor %%xmm11, %%xmm3\n\t"
		"pxor %%xmm1, %%xmm0\n\t"
		"movdqa %%xmm1, %%xmm13\n\t"
		"psllq $32, %%xmm1\n\t"
		"psrlq $32, %%xmm13\n\t"
		"por %%xmm13, %%xmm1\n\t"
		"paddq %%xmm2, %%xmm0\n\t"

		/* Step 2 */
		"movq 2*8(%[blocks0]), %%xmm12\n\t"
		"movhps 2*8(%[blocks1]), %%xmm12\n\t"
		"paddq %%xmm12, %%xmm2\n\t"
		"pxor %%xmm0, %%xmm4\n\t"
		"pxor %%xmm2, %%xmm1\n\t"
		"movdqa %%xmm2, %%xmm13\n\t"
		"psllq $43, %%xmm2\n\t"
		"psrlq $21, %%xmm13\n\t"
		"por %%xmm13, %%xmm2\n\t"
		"paddq %%xmm3, %%xmm1\n\t"

		/* Step 3 */
		"movq 3*8(%[blocks0]), %%xmm12\n\t"
		"movhps 3*8(%[blocks1]), %%xmm12\n\t"
		"paddq %%xmm12, %%xmm3\n\t"
		"pxor %%xmm1, %%xmm5\n\t"
		"pxor %%xmm3, %%xmm2\n\t"
		"movdqa %%xmm3, %%xmm13\n\t"
		"psllq $31, %%xmm3\n\t"
		"psrlq $33, %%xmm13\n\t"
		"por %%xmm13, %%xmm3\n\t"
		"paddq %%xmm4, %%xmm2\n\t"

		/* Step 4 */
		"movq 4*8(%[blocks0]), %%xmm12\n\t"
		"movhps 4*8(%[blocks1]), %%xmm12\n\t"
		"paddq %%xmm12, %%xmm4\n\t"
		"pxor %%xmm2, %%xmm6\n\t"
		"pxor %%xmm4, %%xmm3\n\t"
		"movdqa %%xmm4, %%xmm13\n\t"
		"psllq $17, %%xmm4\n\t"
		"psrlq $47, %%xmm13\n\t"
		"por %%xmm13, %%xmm4\n\t"
		"paddq %%xmm5, %%xmm3\n\t"

		/* Step 5 */
		"movq 5*8(%[blocks0]), %%xmm12\n\t"
		"movhps 5*8(%[blocks1]), %%xmm12\n\t"
		"paddq %%xmm12, %%xmm5\n\t"
		"pxor %%xmm3, %%xmm7\n\t"
		"pxor %%xmm5, %%xmm4\n\t"
		"movdqa %%xmm5, %%xmm13\n\t"
		"psllq $28, %%xmm5\n\t"
		"psrlq $36, %%xmm13\n\t"
		"por %%xmm13, %%xmm5\n\t"
		"paddq %%xmm6, %%xmm4\n\t"

		/* Step 6 */
		"movq 6*8(%[blocks0]), %%xmm12\n\t"
		"movhps 6*8(%[blocks1]), %%xmm12\n\t"
		"paddq %%xmm12, %%xmm6\n\t"
		"pxor %%xmm4, %%xmm8\n\t"
		"pxor %%xmm6, %%xmm5\n\t"
		"movdqa %%xmm6, %%xmm13\n\t"
		"psllq $39, %%xmm6\n\t"
		"psrlq $25, %%xmm13\n\t"
		"por %%xmm13, %%xmm6\n\t"
		"paddq %%xmm7, %%xmm5\n\t"

		/* Step 7 */
		"movq 7*8(%[blocks0]), %%xmm12\n\t"
		"movhps 7*8(%[blocks1]), %%xmm12\n\t"
		"paddq %%xmm12, %%xmm7\n\t"
		"pxor %%xmm5, %%xmm9\n\t"
		"pxor %%xmm7, %%xmm6\n\t"
		"movdqa %%xmm7, %%xmm13\n\t"
		"psllq $57, %%xmm7\n\t"
		"psrlq $7, %%xmm13\n\t"
		"por %%xmm13, %%xmm7\n\t"
		"paddq %%xmm8, %%xmm6\n\t"

		/* Step 8 */
		"movq 8*8(%[blocks0]), %%xmm12\n\t"
		"movhps 8*8(%[blocks1]), %%xmm12\n\t"
		"paddq %%xmm12, %%xmm8\n\t"
		"pxor %%xmm6, %%xmm10\n\t"
		"pxor %%xmm8, %%xmm7\n\t"
		"movdqa %%xmm8, %%xmm13\n\t"
		"psllq $55, %%xmm8\n\t"
		"psrlq $9, %%xmm13\n\t"
		"por %%xmm13, %%xmm8\n\t"
		"paddq %%xmm9, %%xmm7\n\t"

		/* Step 9 */
		"movq 9*8(%[blocks0]), %%xmm12\n\t"
		"movhps 9*8(%[blocks1]), %%xmm12\n\t"
		"paddq %%xmm12, %%xmm9\n\t"
		"pxor %%xmm7, %%xmm11\n\t"
		"pxor %%xmm9, %%xmm8\n\t"
		"movdqa %%xmm9, %%xmm13\n\t"
		"psllq $54, %%xmm9\n\t"
		"psrlq $10, %%xmm13\n\t"
		"por %%xmm13, %%xmm9\n\t"
		"paddq %%xmm10, %%xmm8\n\t"

		/* Step 10 */
		"movq 10*8(%[blocks0]), %%xmm12\n\t"
		"movhps 10*8(%[blocks1]), %%xmm12\n\t"
		"paddq %%xmm12, %%xmm10\n\t"
		"pxor %%xmm8, %%xmm0\n\t"
		"pxor %%xmm10, %%xmm9\n\t"
		"movdqa %%xmm10, %%xmm13\n\t"
		"psllq $22, %%xmm10\n\t"
		"psrlq $42, %%xmm13\n\t"
		"por %%xmm13, %%xmm10\n\t"
		"paddq %%xmm11, %%xmm9\n\t"

		/* Step 11 */
		"movq 11*8(%[blocks0]), %%xmm12\n\t"
		"movhps 11*8(%[blocks1]), %%xmm12\n\t"
		"paddq %%xmm12, %%xmm11\n\t"
		"pxor %%xmm9, %%xmm1\n\t"
		"pxor %%xmm11, %%xmm10\n\t"
		"movdqa %%xmm11, %%xmm13\n\t"
		"psllq $46, %%xmm11\n\t"
		"psrlq $18, %%xmm13\n\t"
		"por %%xmm13, %%xmm11\n\t"
		"paddq %%xmm0, %%xmm10\n\t"

		"add $96, %[blocks0]\n\t"
		"add $96, %[blocks1]\n\t"
		"jmp 1b\n\t"

		"2:\n\t"

		/* Load and add buf0 and buf1 elements */
		"movq 0*8(%[buf0]), %%xmm12\n\t"
		"movhps 0*8(%[buf1]), %%xmm12\n\t"
		"paddq %%xmm12, %%xmm0\n\t"

		"movq 1*8(%[buf0]), %%xmm12\n\t"
		"movhps 1*8(%[buf1]), %%xmm12\n\t"
		"paddq %%xmm12, %%xmm1\n\t"

		"movq 2*8(%[buf0]), %%xmm12\n\t"
		"movhps 2*8(%[buf1]), %%xmm12\n\t"
		"paddq %%xmm12, %%xmm2\n\t"

		"movq 3*8(%[buf0]), %%xmm12\n\t"
		"movhps 3*8(%[buf1]), %%xmm12\n\t"
		"paddq %%xmm12, %%xmm3\n\t"

		"movq 4*8(%[buf0]), %%xmm12\n\t"
		"movhps 4*8(%[buf1]), %%xmm12\n\t"
		"paddq %%xmm12, %%xmm4\n\t"

		"movq 5*8(%[buf0]), %%xmm12\n\t"
		"movhps 5*8(%[buf1]), %%xmm12\n\t"
		"paddq %%xmm12, %%xmm5\n\t"

		"movq 6*8(%[buf0]), %%xmm12\n\t"
		"movhps 6*8(%[buf1]), %%xmm12\n\t"
		"paddq %%xmm12, %%xmm6\n\t"

		"movq 7*8(%[buf0]), %%xmm12\n\t"
		"movhps 7*8(%[buf1]), %%xmm12\n\t"
		"paddq %%xmm12, %%xmm7\n\t"

		"movq 8*8(%[buf0]), %%xmm12\n\t"
		"movhps 8*8(%[buf1]), %%xmm12\n\t"
		"paddq %%xmm12, %%xmm8\n\t"

		"movq 9*8(%[buf0]), %%xmm12\n\t"
		"movhps 9*8(%[buf1]), %%xmm12\n\t"
		"paddq %%xmm12, %%xmm9\n\t"

		"movq 10*8(%[buf0]), %%xmm12\n\t"
		"movhps 10*8(%[buf1]), %%xmm12\n\t"
		"paddq %%xmm12, %%xmm10\n\t"

		"movq 11*8(%[buf0]), %%xmm12\n\t"
		"movhps 11*8(%[buf1]), %%xmm12\n\t"
		"paddq %%xmm12, %%xmm11\n\t"

		/* EndPartial 3 times */
		ASM_END_PARTIAL
		ASM_END_PARTIAL
		ASM_END_PARTIAL

		/* Store back h0 and h1 to output digests */
		"movq %%xmm0, (%[digest0])\n\t"
		"movhps %%xmm0, (%[digest1])\n\t"
		"movq %%xmm1, 8(%[digest0])\n\t"
		"movhps %%xmm1, 8(%[digest1])\n\t"

		: [blocks0] "+r" (blocks0),
		[blocks1] "+r" (blocks1)
		: [end0] "r" (end0),
		[seed0] "r" (seed0),
		[seed1] "r" (seed1),
		[digest0] "r" (digest0),
		[digest1] "r" (digest1),
		[sc_const_ptr] "r" (&sc_const_val),
		[buf0] "r" (buf0),
		[buf1] "r" (buf1)
		: "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",
		"xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15", "cc", "memory"
	);
}

#endif /* __x86_64__ */

