// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Andrea Mazzoleni

#if defined(__x86_64__)

#define SSE_LOAD(offset, dest) \
	"movq " #offset "*8(%[blocks0]), " dest "\n\t" \
	"movhps " #offset "*8(%[blocks1]), " dest "\n\t"

#define SSE_LOAD_NEG(offset, dest) \
	"movq -" #offset "(%%r8), " dest "\n\t" \
	"movhps -" #offset "(%%r9), " dest "\n\t"

#define SSE_MULT(a, b, lo, hi, s0, s1) \
	/* 1. Compute B_h in s0 */ \
	"movdqa " b ", " s0 "\n\t" \
	"psrlq $32, " s0 "\n\t" \
	/* 2. Compute P_lh in s1 */ \
	"movdqa " a ", " s1 "\n\t" \
	"pmuludq " s0 ", " s1 "\n\t" \
	/* 3. Compute A_h in hi */ \
	"movdqa " a ", " hi "\n\t" \
	"psrlq $32, " hi "\n\t" \
	/* 4. Compute P_hh in lo */ \
	"movdqa " hi ", " lo "\n\t" \
	"pmuludq " s0 ", " lo "\n\t" \
	/* 5. Compute P_hl in hi */ \
	"pmuludq " b ", " hi "\n\t" \
	/* 6. Compute P_ll in s0 */ \
	"movdqa " a ", " s0 "\n\t" \
	"pmuludq " b ", " s0 "\n\t" \
	/* 7. M = P_lh + P_hl */ \
	"paddq " hi ", " s1 "\n\t" /* s1 = M */ \
	/* 8. Move P_hh from lo to hi */ \
	"movdqa " lo ", " hi "\n\t" /* hi = P_hh */ \
	/* 9. Generate mask 0xffffffff in xmm15 */ \
	"pcmpeqd %%xmm15, %%xmm15\n\t" \
	"psrlq $32, %%xmm15\n\t" \
	/* 10. Split M into s1 = M_low, lo = M_high */ \
	"movdqa " s1 ", " lo "\n\t" \
	"pand %%xmm15, " s1 "\n\t" /* s1 = M_low */ \
	"psrlq $32, " lo "\n\t" /* lo = M_high */ \
	/* 11. Split P_ll (in s0) into s0 = P_lll, xmm15 = P_llh (overwriting mask) */ \
	"movdqa " s0 ", %%xmm15\n\t" \
	"psrlq $32, %%xmm15\n\t" /* xmm15 = P_llh */ \
	"pand %%xmm15, " s0 "\n\t" /* s0 = P_lll */ /* Wait! xmm15 contains P_llh which has 0 in high 32 bits, but low 32 bits is P_llh, not 0xffffffff! */ \
	/* Ah! We cannot pand with xmm15 because xmm15 is NOT the mask anymore! */ \
	/* To fix this, let's just use the shift method for s0! */ \
	/* "psllq $32, s0; psrlq $32, s0" is safer! */ \

#define SSE_MULT_FIXED(a, b, lo, hi, s0, s1) \
	/* 1. Compute B_h in s0 */ \
	"movdqa " b ", " s0 "\n\t" \
	"psrlq $32, " s0 "\n\t" \
	/* 2. Compute P_lh in s1 */ \
	"movdqa " a ", " s1 "\n\t" \
	"pmuludq " s0 ", " s1 "\n\t" \
	/* 3. Compute A_h in hi */ \
	"movdqa " a ", " hi "\n\t" \
	"psrlq $32, " hi "\n\t" \
	/* 4. Compute P_hh in lo */ \
	"movdqa " hi ", " lo "\n\t" \
	"pmuludq " s0 ", " lo "\n\t" \
	/* 5. Compute P_hl in hi */ \
	"pmuludq " b ", " hi "\n\t" \
	/* 6. Compute P_ll in s0 */ \
	"movdqa " a ", " s0 "\n\t" \
	"pmuludq " b ", " s0 "\n\t" \
	/* 7. M = P_lh + P_hl */ \
	"paddq " hi ", " s1 "\n\t" /* s1 = M */ \
	/* 8. Move P_hh from lo to hi */ \
	"movdqa " lo ", " hi "\n\t" /* hi = P_hh */ \
	/* 9. Generate mask 0xffffffff in xmm15 */ \
	"pcmpeqd %%xmm15, %%xmm15\n\t" \
	"psrlq $32, %%xmm15\n\t" \
	/* 10. Split M into s1 = M_low, lo = M_high */ \
	"movdqa " s1 ", " lo "\n\t" \
	"pand %%xmm15, " s1 "\n\t" /* s1 = M_low */ \
	"psrlq $32, " lo "\n\t" /* lo = M_high */ \
	/* 11. Split P_ll (in s0) into s0 = P_lll, xmm15 = P_llh (overwriting mask) */ \
	"movdqa " s0 ", %%xmm15\n\t" \
	"psrlq $32, %%xmm15\n\t" /* xmm15 = P_llh */ \
	"psllq $32, " s0 "\n\t" \
	"psrlq $32, " s0 "\n\t" /* s0 = P_lll */ \
	/* 12. Compute S1 = P_llh + M_low */ \
	"paddq " s1 ", %%xmm15\n\t" /* xmm15 = S1 */ \
	/* 13. Split S1 into s1 = C1, xmm15 = S1_l */ \
	"movdqa %%xmm15, " s1 "\n\t" \
	"psllq $32, %%xmm15\n\t" \
	"psrlq $32, %%xmm15\n\t" /* xmm15 = S1_l */ \
	"psrlq $32, " s1 "\n\t" /* s1 = C1 */ \
	/* 14. Assemble final low (in lo) */ \
	"psllq $32, %%xmm15\n\t" \
	"paddq " s0 ", %%xmm15\n\t" \
	"movdqa %%xmm15, " lo "\n\t" /* lo = final low */ \
	/* 15. Split P_hh (in hi) into s0 = P_hhl, hi = P_hhh */ \
	"movdqa " hi ", " s0 "\n\t" \
	"psllq $32, " s0 "\n\t" \
	"psrlq $32, " s0 "\n\t" /* s0 = P_hhl */ \
	"psrlq $32, " hi "\n\t" /* hi = P_hhh */ \
	/* 16. Compute final high (in hi) */ \
	"paddq " lo ", " s0 "\n\t" /* s0 = M_high + P_hhl */ \
	"paddq " s1 ", " s0 "\n\t" /* s0 = M_high + P_hhl + C1 */ \
	"psllq $32, " hi "\n\t" \
	"paddq " s0 ", " hi "\n\t" /* hi = final high */

void MuseAirLoongSSE(
	const void* data0, const void* data1,
	size_t size,
	const uint8_t* seed0, const uint8_t* seed1,
	uint8_t* digest0, uint8_t* digest1
)
{
	uint64_t buf0[12];
	uint64_t buf1[12];
	const uint8_t* blocks0 = data0;
	const uint8_t* blocks1 = data1;
	size_t q = size;

	if (q < 32) {
		memset(buf0, 0, 32);
		memset(buf1, 0, 32);
		if (q > 0) {
			memcpy(buf0, blocks0, q);
			memcpy(buf1, blocks1, q);
		}
		blocks0 = (const uint8_t*)buf0;
		blocks1 = (const uint8_t*)buf1;
		q = 32;
	}

	uint64_t rot = size & 63;
	uint64_t rot_diff = 64 - rot;
	static const uint64_t const_mask_i = 01555555555555555555555ULL;
	static const uint64_t const_mask_j = 01333333333333333333333ULL;
	static const uint64_t const_mask_k = 00666666666666666666666ULL;

	__asm__ __volatile__ (
		/* Load seed0 and seed1 */
		"movq %[seed0], %%rax\n\t"
		"movq (%%rax), %%xmm9\n\t"
		"movq %[seed1], %%rcx\n\t"
		"movhps (%%rcx), %%xmm9\n\t" /* xmm9 = seed_a */

		"movq %[seed0], %%rax\n\t"
		"movq 8(%%rax), %%xmm10\n\t"
		"movq %[seed1], %%rcx\n\t"
		"movhps 8(%%rcx), %%xmm10\n\t" /* xmm10 = seed_b */

		/* Load and duplicate masks */
		"movq %[mask_i], %%xmm12\n\t"
		"punpcklqdq %%xmm12, %%xmm12\n\t"

		"movq %[mask_j], %%xmm13\n\t"
		"punpcklqdq %%xmm13, %%xmm13\n\t"

		"movq %[mask_k], %%xmm14\n\t"
		"punpcklqdq %%xmm14, %%xmm14\n\t"

		/* Initialize states */
		"leaq %[const_ptr], %%rax\n\t"

		/* state0 */
		"movq 0*8(%%rax), %%xmm0\n\t"
		"punpcklqdq %%xmm0, %%xmm0\n\t"
		"movdqa %%xmm9, %%xmm15\n\t"
		"pand %%xmm12, %%xmm15\n\t"
		"pxor %%xmm15, %%xmm0\n\t"

		/* state1 */
		"movq 1*8(%%rax), %%xmm1\n\t"
		"punpcklqdq %%xmm1, %%xmm1\n\t"
		"movdqa %%xmm10, %%xmm15\n\t"
		"pand %%xmm13, %%xmm15\n\t"
		"pxor %%xmm15, %%xmm1\n\t"

		/* state2 */
		"movq 2*8(%%rax), %%xmm2\n\t"
		"punpcklqdq %%xmm2, %%xmm2\n\t"
		"movdqa %%xmm9, %%xmm15\n\t"
		"pand %%xmm14, %%xmm15\n\t"
		"pxor %%xmm15, %%xmm2\n\t"

		/* state3 */
		"movq 3*8(%%rax), %%xmm3\n\t"
		"punpcklqdq %%xmm3, %%xmm3\n\t"
		"movdqa %%xmm10, %%xmm15\n\t"
		"pand %%xmm12, %%xmm15\n\t"
		"pxor %%xmm15, %%xmm3\n\t"

		/* state4 */
		"movq 4*8(%%rax), %%xmm4\n\t"
		"punpcklqdq %%xmm4, %%xmm4\n\t"
		"movdqa %%xmm9, %%xmm15\n\t"
		"pand %%xmm13, %%xmm15\n\t"
		"pxor %%xmm15, %%xmm4\n\t"

		/* state5 */
		"movq 5*8(%%rax), %%xmm5\n\t"
		"punpcklqdq %%xmm5, %%xmm5\n\t"
		"movdqa %%xmm10, %%xmm15\n\t"
		"pand %%xmm14, %%xmm15\n\t"
		"pxor %%xmm15, %%xmm5\n\t"

		/* lo5 and hi5 */
		"movq 6*8(%%rax), %%xmm6\n\t"
		"punpcklqdq %%xmm6, %%xmm6\n\t"
		"movdqa %%xmm6, %%xmm7\n\t"

		/* Conditional loop */
		"cmp $96, %[q]\n\t"
		"jbe 3f\n\t"

		"1:\n\t"
		/* Loop step idx = 0 */
		SSE_LOAD(0, "%%xmm15")
		"pxor %%xmm15, %%xmm0\n\t"
		SSE_LOAD(1, "%%xmm15")
		"pxor %%xmm15, %%xmm1\n\t"
		SSE_MULT_FIXED("%%xmm0", "%%xmm1", "%%xmm8", "%%xmm9", "%%xmm10", "%%xmm11")
		"movdqa %%xmm6, %%xmm0\n\t"
		"pxor %%xmm9, %%xmm0\n\t"
		"movdqa %%xmm8, %%xmm10\n\t"

		/* Loop step idx = 1 */
		SSE_LOAD(2, "%%xmm15")
		"pxor %%xmm15, %%xmm1\n\t"
		SSE_LOAD(3, "%%xmm15")
		"pxor %%xmm15, %%xmm2\n\t"
		SSE_MULT_FIXED("%%xmm1", "%%xmm2", "%%xmm8", "%%xmm9", "%%xmm11", "%%xmm15")
		"movdqa %%xmm10, %%xmm1\n\t"
		"pxor %%xmm9, %%xmm1\n\t"
		"movdqa %%xmm8, %%xmm10\n\t"

		/* Loop step idx = 2 */
		SSE_LOAD(4, "%%xmm15")
		"pxor %%xmm15, %%xmm2\n\t"
		SSE_LOAD(5, "%%xmm15")
		"pxor %%xmm15, %%xmm3\n\t"
		SSE_MULT_FIXED("%%xmm2", "%%xmm3", "%%xmm8", "%%xmm9", "%%xmm11", "%%xmm15")
		"movdqa %%xmm10, %%xmm2\n\t"
		"pxor %%xmm9, %%xmm2\n\t"
		"movdqa %%xmm8, %%xmm10\n\t"

		/* Loop step idx = 3 */
		SSE_LOAD(6, "%%xmm15")
		"pxor %%xmm15, %%xmm3\n\t"
		SSE_LOAD(7, "%%xmm15")
		"pxor %%xmm15, %%xmm4\n\t"
		SSE_MULT_FIXED("%%xmm3", "%%xmm4", "%%xmm8", "%%xmm9", "%%xmm11", "%%xmm15")
		"movdqa %%xmm10, %%xmm3\n\t"
		"pxor %%xmm9, %%xmm3\n\t"
		"movdqa %%xmm8, %%xmm10\n\t"

		/* Loop step idx = 4 */
		SSE_LOAD(8, "%%xmm15")
		"pxor %%xmm15, %%xmm4\n\t"
		SSE_LOAD(9, "%%xmm15")
		"pxor %%xmm15, %%xmm5\n\t"
		SSE_MULT_FIXED("%%xmm4", "%%xmm5", "%%xmm8", "%%xmm9", "%%xmm11", "%%xmm15")
		"movdqa %%xmm10, %%xmm4\n\t"
		"pxor %%xmm9, %%xmm4\n\t"
		"movdqa %%xmm8, %%xmm10\n\t"

		/* Loop step idx = 5 */
		SSE_LOAD(10, "%%xmm15")
		"pxor %%xmm15, %%xmm5\n\t"
		SSE_LOAD(11, "%%xmm15")
		"pxor %%xmm15, %%xmm0\n\t"
		SSE_MULT_FIXED("%%xmm5", "%%xmm0", "%%xmm8", "%%xmm9", "%%xmm11", "%%xmm15")
		"movdqa %%xmm10, %%xmm5\n\t"
		"pxor %%xmm9, %%xmm5\n\t"
		"movdqa %%xmm8, %%xmm6\n\t"
		"movdqa %%xmm9, %%xmm7\n\t"

		"add $96, %[blocks0]\n\t"
		"add $96, %[blocks1]\n\t"
		"sub $96, %[q]\n\t"
		"cmp $96, %[q]\n\t"
		"ja 1b\n\t"

		"pxor %%xmm6, %%xmm0\n\t"
		"3:\n\t"

		/* Tail */
		"movdqa %%xmm1, %%xmm10\n\t" /* xor0 = state1 */
		"movdqa %%xmm2, %%xmm11\n\t" /* xor1 = state2 */
		"movdqa %%xmm3, %%xmm12\n\t" /* xor2 = state3 */
		"movdqa %%xmm4, %%xmm13\n\t" /* xor3 = state4 */

		"cmp $32, %[q]\n\t"
		"jbe 4f\n\t"

		SSE_LOAD(0, "%%xmm15")
		"pxor %%xmm15, %%xmm0\n\t"
		SSE_LOAD(1, "%%xmm15")
		"pxor %%xmm15, %%xmm1\n\t"
		SSE_MULT_FIXED("%%xmm0", "%%xmm1", "%%xmm8", "%%xmm9", "%%xmm14", "%%xmm15")
		"movdqa %%xmm8, %%xmm10\n\t"
		"pxor %%xmm9, %%xmm10\n\t"

		"cmp $48, %[q]\n\t"
		"jbe 4f\n\t"

		SSE_LOAD(2, "%%xmm15")
		"pxor %%xmm15, %%xmm1\n\t"
		SSE_LOAD(3, "%%xmm15")
		"pxor %%xmm15, %%xmm2\n\t"
		SSE_MULT_FIXED("%%xmm1", "%%xmm2", "%%xmm8", "%%xmm9", "%%xmm14", "%%xmm15")
		"movdqa %%xmm8, %%xmm11\n\t"
		"pxor %%xmm9, %%xmm11\n\t"

		"cmp $64, %[q]\n\t"
		"jbe 4f\n\t"

		SSE_LOAD(4, "%%xmm15")
		"pxor %%xmm15, %%xmm2\n\t"
		SSE_LOAD(5, "%%xmm15")
		"pxor %%xmm15, %%xmm3\n\t"
		SSE_MULT_FIXED("%%xmm2", "%%xmm3", "%%xmm8", "%%xmm9", "%%xmm14", "%%xmm15")
		"movdqa %%xmm8, %%xmm12\n\t"
		"pxor %%xmm9, %%xmm12\n\t"

		"cmp $80, %[q]\n\t"
		"jbe 4f\n\t"

		SSE_LOAD(6, "%%xmm15")
		"pxor %%xmm15, %%xmm3\n\t"
		SSE_LOAD(7, "%%xmm15")
		"pxor %%xmm15, %%xmm4\n\t"
		SSE_MULT_FIXED("%%xmm3", "%%xmm4", "%%xmm8", "%%xmm9", "%%xmm14", "%%xmm15")
		"movdqa %%xmm8, %%xmm13\n\t"
		"pxor %%xmm9, %%xmm13\n\t"

		"4:\n\t"

		/* Compute end pointers */
		"movq %[blocks0], %%r8\n\t"
		"add %[q], %%r8\n\t"
		"movq %[blocks1], %%r9\n\t"
		"add %[q], %%r9\n\t"

		/* Unconditional tail step 1 */
		SSE_LOAD_NEG(32, "%%xmm15")
		"pxor %%xmm15, %%xmm4\n\t"
		SSE_LOAD_NEG(24, "%%xmm15")
		"pxor %%xmm15, %%xmm5\n\t"
		SSE_MULT_FIXED("%%xmm4", "%%xmm5", "%%xmm8", "%%xmm9", "%%xmm14", "%%xmm15")
		"movdqa %%xmm8, %%xmm14\n\t"
		"pxor %%xmm9, %%xmm14\n\t"

		/* Unconditional tail step 2 */
		SSE_LOAD_NEG(16, "%%xmm15")
		"pxor %%xmm15, %%xmm5\n\t"
		SSE_LOAD_NEG(8, "%%xmm15")
		"pxor %%xmm15, %%xmm0\n\t"
		SSE_MULT_FIXED("%%xmm5", "%%xmm0", "%%xmm8", "%%xmm9", "%%xmm12", "%%xmm15")
		"movdqa %%xmm8, %%xmm15\n\t"
		"pxor %%xmm9, %%xmm15\n\t"

		/* Subtract states to compute i, j, k */
		"psubq %%xmm1, %%xmm0\n\t"
		"leaq %[const_ptr], %%rax\n\t"
		"movq 7*8(%%rax), %%xmm8\n\t"
		"punpcklqdq %%xmm8, %%xmm8\n\t"
		"pxor %%xmm8, %%xmm0\n\t" /* xmm0 = i */

		"psubq %%xmm3, %%xmm2\n\t"
		"movq 8*8(%%rax), %%xmm8\n\t"
		"punpcklqdq %%xmm8, %%xmm8\n\t"
		"pxor %%xmm8, %%xmm2\n\t" /* xmm2 = j */

		"psubq %%xmm5, %%xmm4\n\t"
		"movq 9*8(%%rax), %%xmm8\n\t"
		"punpcklqdq %%xmm8, %%xmm8\n\t"
		"pxor %%xmm8, %%xmm4\n\t" /* xmm4 = k */

		/* Rotations and size subtraction */
		"movq %[rot], %%xmm8\n\t"
		"movq %[rot_diff], %%xmm9\n\t"

		"movdqa %%xmm0, %%xmm1\n\t"
		"psllq %%xmm8, %%xmm0\n\t"
		"psrlq %%xmm9, %%xmm1\n\t"
		"por %%xmm1, %%xmm0\n\t"

		"movdqa %%xmm2, %%xmm3\n\t"
		"psrlq %%xmm8, %%xmm2\n\t"
		"psllq %%xmm9, %%xmm3\n\t"
		"por %%xmm3, %%xmm2\n\t"

		"movq %[size], %%xmm8\n\t"
		"punpcklqdq %%xmm8, %%xmm8\n\t"
		"psubq %%xmm8, %%xmm4\n\t"

		/* i, j, k corrections */
		"psubq %%xmm13, %%xmm0\n\t"
		"psubq %%xmm14, %%xmm0\n\t"

		"psubq %%xmm15, %%xmm2\n\t"
		"psubq %%xmm10, %%xmm2\n\t"

		"psubq %%xmm11, %%xmm4\n\t"
		"psubq %%xmm12, %%xmm4\n\t"

		/* Final multiplications */
		SSE_MULT_FIXED("%%xmm0", "%%xmm2", "%%xmm10", "%%xmm11", "%%xmm8", "%%xmm9")
		SSE_MULT_FIXED("%%xmm2", "%%xmm4", "%%xmm12", "%%xmm13", "%%xmm8", "%%xmm9")
		SSE_MULT_FIXED("%%xmm4", "%%xmm0", "%%xmm8", "%%xmm9", "%%xmm14", "%%xmm15")

		"movdqa %%xmm8, %%xmm0\n\t"
		"pxor %%xmm11, %%xmm0\n\t"

		"movdqa %%xmm10, %%xmm2\n\t"
		"pxor %%xmm13, %%xmm2\n\t"

		"movdqa %%xmm12, %%xmm4\n\t"
		"pxor %%xmm9, %%xmm4\n\t"

		/* Multiply by constants 10, 11, 12 */
		"leaq %[const_ptr], %%rax\n\t"
		"movq 10*8(%%rax), %%xmm8\n\t"
		"punpcklqdq %%xmm8, %%xmm8\n\t"
		SSE_MULT_FIXED("%%xmm0", "%%xmm8", "%%xmm10", "%%xmm11", "%%xmm12", "%%xmm13")

		"leaq %[const_ptr], %%rax\n\t"
		"movq 11*8(%%rax), %%xmm8\n\t"
		"punpcklqdq %%xmm8, %%xmm8\n\t"
		SSE_MULT_FIXED("%%xmm2", "%%xmm8", "%%xmm12", "%%xmm13", "%%xmm10", "%%xmm11")

		"leaq %[const_ptr], %%rax\n\t"
		"movq 12*8(%%rax), %%xmm8\n\t"
		"punpcklqdq %%xmm8, %%xmm8\n\t"
		SSE_MULT_FIXED("%%xmm4", "%%xmm8", "%%xmm6", "%%xmm7", "%%xmm10", "%%xmm11")

		/* Final digests XOR */
		"pxor %%xmm13, %%xmm10\n\t"
		"pxor %%xmm6, %%xmm10\n\t"

		"pxor %%xmm12, %%xmm11\n\t"
		"pxor %%xmm7, %%xmm11\n\t"

		/* Store back digests */
		"movq %[digest0], %%rax\n\t"
		"movq %%xmm10, (%%rax)\n\t"
		"movq %%xmm11, 8(%%rax)\n\t"

		"movq %[digest1], %%rcx\n\t"
		"movhps %%xmm10, (%%rcx)\n\t"
		"movhps %%xmm11, 8(%%rcx)\n\t"

		: [blocks0] "+r" (blocks0),
		[blocks1] "+r" (blocks1),
		[q] "+r" (q)
		: [seed0] "m" (seed0),
		[seed1] "m" (seed1),
		[digest0] "m" (digest0),
		[digest1] "m" (digest1),
		[const_ptr] "m" (MUSEAIR_CONSTANT),
		[mask_i] "m" (const_mask_i),
		[mask_j] "m" (const_mask_j),
		[mask_k] "m" (const_mask_k),
		[rot] "m" (rot),
		[rot_diff] "m" (rot_diff),
		[size] "m" (size)
		: "rax", "rcx", "rdx", "r8", "r9",
		"xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",
		"xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15", "cc", "memory"
	);
}

#else /* !__x86_64__ */

void MuseAirLoongSSE(
	const void* data0, const void* data1,
	size_t size,
	const uint8_t* seed0, const uint8_t* seed1,
	uint8_t* digest0, uint8_t* digest1
)
{
	MuseAirLoong(data0, size, seed0, digest0);
	MuseAirLoong(data1, size, seed1, digest1);
}

#endif /* __x86_64__ */

