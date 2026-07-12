// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Andrea Mazzoleni

#if defined(__x86_64__)

#define AVX_LOAD(offset, dest) \
	"vmovq " #offset "*8(%[blocks0]), %%xmm14\n\t" \
	"vmovhps " #offset "*8(%[blocks1]), %%xmm14, %%xmm14\n\t" \
	"vmovq " #offset "*8(%[blocks2]), %%xmm15\n\t" \
	"vmovhps " #offset "*8(%[blocks3]), %%xmm15, %%xmm15\n\t" \
	"vinserti128 $1, %%xmm15, %%ymm14, " dest "\n\t"

#define AVX_LOAD_NEG(offset, dest) \
	"vmovq -" #offset "(%%r8), %%xmm14\n\t" \
	"vmovhps -" #offset "(%%r9), %%xmm14, %%xmm14\n\t" \
	"vmovq -" #offset "(%%r10), %%xmm15\n\t" \
	"vmovhps -" #offset "(%%r11), %%xmm15, %%xmm15\n\t" \
	"vinserti128 $1, %%xmm15, %%ymm14, " dest "\n\t"

#define AVX_MULT(ymm_a, ymm_b, ymm_lo, ymm_hi) \
	/* 1. ymm_ll = ymm_a * ymm_b (low parts) */ \
	"vpmuludq " ymm_b ", " ymm_a ", %%ymm8\n\t" /* ymm8 = P_ll */ \
	/* 2. ymm_lh = ymm_a * ymm_b_high */ \
	"vpsrlq $32, " ymm_b ", %%ymm9\n\t" /* ymm9 = B_h */ \
	"vpmuludq %%ymm9, " ymm_a ", %%ymm10\n\t" /* ymm10 = P_lh */ \
	/* 3. ymm_hl = ymm_a_high * ymm_b */ \
	"vpsrlq $32, " ymm_a ", %%ymm14\n\t" /* ymm14 = A_h */ \
	"vpmuludq " ymm_b ", %%ymm14, %%ymm15\n\t" /* ymm15 = P_hl */ \
	/* 4. ymm_hh = ymm_a_high * ymm_b_high */ \
	"vpmuludq %%ymm9, %%ymm14, %%ymm9\n\t" /* ymm9 = P_hh */ \
	/* 5. M = P_lh + P_hl */ \
	"vpaddq %%ymm15, %%ymm10, %%ymm15\n\t" /* ymm15 = M */ \
	/* Generate mask 0xffffffff in ymm14 */ \
	"vpcmpeqd %%ymm14, %%ymm14, %%ymm14\n\t" \
	"vpsrlq $32, %%ymm14, %%ymm14\n\t" \
	/* 6. Split M into ymm10 = M_low, ymm15 = M_high */ \
	"vpand %%ymm14, %%ymm15, %%ymm10\n\t" /* ymm10 = M_low */ \
	"vpsrlq $32, %%ymm15, %%ymm15\n\t" /* ymm15 = M_high */ \
	/* 7. Split P_ll (in ymm8) into ymm12 = P_lll, ymm13 = P_llh */ \
	"vpsrlq $32, %%ymm8, %%ymm13\n\t" /* ymm13 = P_llh */ \
	"vpand %%ymm14, %%ymm8, %%ymm12\n\t" /* ymm12 = P_lll */ \
	/* 8. S1 = P_llh + M_low */ \
	"vpaddq %%ymm10, %%ymm13, %%ymm10\n\t" /* ymm10 = S1 */ \
	/* 9. Split S1 into ymm13 = C1, ymm10 = S1_l */ \
	"vpsrlq $32, %%ymm10, %%ymm13\n\t" /* ymm13 = C1 */ \
	"vpand %%ymm14, %%ymm10, %%ymm10\n\t" /* ymm10 = S1_l */ \
	/* 10. Assemble final low = P_lll + S1_l * 2^32 */ \
	"vpsllq $32, %%ymm10, %%ymm10\n\t" \
	"vpaddq %%ymm12, %%ymm10, " ymm_lo "\n\t" /* ymm_lo = final low */ \
	/* 11. Split P_hh (in ymm9) into ymm12 = P_hhl, ymm10 = P_hhh */ \
	"vpsrlq $32, %%ymm9, %%ymm10\n\t" /* ymm10 = P_hhh */ \
	"vpand %%ymm14, %%ymm9, %%ymm12\n\t" /* ymm12 = P_hhl */ \
	/* 12. Assemble final high = M_high + P_hhl + C1 + P_hhh * 2^32 */ \
	"vpaddq %%ymm12, %%ymm15, %%ymm15\n\t" /* ymm15 = M_high + P_hhl */ \
	"vpaddq %%ymm13, %%ymm15, %%ymm15\n\t" /* ymm15 = M_high + P_hhl + C1 */ \
	"vpsllq $32, %%ymm10, %%ymm10\n\t" \
	"vpaddq %%ymm10, %%ymm15, " ymm_hi "\n\t" /* ymm_hi = final high */

void MuseAirLoongAVX(
	const void* data0, const void* data1, const void* data2, const void* data3,
	size_t size,
	const uint8_t* seed0, const uint8_t* seed1, const uint8_t* seed2, const uint8_t* seed3,
	uint8_t* digest0, uint8_t* digest1, uint8_t* digest2, uint8_t* digest3
)
{
	uint64_t buf0[12];
	uint64_t buf1[12];
	uint64_t buf2[12];
	uint64_t buf3[12];
	const uint8_t* blocks0 = data0;
	const uint8_t* blocks1 = data1;
	const uint8_t* blocks2 = data2;
	const uint8_t* blocks3 = data3;
	size_t q = size;

	if (q < 32) {
		memset(buf0, 0, 32);
		memset(buf1, 0, 32);
		memset(buf2, 0, 32);
		memset(buf3, 0, 32);
		if (q > 0) {
			memcpy(buf0, blocks0, q);
			memcpy(buf1, blocks1, q);
			memcpy(buf2, blocks2, q);
			memcpy(buf3, blocks3, q);
		}
		blocks0 = (const uint8_t*)buf0;
		blocks1 = (const uint8_t*)buf1;
		blocks2 = (const uint8_t*)buf2;
		blocks3 = (const uint8_t*)buf3;
		q = 32;
	}

	uint64_t rot = size & 63;
	uint64_t rot_diff = 64 - rot;
	static const uint64_t const_mask_i = 01555555555555555555555ULL;
	static const uint64_t const_mask_j = 01333333333333333333333ULL;
	static const uint64_t const_mask_k = 00666666666666666666666ULL;

	__asm__ __volatile__ (
		/* Load seeds */
		"movq %[seed0], %%rax\n\t"
		"vmovq (%%rax), %%xmm9\n\t"
		"movq %[seed1], %%rax\n\t"
		"vmovhps (%%rax), %%xmm9, %%xmm9\n\t"
		"movq %[seed2], %%rax\n\t"
		"vmovq (%%rax), %%xmm13\n\t"
		"movq %[seed3], %%rax\n\t"
		"vmovhps (%%rax), %%xmm13, %%xmm13\n\t"
		"vinserti128 $1, %%xmm13, %%ymm9, %%ymm9\n\t" /* ymm9 = seed_a */

		"movq %[seed0], %%rax\n\t"
		"vmovq 8(%%rax), %%xmm10\n\t"
		"movq %[seed1], %%rax\n\t"
		"vmovhps 8(%%rax), %%xmm10, %%xmm10\n\t"
		"movq %[seed2], %%rax\n\t"
		"vmovq 8(%%rax), %%xmm13\n\t"
		"movq %[seed3], %%rax\n\t"
		"vmovhps 8(%%rax), %%xmm13, %%xmm13\n\t"
		"vinserti128 $1, %%xmm13, %%ymm10, %%ymm10\n\t" /* ymm10 = seed_b */

		/* Load masks */
		"vmovq %[mask_i], %%xmm12\n\t"
		"vpunpcklqdq %%xmm12, %%xmm12, %%xmm12\n\t"
		"vinserti128 $1, %%xmm12, %%ymm12, %%ymm12\n\t"

		"vmovq %[mask_j], %%xmm13\n\t"
		"vpunpcklqdq %%xmm13, %%xmm13, %%xmm13\n\t"
		"vinserti128 $1, %%xmm13, %%ymm13, %%ymm13\n\t"

		"vmovq %[mask_k], %%xmm14\n\t"
		"vpunpcklqdq %%xmm14, %%xmm14, %%xmm14\n\t"
		"vinserti128 $1, %%xmm14, %%ymm14, %%ymm14\n\t"

		/* Initialize states */
		"leaq %[const_ptr], %%rax\n\t"

		/* state0 */
		"vmovq 0*8(%%rax), %%xmm0\n\t"
		"vpunpcklqdq %%xmm0, %%xmm0, %%xmm0\n\t"
		"vinserti128 $1, %%xmm0, %%ymm0, %%ymm0\n\t"
		"vpand %%ymm12, %%ymm9, %%ymm15\n\t"
		"vpxor %%ymm15, %%ymm0, %%ymm0\n\t"

		/* state1 */
		"vmovq 1*8(%%rax), %%xmm1\n\t"
		"vpunpcklqdq %%xmm1, %%xmm1, %%xmm1\n\t"
		"vinserti128 $1, %%xmm1, %%ymm1, %%ymm1\n\t"
		"vpand %%ymm13, %%ymm10, %%ymm15\n\t"
		"vpxor %%ymm15, %%ymm1, %%ymm1\n\t"

		/* state2 */
		"vmovq 2*8(%%rax), %%xmm2\n\t"
		"vpunpcklqdq %%xmm2, %%xmm2, %%xmm2\n\t"
		"vinserti128 $1, %%xmm2, %%ymm2, %%ymm2\n\t"
		"vpand %%ymm14, %%ymm9, %%ymm15\n\t"
		"vpxor %%ymm15, %%ymm2, %%ymm2\n\t"

		/* state3 */
		"vmovq 3*8(%%rax), %%xmm3\n\t"
		"vpunpcklqdq %%xmm3, %%xmm3, %%xmm3\n\t"
		"vinserti128 $1, %%xmm3, %%ymm3, %%ymm3\n\t"
		"vpand %%ymm12, %%ymm10, %%ymm15\n\t"
		"vpxor %%ymm15, %%ymm3, %%ymm3\n\t"

		/* state4 */
		"vmovq 4*8(%%rax), %%xmm4\n\t"
		"vpunpcklqdq %%xmm4, %%xmm4, %%xmm4\n\t"
		"vinserti128 $1, %%xmm4, %%ymm4, %%ymm4\n\t"
		"vpand %%ymm13, %%ymm9, %%ymm15\n\t"
		"vpxor %%ymm15, %%ymm4, %%ymm4\n\t"

		/* state5 */
		"vmovq 5*8(%%rax), %%xmm5\n\t"
		"vpunpcklqdq %%xmm5, %%xmm5, %%xmm5\n\t"
		"vinserti128 $1, %%xmm5, %%ymm5, %%ymm5\n\t"
		"vpand %%ymm14, %%ymm10, %%ymm15\n\t"
		"vpxor %%ymm15, %%ymm5, %%ymm5\n\t"

		/* lo5 and hi5 */
		"vmovq 6*8(%%rax), %%xmm6\n\t"
		"vpunpcklqdq %%xmm6, %%xmm6, %%xmm6\n\t"
		"vinserti128 $1, %%xmm6, %%ymm6, %%ymm6\n\t"
		"vmovdqa %%ymm6, %%ymm7\n\t"

		/* Conditional loop */
		"cmp $96, %[q]\n\t"
		"jbe 3f\n\t"

		"1:\n\t"
		/* Loop step idx = 0 */
		AVX_LOAD(0, "%%ymm15")
		"vpxor %%ymm15, %%ymm0, %%ymm0\n\t"
		AVX_LOAD(1, "%%ymm15")
		"vpxor %%ymm15, %%ymm1, %%ymm1\n\t"
		AVX_MULT("%%ymm0", "%%ymm1", "%%ymm8", "%%ymm9")
		"vpxor %%ymm9, %%ymm6, %%ymm0\n\t"
		"vmovdqa %%ymm8, %%ymm10\n\t"

		/* Loop step idx = 1 */
		AVX_LOAD(2, "%%ymm15")
		"vpxor %%ymm15, %%ymm1, %%ymm1\n\t"
		AVX_LOAD(3, "%%ymm15")
		"vpxor %%ymm15, %%ymm2, %%ymm2\n\t"
		AVX_MULT("%%ymm1", "%%ymm2", "%%ymm8", "%%ymm9")
		"vpxor %%ymm9, %%ymm10, %%ymm1\n\t"
		"vmovdqa %%ymm8, %%ymm10\n\t"

		/* Loop step idx = 2 */
		AVX_LOAD(4, "%%ymm15")
		"vpxor %%ymm15, %%ymm2, %%ymm2\n\t"
		AVX_LOAD(5, "%%ymm15")
		"vpxor %%ymm15, %%ymm3, %%ymm3\n\t"
		AVX_MULT("%%ymm2", "%%ymm3", "%%ymm8", "%%ymm9")
		"vpxor %%ymm9, %%ymm10, %%ymm2\n\t"
		"vmovdqa %%ymm8, %%ymm10\n\t"

		/* Loop step idx = 3 */
		AVX_LOAD(6, "%%ymm15")
		"vpxor %%ymm15, %%ymm3, %%ymm3\n\t"
		AVX_LOAD(7, "%%ymm15")
		"vpxor %%ymm15, %%ymm4, %%ymm4\n\t"
		AVX_MULT("%%ymm3", "%%ymm4", "%%ymm8", "%%ymm9")
		"vpxor %%ymm9, %%ymm10, %%ymm3\n\t"
		"vmovdqa %%ymm8, %%ymm10\n\t"

		/* Loop step idx = 4 */
		AVX_LOAD(8, "%%ymm15")
		"vpxor %%ymm15, %%ymm4, %%ymm4\n\t"
		AVX_LOAD(9, "%%ymm15")
		"vpxor %%ymm15, %%ymm5, %%ymm5\n\t"
		AVX_MULT("%%ymm4", "%%ymm5", "%%ymm8", "%%ymm9")
		"vpxor %%ymm9, %%ymm10, %%ymm4\n\t"
		"vmovdqa %%ymm8, %%ymm10\n\t"

		/* Loop step idx = 5 */
		AVX_LOAD(10, "%%ymm15")
		"vpxor %%ymm15, %%ymm5, %%ymm5\n\t"
		AVX_LOAD(11, "%%ymm15")
		"vpxor %%ymm15, %%ymm0, %%ymm0\n\t"
		AVX_MULT("%%ymm5", "%%ymm0", "%%ymm8", "%%ymm9")
		"vpxor %%ymm9, %%ymm10, %%ymm5\n\t"
		"vmovdqa %%ymm8, %%ymm6\n\t"
		"vmovdqa %%ymm9, %%ymm7\n\t"

		"add $96, %[blocks0]\n\t"
		"add $96, %[blocks1]\n\t"
		"add $96, %[blocks2]\n\t"
		"add $96, %[blocks3]\n\t"
		"sub $96, %[q]\n\t"
		"cmp $96, %[q]\n\t"
		"ja 1b\n\t"

		"vpxor %%ymm6, %%ymm0, %%ymm0\n\t"
		"3:\n\t"

		/* Tail */
		"vmovdqa %%ymm1, %%ymm10\n\t" /* xor0 = state1 */
		"vmovdqa %%ymm2, %%ymm11\n\t" /* xor1 = state2 */
		"vmovdqa %%ymm3, %%ymm12\n\t" /* xor2 = state3 */
		"vmovdqa %%ymm4, %%ymm13\n\t" /* xor3 = state4 */

		"cmp $32, %[q]\n\t"
		"jbe 4f\n\t"

		AVX_LOAD(0, "%%ymm15")
		"vpxor %%ymm15, %%ymm0, %%ymm0\n\t"
		AVX_LOAD(1, "%%ymm15")
		"vpxor %%ymm15, %%ymm1, %%ymm1\n\t"
		AVX_MULT("%%ymm0", "%%ymm1", "%%ymm8", "%%ymm9")
		"vpxor %%ymm9, %%ymm8, %%ymm10\n\t"

		"cmp $48, %[q]\n\t"
		"jbe 4f\n\t"

		AVX_LOAD(2, "%%ymm15")
		"vpxor %%ymm15, %%ymm1, %%ymm1\n\t"
		AVX_LOAD(3, "%%ymm15")
		"vpxor %%ymm15, %%ymm2, %%ymm2\n\t"
		AVX_MULT("%%ymm1", "%%ymm2", "%%ymm8", "%%ymm9")
		"vpxor %%ymm9, %%ymm8, %%ymm11\n\t"

		"cmp $64, %[q]\n\t"
		"jbe 4f\n\t"

		AVX_LOAD(4, "%%ymm15")
		"vpxor %%ymm15, %%ymm2, %%ymm2\n\t"
		AVX_LOAD(5, "%%ymm15")
		"vpxor %%ymm15, %%ymm3, %%ymm3\n\t"
		AVX_MULT("%%ymm2", "%%ymm3", "%%ymm8", "%%ymm9")
		"vpxor %%ymm9, %%ymm8, %%ymm12\n\t"

		"cmp $80, %[q]\n\t"
		"jbe 4f\n\t"

		AVX_LOAD(6, "%%ymm15")
		"vpxor %%ymm15, %%ymm3, %%ymm3\n\t"
		AVX_LOAD(7, "%%ymm15")
		"vpxor %%ymm15, %%ymm4, %%ymm4\n\t"
		AVX_MULT("%%ymm3", "%%ymm4", "%%ymm8", "%%ymm9")
		"vpxor %%ymm9, %%ymm8, %%ymm13\n\t"

		"4:\n\t"

		/* Compute end pointers */
		"movq %[blocks0], %%r8\n\t"
		"add %[q], %%r8\n\t"
		"movq %[blocks1], %%r9\n\t"
		"add %[q], %%r9\n\t"
		"movq %[blocks2], %%r10\n\t"
		"add %[q], %%r10\n\t"
		"movq %[blocks3], %%r11\n\t"
		"add %[q], %%r11\n\t"

		/* Unconditional tail step 1 */
		AVX_LOAD_NEG(32, "%%ymm15")
		"vpxor %%ymm15, %%ymm4, %%ymm4\n\t"
		AVX_LOAD_NEG(24, "%%ymm15")
		"vpxor %%ymm15, %%ymm5, %%ymm5\n\t"
		AVX_MULT("%%ymm4", "%%ymm5", "%%ymm8", "%%ymm9")
		"vpxor %%ymm9, %%ymm8, %%ymm14\n\t"

		/* Unconditional tail step 2 */
		AVX_LOAD_NEG(16, "%%ymm15")
		"vpxor %%ymm15, %%ymm5, %%ymm5\n\t"
		AVX_LOAD_NEG(8, "%%ymm15")
		"vpxor %%ymm15, %%ymm0, %%ymm0\n\t"
		AVX_MULT("%%ymm5", "%%ymm0", "%%ymm8", "%%ymm9")
		"vpxor %%ymm9, %%ymm8, %%ymm15\n\t"

		/* Subtract states to compute i, j, k */
		"vpsubq %%ymm1, %%ymm0, %%ymm0\n\t"
		"leaq %[const_ptr], %%rax\n\t"
		"vmovq 7*8(%%rax), %%xmm8\n\t"
		"vpunpcklqdq %%xmm8, %%xmm8, %%xmm8\n\t"
		"vinserti128 $1, %%xmm8, %%ymm8, %%ymm8\n\t"
		"vpxor %%ymm8, %%ymm0, %%ymm0\n\t" /* ymm0 = i */

		"vpsubq %%ymm3, %%ymm2, %%ymm2\n\t"
		"vmovq 8*8(%%rax), %%xmm8\n\t"
		"vpunpcklqdq %%xmm8, %%xmm8, %%xmm8\n\t"
		"vinserti128 $1, %%xmm8, %%ymm8, %%ymm8\n\t"
		"vpxor %%ymm8, %%ymm2, %%ymm2\n\t" /* ymm2 = j */

		"vpsubq %%ymm5, %%ymm4, %%ymm4\n\t"
		"vmovq 9*8(%%rax), %%xmm8\n\t"
		"vpunpcklqdq %%xmm8, %%xmm8, %%xmm8\n\t"
		"vinserti128 $1, %%xmm8, %%ymm8, %%ymm8\n\t"
		"vpxor %%ymm8, %%ymm4, %%ymm4\n\t" /* ymm4 = k */

		/* Rotations and size subtraction */
		"vmovq %[rot], %%xmm8\n\t"
		"vmovq %[rot_diff], %%xmm9\n\t"

		"vpsllq %%xmm8, %%ymm0, %%ymm1\n\t"
		"vpsrlq %%xmm9, %%ymm0, %%ymm0\n\t"
		"vpor %%ymm0, %%ymm1, %%ymm0\n\t"

		"vpsrlq %%xmm8, %%ymm2, %%ymm3\n\t"
		"vpsllq %%xmm9, %%ymm2, %%ymm2\n\t"
		"vpor %%ymm2, %%ymm3, %%ymm2\n\t"

		"vmovq %[size], %%xmm8\n\t"
		"vpunpcklqdq %%xmm8, %%xmm8, %%xmm8\n\t"
		"vinserti128 $1, %%xmm8, %%ymm8, %%ymm8\n\t"
		"vpsubq %%ymm8, %%ymm4, %%ymm4\n\t"

		/* i, j, k corrections */
		"vpsubq %%ymm13, %%ymm0, %%ymm0\n\t"
		"vpsubq %%ymm14, %%ymm0, %%ymm0\n\t"

		"vpsubq %%ymm15, %%ymm2, %%ymm2\n\t"
		"vpsubq %%ymm10, %%ymm2, %%ymm2\n\t"

		"vpsubq %%ymm11, %%ymm4, %%ymm4\n\t"
		"vpsubq %%ymm12, %%ymm4, %%ymm4\n\t"

		/* Final multiplications */
		AVX_MULT("%%ymm0", "%%ymm2", "%%ymm10", "%%ymm11")
		AVX_MULT("%%ymm2", "%%ymm4", "%%ymm12", "%%ymm13")
		AVX_MULT("%%ymm4", "%%ymm0", "%%ymm8", "%%ymm9")

		"vpxor %%ymm11, %%ymm8, %%ymm0\n\t"
		"vpxor %%ymm13, %%ymm10, %%ymm2\n\t"
		"vpxor %%ymm9, %%ymm12, %%ymm4\n\t"

		/* Multiply by constants 10, 11, 12 */
		"vmovq 10*8(%%rax), %%xmm8\n\t"
		"vpunpcklqdq %%xmm8, %%xmm8, %%xmm8\n\t"
		"vinserti128 $1, %%xmm8, %%ymm8, %%ymm8\n\t"
		AVX_MULT("%%ymm0", "%%ymm8", "%%ymm10", "%%ymm11")

		"vmovq 11*8(%%rax), %%xmm8\n\t"
		"vpunpcklqdq %%xmm8, %%xmm8, %%xmm8\n\t"
		"vinserti128 $1, %%xmm8, %%ymm8, %%ymm8\n\t"
		AVX_MULT("%%ymm2", "%%ymm8", "%%ymm12", "%%ymm13")

		"vmovq 12*8(%%rax), %%xmm8\n\t"
		"vpunpcklqdq %%xmm8, %%xmm8, %%xmm8\n\t"
		"vinserti128 $1, %%xmm8, %%ymm8, %%ymm8\n\t"
		AVX_MULT("%%ymm4", "%%ymm8", "%%ymm6", "%%ymm7")

		/* Final digests XOR */
		"vpxor %%ymm13, %%ymm10, %%ymm10\n\t"
		"vpxor %%ymm6, %%ymm10, %%ymm10\n\t"

		"vpxor %%ymm12, %%ymm11, %%ymm11\n\t"
		"vpxor %%ymm7, %%ymm11, %%ymm11\n\t"

		/* Extract high parts of digests */
		"vextracti128 $1, %%ymm10, %%xmm12\n\t"
		"vextracti128 $1, %%ymm11, %%xmm13\n\t"

		/* Store digest0 */
		"movq %[digest0], %%rax\n\t"
		"vmovq %%xmm10, (%%rax)\n\t"
		"vmovq %%xmm11, 8(%%rax)\n\t"

		/* Store digest1 */
		"movq %[digest1], %%rcx\n\t"
		"vmovhps %%xmm10, (%%rcx)\n\t"
		"vmovhps %%xmm11, 8(%%rcx)\n\t"

		/* Store digest2 */
		"movq %[digest2], %%rdx\n\t"
		"vmovq %%xmm12, (%%rdx)\n\t"
		"vmovq %%xmm13, 8(%%rdx)\n\t"

		/* Store digest3 */
		"movq %[digest3], %%rsi\n\t"
		"vmovhps %%xmm12, (%%rsi)\n\t"
		"vmovhps %%xmm13, 8(%%rsi)\n\t"

		: [blocks0] "+r" (blocks0),
		[blocks1] "+r" (blocks1),
		[blocks2] "+r" (blocks2),
		[blocks3] "+r" (blocks3),
		[q] "+r" (q)
		: [seed0] "m" (seed0),
		[seed1] "m" (seed1),
		[seed2] "m" (seed2),
		[seed3] "m" (seed3),
		[digest0] "m" (digest0),
		[digest1] "m" (digest1),
		[digest2] "m" (digest2),
		[digest3] "m" (digest3),
		[const_ptr] "m" (MUSEAIR_CONSTANT),
		[mask_i] "m" (const_mask_i),
		[mask_j] "m" (const_mask_j),
		[mask_k] "m" (const_mask_k),
		[rot] "m" (rot),
		[rot_diff] "m" (rot_diff),
		[size] "m" (size)
		: "rax", "rcx", "rdx", "rsi", "r8", "r9", "r10", "r11",
		"xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",
		"xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15", "cc", "memory"
	);
}

#else /* !__x86_64__ */

void MuseAirLoongAVX(
	const void* data0, const void* data1, const void* data2, const void* data3,
	size_t size,
	const uint8_t* seed0, const uint8_t* seed1, const uint8_t* seed2, const uint8_t* seed3,
	uint8_t* digest0, uint8_t* digest1, uint8_t* digest2, uint8_t* digest3
)
{
	MuseAirLoong(data0, size, seed0, digest0);
	MuseAirLoong(data1, size, seed1, digest1);
	MuseAirLoong(data2, size, seed2, digest2);
	MuseAirLoong(data3, size, seed3, digest3);
}

#endif /* __x86_64__ */

