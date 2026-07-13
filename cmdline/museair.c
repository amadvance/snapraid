// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Andrea Mazzoleni

/*
 * Derivative work of MuseAir v1 in the "Long" (len > 32), "b128", and "bfast" configurations.
 *
 * This implementation also accepts a length <= 32, unlike the original MuseAir,
 * which switches to the short version for this length.
 *
 * https://github.com/eternal-io/museair
 */

/*
 * MuseAir v1
 * By K--Aethiax
 *
 * Released into the public domain under the CC0 1.0 license. To view a copy
 * of this license, visit: https://creativecommons.org/publicdomain/zero/1.0/
 *
 * Note that this file is used with SMHasher3, not a standalone implementation;
 * there isn't a C one because I don't want to deal with boilerplates in C yet.
 */

/*
 * Analysis of the BFast variant and blinding multiplication
 *
 * Many fast non-cryptographic hashes (wyhash, rapidhash) use "folding
 * multiplication": fold_mul(state ^ input_a, input_b ^ SECRET), where both
 * the state and a public constant are XORed directly with input words.
 * This is vulnerable to "blinding multiplication": if any operand becomes
 * zero, the product is zero, and all accumulated state in that lane is lost.
 * An attacker who knows the SECRET constants can trivially craft an input
 * word that zeros out an operand, with probability 2^-64 per attempt (or
 * 2^-63 considering either operand).
 *
 * MuseAir BFast avoids this by using a circular accumulator with six
 * 64-bit state words. The core loop processes 12 input words per iteration:
 *
 *   state[0] ^= input[0];
 *   state[1] ^= input[1];
 *   (lo0, hi0) = wide_mul(state[0], state[1]);
 *   state[0] = lo5 ^ hi0;   // lo5 is from the PREVIOUS step
 *   ...repeat circularly for state[1] through state[5]...
 *
 * Inputs are mixed into the state (not XORed with public constants), so
 * an attacker must predict the evolving state to craft a blinding input.
 *
 * Even if a blinding multiplication occurs at the current step (one
 * operand is zero, making lo0 = hi0 = 0), the state update becomes:
 *
 *   state[0] = lo5 ^ 0 = lo5
 *
 * The state is NOT zeroed out: it inherits lo5 from the previous step,
 * which still carries entropy from all earlier inputs. The state only
 * zeros out if lo5 also happens to be zero.
 *
 * The probability of lo5 being zero equals the probability that the
 * low 64 bits of a 64x64->128 multiplication are zero. This happens
 * when the total number of trailing zero bits in both operands is >= 64.
 * For uniformly random operands, the exact count of such pairs in
 * {0..2^n-1}^2 is (n+2) * 2^(n-1). For n = 64 this gives 33 * 2^64
 * pairs out of 2^128 total, so:
 *
 *   P(lo64 = 0) = 33 * 2^-64 ~ 2^-59
 *
 * The overall probability of a catastrophic state zero-out is therefore:
 *
 *   P(blinding at current step) * P(lo5 = 0)
 *   ~ 2^-63 * 33 * 2^-64
 *   = 33 * 2^-127
 *   ~ 2^-122
 *
 * This is astronomically unlikely and completely irrelevant for detecting
 * silent data corruption, where inputs are real file data (not adversarial).
 *
 * The "Standard" variant of MuseAir replaces the assignment (=) with a
 * subtraction (-=) in the state update, making blinding multiplication
 * impossible at a performance cost.
 *
 * Why BFast was chosen over Standard for SnapRAID
 *
 * SnapRAID uses the hash exclusively to detect silent data corruption
 * (bit rot, firmware bugs, media degradation). The inputs are always
 * real file data read from disk, never adversarially crafted content.
 * In this context:
 *
 * - The 2^-122 blinding probability is irrelevant. Random bit errors
 *   produce uniformly random changes that cannot trigger a blinding
 *   multiplication. Only a deliberate attacker controlling the exact
 *   file content could attempt to exploit it.
 *
 * - Both variants pass all 250 SMHasher3 quality tests and provide
 *   identical error detection for random corruption.
 *
 * - BFast is measurably faster, and its structure is more amenable to
 *   loop unrolling.
 *
 * The Standard variant's only advantage, full immunity to adversarial
 * blinding, provides no benefit when the threat model is physical
 * data corruption rather than a malicious actor.
 *
 * Short-input extension (len <= 32)
 *
 * The original MuseAir algorithm uses a completely separate "Short"
 * hash function for inputs of 32 bytes or less. This implementation
 * instead zero-pads short inputs to 32 bytes and processes them
 * through the same "Long" code path:
 *
 *   if (len < 32) { memset(buf, 0, 32); memcpy(buf, p, len); }
 *
 * The motivation is to avoid maintaining two distinct hash algorithms.
 * Having a single code path reduces complexity and the potential for
 * algorithm defects. Hash functions generally provide a specialized
 * short version to improve performance for small keys, but this is
 * irrelevant for SnapRAID, which typically hashes 256 KB blocks.
 * Short inputs only occur as an exception.
 *
 * This extension is safe because the original length is mixed into
 * the finalization, independent of the zero-padded buffer:
 *
 *   rot = len & 63;
 *   i = rotl64(i, rot);   // left-rotate by original length
 *   j = rotr64(j, rot);   // right-rotate by original length
 *   k = k - len;          // subtract original length
 *
 * So even though zero-padded buffers for, say, len=1 and len=2 are
 * identical, the different rotation amounts and subtraction values
 * produce completely different hashes after the final multiplications.
 */

#define u64x(N) (N * 8)

/* AiryAi(0) fractional part calculated by Y-Cruncher */
static const uint64_t MUSEAIR_CONSTANT[13] = {
	0x5ae31e589c56e17aULL,
	0x96d7bb04e64f6da9ULL,
	0x7ab1006b26f9eb64ULL,
	0x21233394220b8457ULL,
	0x047cb9557c9f3b43ULL,
	0xd24f2590c0bcee28ULL,
	0x33ea8f71bb6016d8ULL,
	0xb5d2697595d0a01fULL,
	0x9bb30a32f00e2b4fULL,
	0x4acea09317a429d1ULL,
	0xc2b2435dfdd545c6ULL,
	0xfda811a785572a42ULL,
	0xe5f50676bf67137bULL
};

#define mult64_128(l, h, a, b) \
	do { \
		l = a; \
		h = b; \
		util_mum(&l, &h); \
	} while (0)

#define likely(a) tommy_likely(a)
#define unlikely(a) tommy_unlikely(a)

static const uint64_t MASK_A = 0xAAAAAAAAAAAAAAAAULL;
static const uint64_t MASK_B = 0x5555555555555555ULL;
static const uint64_t MASK_I = 01555555555555555555555ULL;
static const uint64_t MASK_J = 01333333333333333333333ULL;
static const uint64_t MASK_K = 00666666666666666666666ULL;

static void MuseAirLoong(const void* bytes, size_t len, const uint8_t* seed, uint8_t* out)
{
	const uint8_t* p = bytes;
	size_t q = len;
	uint8_t buf[32];

	if (unlikely(q < 32)) {
		memset(buf, 0, 32);
		if (q > 0)
			memcpy(buf, p, q);
		p = buf;
		q = 32;
	}

	uint64_t i, j, k;
	uint64_t lo0, lo1, lo2, lo3, lo4, lo5 = MUSEAIR_CONSTANT[6];
	uint64_t hi0, hi1, hi2, hi3, hi4, hi5 = MUSEAIR_CONSTANT[6];
	uint64_t state[6] = { MUSEAIR_CONSTANT[0], MUSEAIR_CONSTANT[1], MUSEAIR_CONSTANT[2], MUSEAIR_CONSTANT[3], MUSEAIR_CONSTANT[4], MUSEAIR_CONSTANT[5] };

	uint64_t seed_a = util_read64(seed);
	uint64_t seed_b = util_read64(seed + 8);

	state[0] ^= seed_a & MASK_I;
	state[1] ^= seed_b & MASK_J;
	state[2] ^= seed_a & MASK_K;
	state[3] ^= seed_b & MASK_I;
	state[4] ^= seed_a & MASK_J;
	state[5] ^= seed_b & MASK_K;

	if (unlikely(q > u64x(12))) {
		do {
			state[0] ^= util_read64(p + u64x(0));
			state[1] ^= util_read64(p + u64x(1));
			mult64_128(lo0, hi0, state[0], state[1]);
			state[0] = lo5 ^ hi0;

			state[1] ^= util_read64(p + u64x(2));
			state[2] ^= util_read64(p + u64x(3));
			mult64_128(lo1, hi1, state[1], state[2]);
			state[1] = lo0 ^ hi1;

			state[2] ^= util_read64(p + u64x(4));
			state[3] ^= util_read64(p + u64x(5));
			mult64_128(lo2, hi2, state[2], state[3]);
			state[2] = lo1 ^ hi2;

			state[3] ^= util_read64(p + u64x(6));
			state[4] ^= util_read64(p + u64x(7));
			mult64_128(lo3, hi3, state[3], state[4]);
			state[3] = lo2 ^ hi3;

			state[4] ^= util_read64(p + u64x(8));
			state[5] ^= util_read64(p + u64x(9));
			mult64_128(lo4, hi4, state[4], state[5]);
			state[4] = lo3 ^ hi4;

			state[5] ^= util_read64(p + u64x(10));
			state[0] ^= util_read64(p + u64x(11));
			mult64_128(lo5, hi5, state[5], state[0]);
			state[5] = lo4 ^ hi5;

			p += u64x(12);
			q -= u64x(12);

		} while (likely(q > u64x(12)));

		state[0] ^= lo5;
	}

	lo0 = 0, lo1 = 0, lo2 = 0, lo3 = 0;
	hi0 = state[1];
	hi1 = state[2];
	hi2 = state[3];
	hi3 = state[4];

	if (q > u64x(4)) {
		state[0] ^= util_read64(p + u64x(0));
		state[1] ^= util_read64(p + u64x(1));
		mult64_128(lo0, hi0, state[0], state[1]);

		if (q > u64x(6)) {
			state[1] ^= util_read64(p + u64x(2));
			state[2] ^= util_read64(p + u64x(3));
			mult64_128(lo1, hi1, state[1], state[2]);

			if (q > u64x(8)) {
				state[2] ^= util_read64(p + u64x(4));
				state[3] ^= util_read64(p + u64x(5));
				mult64_128(lo2, hi2, state[2], state[3]);

				if (q > u64x(10)) {
					state[3] ^= util_read64(p + u64x(6));
					state[4] ^= util_read64(p + u64x(7));
					mult64_128(lo3, hi3, state[3], state[4]);
				}
			}
		}
	}

	state[4] ^= util_read64(p + q - u64x(4));
	state[5] ^= util_read64(p + q - u64x(3));
	mult64_128(lo4, hi4, state[4], state[5]);

	state[5] ^= util_read64(p + q - u64x(2));
	state[0] ^= util_read64(p + q - u64x(1));
	mult64_128(lo5, hi5, state[5], state[0]);

	i = (state[0] - state[1]) ^ MUSEAIR_CONSTANT[7];
	j = (state[2] - state[3]) ^ MUSEAIR_CONSTANT[8];
	k = (state[4] - state[5]) ^ MUSEAIR_CONSTANT[9];

	int rot = len & 63;
	i = util_rotl64(i, rot);
	j = util_rotr64(j, rot);
	k = k - len;

	i = i - (lo3 ^ hi3) - (lo4 ^ hi4);
	j = j - (lo5 ^ hi5) - (lo0 ^ hi0);
	k = k - (lo1 ^ hi1) - (lo2 ^ hi2);

	mult64_128(lo0, hi0, i, j);
	mult64_128(lo1, hi1, j, k);
	mult64_128(lo2, hi2, k, i);

	i = lo2 ^ hi0;
	j = lo0 ^ hi1;
	k = lo1 ^ hi2;

	mult64_128(lo3, hi3, i, MUSEAIR_CONSTANT[10]);
	mult64_128(lo4, hi4, j, MUSEAIR_CONSTANT[11]);
	mult64_128(lo5, hi5, k, MUSEAIR_CONSTANT[12]);

	util_write64(out, lo3 ^ hi4 ^ lo5);
	util_write64(out + 8, hi3 ^ lo4 ^ hi5);
}

