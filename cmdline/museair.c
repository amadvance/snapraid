/*
 * Copyright (C) 2026 Andrea Mazzoleni
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Derivative work from MuseAir 0.4-rc4 in the "Bfast" configuration
 *
 * This version extends the original MuseAirLoong to accept a full 128-bit seed.
 *
 * Note: The original algorithm's behavior can be perfectly replicated by
 * setting the low and high part of the seed to the same 64-bit value.
 *
 * https://github.com/eternal-io/museair
 */

/*
 * MuseAir hash algorithm itself and its reference implementation (this file)
 * by  K--Aethiax  are released into the public domain under the CC0 1.0 license.
 * To view a copy of this license, visit: https://creativecommons.org/publicdomain/zero/1.0/
 */

#define MUSEAIR_ALGORITHM_VERSION "0.4-rc4"

#define u64x(N) (N * 8)

/* AiryAi(0) fractional part calculated by Y-Cruncher */
static const uint64_t MUSEAIR_CONSTANT[7] = {
	0x5ae31e589c56e17aULL,
	0x96d7bb04e64f6da9ULL,
	0x7ab1006b26f9eb64ULL,
	0x21233394220b8457ULL,
	0x047cb9557c9f3b43ULL,
	0xd24f2590c0bcee28ULL,
	0x33ea8f71bb6016d8ULL
};

#define mult64_128(l, h, a, b) \
	do { \
		l = a; \
		h = b; \
		util_mum(&l, &h); \
	} while (0)

#define likely(a) tommy_likely(a)
#define unlikely(a) tommy_unlikely(a)

static void MuseAirLoong(const void* bytes, size_t len, const uint8_t* seed, uint8_t* out)
{
	const uint8_t* p = bytes;
	size_t q = len;

	/* EXTENSION: This algo doesn't support shorter lengths */
	assert(len >= 32);

	uint64_t i, j, k;

	uint64_t lo0, lo1, lo2, lo3, lo4, lo5 = MUSEAIR_CONSTANT[6];
	uint64_t hi0, hi1, hi2, hi3, hi4, hi5;

	/*
	 * EXTENSION: Initialize primary state with 128-bit seed (seed0 and seed1)
	 * Ensures both halves influence the hash.
	 *
	 * Original code was:
	 * uint64_t state[6] = {MUSEAIR_CONSTANT[0] + seed, MUSEAIR_CONSTANT[1] - seed, MUSEAIR_CONSTANT[2] ^ seed,
	 *                      MUSEAIR_CONSTANT[3] + seed, MUSEAIR_CONSTANT[4] - seed, MUSEAIR_CONSTANT[5] ^ seed};
	 */
	uint64_t seed0 = util_read64(seed);
	uint64_t seed1 = util_read64(seed + 8);
	uint64_t state[6] = { MUSEAIR_CONSTANT[0] + seed0, MUSEAIR_CONSTANT[1] - seed1, MUSEAIR_CONSTANT[2] ^ seed0,
			      MUSEAIR_CONSTANT[3] + seed1, MUSEAIR_CONSTANT[4] - seed0, MUSEAIR_CONSTANT[5] ^ seed1 };

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

	lo0 = 0, lo1 = 0, lo2 = 0, lo3 = 0, lo4 = 0, lo5 = 0;
	hi0 = 0, hi1 = 0, hi2 = 0, hi3 = 0, hi4 = 0, hi5 = 0;

	if (likely(q > u64x(4))) {
		state[0] ^= util_read64(p + u64x(0));
		state[1] ^= util_read64(p + u64x(1));
		mult64_128(lo0, hi0, state[0], state[1]);

		if (likely(q > u64x(6))) {
			state[1] ^= util_read64(p + u64x(2));
			state[2] ^= util_read64(p + u64x(3));
			mult64_128(lo1, hi1, state[1], state[2]);

			if (likely(q > u64x(8))) {
				state[2] ^= util_read64(p + u64x(4));
				state[3] ^= util_read64(p + u64x(5));
				mult64_128(lo2, hi2, state[2], state[3]);

				if (likely(q > u64x(10))) {
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

	i = state[0] - state[1];
	j = state[2] - state[3];
	k = state[4] - state[5];

	int rot = len & 63;
	i = util_rotl64(i, rot);
	j = util_rotr64(j, rot);
	k ^= len;

	i += lo3 ^ hi3 ^ lo4 ^ hi4;
	j += lo5 ^ hi5 ^ lo0 ^ hi0;
	k += lo1 ^ hi1 ^ lo2 ^ hi2;

	mult64_128(lo0, hi0, i, j);
	mult64_128(lo1, hi1, j, k);
	mult64_128(lo2, hi2, k, i);

	util_write64(out, lo0 ^ lo1 ^ hi2);
	util_write64(out + 8, hi0 ^ hi1 ^ lo2);
}

