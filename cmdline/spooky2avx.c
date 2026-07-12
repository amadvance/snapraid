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

#define ASM_STEP_AVX2(idx, s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11, r, r_diff) \
	"vmovq " #idx "*8(%[blocks0]), %%xmm12\n\t" \
	"vmovhps " #idx "*8(%[blocks1]), %%xmm12, %%xmm12\n\t" \
	"vmovq " #idx "*8(%[blocks2]), %%xmm13\n\t" \
	"vmovhps " #idx "*8(%[blocks3]), %%xmm13, %%xmm13\n\t" \
	"vinserti128 $1, %%xmm13, %%ymm12, %%ymm12\n\t" \
	"vpaddq %%ymm12, " s0 ", " s0 "\n\t" \
	"vpxor " s10 ", " s2 ", " s2 "\n\t" \
	"vpxor " s0 ", " s11 ", " s11 "\n\t" \
	"vmovdqa " s0 ", %%ymm13\n\t" \
	"vpsllq $" #r ", " s0 ", " s0 "\n\t" \
	"vpsrlq $" #r_diff ", %%ymm13, %%ymm13\n\t" \
	"vpor %%ymm13, " s0 ", " s0 "\n\t" \
	"vpaddq " s1 ", " s11 ", " s11 "\n\t"

#define ASM_MIX_AVX2 \
	ASM_STEP_AVX2(0, "%%ymm0", "%%ymm1", "%%ymm2", "%%ymm3", "%%ymm4", "%%ymm5", "%%ymm6", "%%ymm7", "%%ymm8", "%%ymm9", "%%ymm10", "%%ymm11", 11, 53) \
	ASM_STEP_AVX2(1, "%%ymm1", "%%ymm2", "%%ymm3", "%%ymm4", "%%ymm5", "%%ymm6", "%%ymm7", "%%ymm8", "%%ymm9", "%%ymm10", "%%ymm11", "%%ymm0", 32, 32) \
	ASM_STEP_AVX2(2, "%%ymm2", "%%ymm3", "%%ymm4", "%%ymm5", "%%ymm6", "%%ymm7", "%%ymm8", "%%ymm9", "%%ymm10", "%%ymm11", "%%ymm0", "%%ymm1", 43, 21) \
	ASM_STEP_AVX2(3, "%%ymm3", "%%ymm4", "%%ymm5", "%%ymm6", "%%ymm7", "%%ymm8", "%%ymm9", "%%ymm10", "%%ymm11", "%%ymm0", "%%ymm1", "%%ymm2", 31, 33) \
	ASM_STEP_AVX2(4, "%%ymm4", "%%ymm5", "%%ymm6", "%%ymm7", "%%ymm8", "%%ymm9", "%%ymm10", "%%ymm11", "%%ymm0", "%%ymm1", "%%ymm2", "%%ymm3", 17, 47) \
	ASM_STEP_AVX2(5, "%%ymm5", "%%ymm6", "%%ymm7", "%%ymm8", "%%ymm9", "%%ymm10", "%%ymm11", "%%ymm0", "%%ymm1", "%%ymm2", "%%ymm3", "%%ymm4", 28, 36) \
	ASM_STEP_AVX2(6, "%%ymm6", "%%ymm7", "%%ymm8", "%%ymm9", "%%ymm10", "%%ymm11", "%%ymm0", "%%ymm1", "%%ymm2", "%%ymm3", "%%ymm4", "%%ymm5", 39, 25) \
	ASM_STEP_AVX2(7, "%%ymm7", "%%ymm8", "%%ymm9", "%%ymm10", "%%ymm11", "%%ymm0", "%%ymm1", "%%ymm2", "%%ymm3", "%%ymm4", "%%ymm5", "%%ymm6", 57, 7) \
	ASM_STEP_AVX2(8, "%%ymm8", "%%ymm9", "%%ymm10", "%%ymm11", "%%ymm0", "%%ymm1", "%%ymm2", "%%ymm3", "%%ymm4", "%%ymm5", "%%ymm6", "%%ymm7", 55, 9) \
	ASM_STEP_AVX2(9, "%%ymm9", "%%ymm10", "%%ymm11", "%%ymm0", "%%ymm1", "%%ymm2", "%%ymm3", "%%ymm4", "%%ymm5", "%%ymm6", "%%ymm7", "%%ymm8", 54, 10) \
	ASM_STEP_AVX2(10, "%%ymm10", "%%ymm11", "%%ymm0", "%%ymm1", "%%ymm2", "%%ymm3", "%%ymm4", "%%ymm5", "%%ymm6", "%%ymm7", "%%ymm8", "%%ymm9", 22, 42) \
	ASM_STEP_AVX2(11, "%%ymm11", "%%ymm0", "%%ymm1", "%%ymm2", "%%ymm3", "%%ymm4", "%%ymm5", "%%ymm6", "%%ymm7", "%%ymm8", "%%ymm9", "%%ymm10", 46, 18)

#define ASM_END_STEP_AVX2(s1, s11, s2, r, r_diff) \
	"vpaddq " s1 ", " s11 ", " s11 "\n\t" \
	"vpxor " s11 ", " s2 ", " s2 "\n\t" \
	"vmovdqa " s1 ", %%ymm13\n\t" \
	"vpsllq $" #r ", " s1 ", " s1 "\n\t" \
	"vpsrlq $" #r_diff ", %%ymm13, %%ymm13\n\t" \
	"vpor %%ymm13, " s1 ", " s1 "\n\t"

#define ASM_END_PARTIAL_AVX2 \
	ASM_END_STEP_AVX2("%%ymm1", "%%ymm11", "%%ymm2", 44, 20) \
	ASM_END_STEP_AVX2("%%ymm2", "%%ymm0", "%%ymm3", 15, 49) \
	ASM_END_STEP_AVX2("%%ymm3", "%%ymm1", "%%ymm4", 34, 30) \
	ASM_END_STEP_AVX2("%%ymm4", "%%ymm2", "%%ymm5", 21, 43) \
	ASM_END_STEP_AVX2("%%ymm5", "%%ymm3", "%%ymm6", 38, 26) \
	ASM_END_STEP_AVX2("%%ymm6", "%%ymm4", "%%ymm7", 33, 31) \
	ASM_END_STEP_AVX2("%%ymm7", "%%ymm5", "%%ymm8", 10, 54) \
	ASM_END_STEP_AVX2("%%ymm8", "%%ymm6", "%%ymm9", 13, 51) \
	ASM_END_STEP_AVX2("%%ymm9", "%%ymm7", "%%ymm10", 38, 26) \
	ASM_END_STEP_AVX2("%%ymm10", "%%ymm8", "%%ymm11", 53, 11) \
	ASM_END_STEP_AVX2("%%ymm11", "%%ymm9", "%%ymm0", 42, 22) \
	ASM_END_STEP_AVX2("%%ymm0", "%%ymm10", "%%ymm1", 54, 10)

void SpookyHash128AVX(
	const void* data0, const void* data1, const void* data2, const void* data3,
	size_t size,
	const uint8_t* seed0, const uint8_t* seed1, const uint8_t* seed2, const uint8_t* seed3,
	uint8_t* digest0, uint8_t* digest1, uint8_t* digest2, uint8_t* digest3
)
{
	uint64_t buf0[sc_numVars];
	uint64_t buf1[sc_numVars];
	uint64_t buf2[sc_numVars];
	uint64_t buf3[sc_numVars];
	size_t nblocks;
	const uint8_t* blocks0;
	const uint8_t* blocks1;
	const uint8_t* blocks2;
	const uint8_t* blocks3;
	const uint8_t* end0;
	size_t size_remainder;
	static const uint64_t sc_const_val = sc_const;

	nblocks = size / sc_blockSize;
	blocks0 = (const uint8_t*)data0;
	blocks1 = (const uint8_t*)data1;
	blocks2 = (const uint8_t*)data2;
	blocks3 = (const uint8_t*)data3;
	end0 = blocks0 + nblocks * sc_blockSize;

	/* tail data is computed beforehand */
	size_remainder = size - nblocks * sc_blockSize;
	memcpy(buf0, blocks0 + nblocks * sc_blockSize, size_remainder);
	memset(((uint8_t*)buf0) + size_remainder, 0, sc_blockSize - size_remainder);
	((uint8_t*)buf0)[sc_blockSize - 1] = size_remainder;

	memcpy(buf1, blocks1 + nblocks * sc_blockSize, size_remainder);
	memset(((uint8_t*)buf1) + size_remainder, 0, sc_blockSize - size_remainder);
	((uint8_t*)buf1)[sc_blockSize - 1] = size_remainder;

	memcpy(buf2, blocks2 + nblocks * sc_blockSize, size_remainder);
	memset(((uint8_t*)buf2) + size_remainder, 0, sc_blockSize - size_remainder);
	((uint8_t*)buf2)[sc_blockSize - 1] = size_remainder;

	memcpy(buf3, blocks3 + nblocks * sc_blockSize, size_remainder);
	memset(((uint8_t*)buf3) + size_remainder, 0, sc_blockSize - size_remainder);
	((uint8_t*)buf3)[sc_blockSize - 1] = size_remainder;

	__asm__ __volatile__ (
		/* Load seed0 into XMM registers */
		"movq %[seed0], %%rax\n\t"
		"vmovq (%%rax), %%xmm9\n\t"
		"movq %[seed1], %%rax\n\t"
		"vmovhps (%%rax), %%xmm9, %%xmm9\n\t"
		"movq %[seed2], %%rax\n\t"
		"vmovq (%%rax), %%xmm13\n\t"
		"movq %[seed3], %%rax\n\t"
		"vmovhps (%%rax), %%xmm13, %%xmm13\n\t"
		"vinserti128 $1, %%xmm13, %%ymm9, %%ymm9\n\t"

		"movq %[seed0], %%rax\n\t"
		"vmovq 8(%%rax), %%xmm10\n\t"
		"movq %[seed1], %%rax\n\t"
		"vmovhps 8(%%rax), %%xmm10, %%xmm10\n\t"
		"movq %[seed2], %%rax\n\t"
		"vmovq 8(%%rax), %%xmm13\n\t"
		"movq %[seed3], %%rax\n\t"
		"vmovhps 8(%%rax), %%xmm13, %%xmm13\n\t"
		"vinserti128 $1, %%xmm13, %%ymm10, %%ymm10\n\t"

		/* Load sc_const into XMM15 and duplicate it across YMM15 */
		"vmovq %[sc_const_val], %%xmm15\n\t"
		"vpunpcklqdq %%xmm15, %%xmm15, %%xmm15\n\t"
		"vinserti128 $1, %%xmm15, %%ymm15, %%ymm15\n\t"

		/* Initialize h0..h11 */
		"vmovdqa %%ymm9, %%ymm0\n\t"
		"vmovdqa %%ymm9, %%ymm3\n\t"
		"vmovdqa %%ymm9, %%ymm6\n\t"
		"vmovdqa %%ymm10, %%ymm1\n\t"
		"vmovdqa %%ymm10, %%ymm4\n\t"
		"vmovdqa %%ymm10, %%ymm7\n\t"
		"vmovdqa %%ymm15, %%ymm2\n\t"
		"vmovdqa %%ymm15, %%ymm5\n\t"
		"vmovdqa %%ymm15, %%ymm8\n\t"
		"vmovdqa %%ymm15, %%ymm11\n\t"

		/* Main loop */
		"1:\n\t"
		"cmp %[end0], %[blocks0]\n\t"
		"jae 2f\n\t"

		ASM_MIX_AVX2

		"add $96, %[blocks0]\n\t"
		"add $96, %[blocks1]\n\t"
		"add $96, %[blocks2]\n\t"
		"add $96, %[blocks3]\n\t"
		"jmp 1b\n\t"

		"2:\n\t"

		/* Copy buf0..buf3 pointers into blocks0..blocks3 to reuse registers */
		"leaq %[buf0], %[blocks0]\n\t"
		"leaq %[buf1], %[blocks1]\n\t"
		"leaq %[buf2], %[blocks2]\n\t"
		"leaq %[buf3], %[blocks3]\n\t"

		/* Load and add buf0..buf3 elements */
		"vmovq 0*8(%[blocks0]), %%xmm12\n\t"
		"vmovhps 0*8(%[blocks1]), %%xmm12, %%xmm12\n\t"
		"vmovq 0*8(%[blocks2]), %%xmm13\n\t"
		"vmovhps 0*8(%[blocks3]), %%xmm13, %%xmm13\n\t"
		"vinserti128 $1, %%xmm13, %%ymm12, %%ymm12\n\t"
		"vpaddq %%ymm12, %%ymm0, %%ymm0\n\t"

		"vmovq 1*8(%[blocks0]), %%xmm12\n\t"
		"vmovhps 1*8(%[blocks1]), %%xmm12, %%xmm12\n\t"
		"vmovq 1*8(%[blocks2]), %%xmm13\n\t"
		"vmovhps 1*8(%[blocks3]), %%xmm13, %%xmm13\n\t"
		"vinserti128 $1, %%xmm13, %%ymm12, %%ymm12\n\t"
		"vpaddq %%ymm12, %%ymm1, %%ymm1\n\t"

		"vmovq 2*8(%[blocks0]), %%xmm12\n\t"
		"vmovhps 2*8(%[blocks1]), %%xmm12, %%xmm12\n\t"
		"vmovq 2*8(%[blocks2]), %%xmm13\n\t"
		"vmovhps 2*8(%[blocks3]), %%xmm13, %%xmm13\n\t"
		"vinserti128 $1, %%xmm13, %%ymm12, %%ymm12\n\t"
		"vpaddq %%ymm12, %%ymm2, %%ymm2\n\t"

		"vmovq 3*8(%[blocks0]), %%xmm12\n\t"
		"vmovhps 3*8(%[blocks1]), %%xmm12, %%xmm12\n\t"
		"vmovq 3*8(%[blocks2]), %%xmm13\n\t"
		"vmovhps 3*8(%[blocks3]), %%xmm13, %%xmm13\n\t"
		"vinserti128 $1, %%xmm13, %%ymm12, %%ymm12\n\t"
		"vpaddq %%ymm12, %%ymm3, %%ymm3\n\t"

		"vmovq 4*8(%[blocks0]), %%xmm12\n\t"
		"vmovhps 4*8(%[blocks1]), %%xmm12, %%xmm12\n\t"
		"vmovq 4*8(%[blocks2]), %%xmm13\n\t"
		"vmovhps 4*8(%[blocks3]), %%xmm13, %%xmm13\n\t"
		"vinserti128 $1, %%xmm13, %%ymm12, %%ymm12\n\t"
		"vpaddq %%ymm12, %%ymm4, %%ymm4\n\t"

		"vmovq 5*8(%[blocks0]), %%xmm12\n\t"
		"vmovhps 5*8(%[blocks1]), %%xmm12, %%xmm12\n\t"
		"vmovq 5*8(%[blocks2]), %%xmm13\n\t"
		"vmovhps 5*8(%[blocks3]), %%xmm13, %%xmm13\n\t"
		"vinserti128 $1, %%xmm13, %%ymm12, %%ymm12\n\t"
		"vpaddq %%ymm12, %%ymm5, %%ymm5\n\t"

		"vmovq 6*8(%[blocks0]), %%xmm12\n\t"
		"vmovhps 6*8(%[blocks1]), %%xmm12, %%xmm12\n\t"
		"vmovq 6*8(%[blocks2]), %%xmm13\n\t"
		"vmovhps 6*8(%[blocks3]), %%xmm13, %%xmm13\n\t"
		"vinserti128 $1, %%xmm13, %%ymm12, %%ymm12\n\t"
		"vpaddq %%ymm12, %%ymm6, %%ymm6\n\t"

		"vmovq 7*8(%[blocks0]), %%xmm12\n\t"
		"vmovhps 7*8(%[blocks1]), %%xmm12, %%xmm12\n\t"
		"vmovq 7*8(%[blocks2]), %%xmm13\n\t"
		"vmovhps 7*8(%[blocks3]), %%xmm13, %%xmm13\n\t"
		"vinserti128 $1, %%xmm13, %%ymm12, %%ymm12\n\t"
		"vpaddq %%ymm12, %%ymm7, %%ymm7\n\t"

		"vmovq 8*8(%[blocks0]), %%xmm12\n\t"
		"vmovhps 8*8(%[blocks1]), %%xmm12, %%xmm12\n\t"
		"vmovq 8*8(%[blocks2]), %%xmm13\n\t"
		"vmovhps 8*8(%[blocks3]), %%xmm13, %%xmm13\n\t"
		"vinserti128 $1, %%xmm13, %%ymm12, %%ymm12\n\t"
		"vpaddq %%ymm12, %%ymm8, %%ymm8\n\t"

		"vmovq 9*8(%[blocks0]), %%xmm12\n\t"
		"vmovhps 9*8(%[blocks1]), %%xmm12, %%xmm12\n\t"
		"vmovq 9*8(%[blocks2]), %%xmm13\n\t"
		"vmovhps 9*8(%[blocks3]), %%xmm13, %%xmm13\n\t"
		"vinserti128 $1, %%xmm13, %%ymm12, %%ymm12\n\t"
		"vpaddq %%ymm12, %%ymm9, %%ymm9\n\t"

		"vmovq 10*8(%[blocks0]), %%xmm12\n\t"
		"vmovhps 10*8(%[blocks1]), %%xmm12, %%xmm12\n\t"
		"vmovq 10*8(%[blocks2]), %%xmm13\n\t"
		"vmovhps 10*8(%[blocks3]), %%xmm13, %%xmm13\n\t"
		"vinserti128 $1, %%xmm13, %%ymm12, %%ymm12\n\t"
		"vpaddq %%ymm12, %%ymm10, %%ymm10\n\t"

		"vmovq 11*8(%[blocks0]), %%xmm12\n\t"
		"vmovhps 11*8(%[blocks1]), %%xmm12, %%xmm12\n\t"
		"vmovq 11*8(%[blocks2]), %%xmm13\n\t"
		"vmovhps 11*8(%[blocks3]), %%xmm13, %%xmm13\n\t"
		"vinserti128 $1, %%xmm13, %%ymm12, %%ymm12\n\t"
		"vpaddq %%ymm12, %%ymm11, %%ymm11\n\t"

		/* EndPartial 3 times */
		ASM_END_PARTIAL_AVX2
		ASM_END_PARTIAL_AVX2
		ASM_END_PARTIAL_AVX2

		/* Extract upper halves of h0 and h1 */
		"vextracti128 $1, %%ymm0, %%xmm12\n\t"
		"vextracti128 $1, %%ymm1, %%xmm13\n\t"

		/* Store digest0 and digest1 using XMM registers */
		"movq %[digest0], %%rax\n\t"
		"vmovq %%xmm0, (%%rax)\n\t"
		"vmovq %%xmm1, 8(%%rax)\n\t"

		"movq %[digest1], %%rcx\n\t"
		"vmovhps %%xmm0, (%%rcx)\n\t"
		"vmovhps %%xmm1, 8(%%rcx)\n\t"

		/* Store digest2 and digest3 using extracted XMM registers */
		"movq %[digest2], %%rdx\n\t"
		"vmovq %%xmm12, (%%rdx)\n\t"
		"vmovq %%xmm13, 8(%%rdx)\n\t"

		"movq %[digest3], %%r8\n\t"
		"vmovhps %%xmm12, (%%r8)\n\t"
		"vmovhps %%xmm13, 8(%%r8)\n\t"

		: [blocks0] "+r" (blocks0),
		[blocks1] "+r" (blocks1),
		[blocks2] "+r" (blocks2),
		[blocks3] "+r" (blocks3)
		: [end0] "r" (end0),
		[seed0] "m" (seed0),
		[seed1] "m" (seed1),
		[seed2] "m" (seed2),
		[seed3] "m" (seed3),
		[digest0] "m" (digest0),
		[digest1] "m" (digest1),
		[digest2] "m" (digest2),
		[digest3] "m" (digest3),
		[sc_const_val] "m" (sc_const_val),
		[buf0] "m" (buf0),
		[buf1] "m" (buf1),
		[buf2] "m" (buf2),
		[buf3] "m" (buf3)
		: "rax", "rcx", "rdx", "r8", "r9", "r10", "r11",
		"xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",
		"xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15", "cc", "memory"
	);
}

#else /* !__x86_64__ */

void SpookyHash128AVX(
	const void* data0, const void* data1, const void* data2, const void* data3,
	size_t size,
	const uint8_t* seed0, const uint8_t* seed1, const uint8_t* seed2, const uint8_t* seed3,
	uint8_t* digest0, uint8_t* digest1, uint8_t* digest2, uint8_t* digest3
)
{
	SpookyHash128(data0, size, seed0, digest0);
	SpookyHash128(data1, size, seed1, digest1);
	SpookyHash128(data2, size, seed2, digest2);
	SpookyHash128(data3, size, seed3, digest3);
}

#endif /* __x86_64__ */

