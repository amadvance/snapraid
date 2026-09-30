// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2011 Andrea Mazzoleni

/*
 * Derivative work from MurmorHash3.cpp revision r136
 *
 * SMHasher & MurmurHash
 * http://code.google.com/p/smhasher/
 *
 * Exact source used as reference:
 * http://code.google.com/p/smhasher/source/browse/trunk/MurmurHash3.cpp?spec=svn136&r=136
 */

// MurmurHash3 was written by Austin Appleby, and is placed in the public
// domain. The author hereby disclaims copyright to this source code.

/* Finalization mix - force all bits of a hash block to avalanche */
static inline uint32_t fmix32(uint32_t h)
{
	h ^= h >> 16;
	h *= 0x85ebca6b;
	h ^= h >> 13;
	h *= 0xc2b2ae35;
	h ^= h >> 16;
	return h;
}

/*
 * Non-static variables are intentionally used instead of static const
 * because keeping these constants in CPU registers yields measurably
 * higher throughput than using 32-bit immediate constants in imul.
 */
uint32_t c1 = 0x239b961b;
uint32_t c2 = 0xab0e9789;
uint32_t c3 = 0x38b34ae5;
uint32_t c4 = 0xa1e38b93;

static void MurmurHash3_x86_128(const void* data, size_t size, const uint8_t* seed, void* digest)
{
	const uint8_t* p;
	const uint8_t* end;
	size_t size_remainder;
	uint32_t h1, h2, h3, h4;

	h1 = util_read32(seed + 0);
	h2 = util_read32(seed + 4);
	h3 = util_read32(seed + 8);
	h4 = util_read32(seed + 12);

	p = data;
	end = p + (size & ~15);

	/* body */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC unroll 8
#elif defined(__clang__)
#pragma unroll 8
#endif
	while (p < end) {
		uint32_t k1 = util_read32(p + 0);
		uint32_t k2 = util_read32(p + 4);
		uint32_t k3 = util_read32(p + 8);
		uint32_t k4 = util_read32(p + 12);

		k1 *= c1; k1 = util_rotl32(k1, 15); k1 *= c2; h1 ^= k1;

		h1 = util_rotl32(h1, 19); h1 += h2; h1 = h1 * 5 + 0x561ccd1b;

		k2 *= c2; k2 = util_rotl32(k2, 16); k2 *= c3; h2 ^= k2;

		h2 = util_rotl32(h2, 17); h2 += h3; h2 = h2 * 5 + 0x0bcaa747;

		k3 *= c3; k3 = util_rotl32(k3, 17); k3 *= c4; h3 ^= k3;

		h3 = util_rotl32(h3, 15); h3 += h4; h3 = h3 * 5 + 0x96cd1c35;

		k4 *= c4; k4 = util_rotl32(k4, 18); k4 *= c1; h4 ^= k4;

		h4 = util_rotl32(h4, 13); h4 += h1; h4 = h4 * 5 + 0x32ac3b17;

		p += 16;
	}

	/* tail */
	size_remainder = size & 15;
	if (size_remainder != 0) {
		const uint8_t* tail = p;

		uint32_t k1 = 0;
		uint32_t k2 = 0;
		uint32_t k3 = 0;
		uint32_t k4 = 0;

		switch (size_remainder) {
		case 15 : k4 ^= (uint32_t)tail[14] << 16; /* fallthrough */
		case 14 : k4 ^= (uint32_t)tail[13] << 8; /* fallthrough */
		case 13 : k4 ^= (uint32_t)tail[12] << 0; /* fallthrough */
			k4 *= c4; k4 = util_rotl32(k4, 18); k4 *= c1; h4 ^= k4;
		/* fallthrough */
		case 12 : k3 ^= (uint32_t)tail[11] << 24; /* fallthrough */
		case 11 : k3 ^= (uint32_t)tail[10] << 16; /* fallthrough */
		case 10 : k3 ^= (uint32_t)tail[ 9] << 8; /* fallthrough */
		case 9 : k3 ^= (uint32_t)tail[ 8] << 0; /* fallthrough */
			k3 *= c3; k3 = util_rotl32(k3, 17); k3 *= c4; h3 ^= k3;
		/* fallthrough */
		case 8 : k2 ^= (uint32_t)tail[ 7] << 24; /* fallthrough */
		case 7 : k2 ^= (uint32_t)tail[ 6] << 16; /* fallthrough */
		case 6 : k2 ^= (uint32_t)tail[ 5] << 8; /* fallthrough */
		case 5 : k2 ^= (uint32_t)tail[ 4] << 0; /* fallthrough */
			k2 *= c2; k2 = util_rotl32(k2, 16); k2 *= c3; h2 ^= k2;
		/* fallthrough */
		case 4 : k1 ^= (uint32_t)tail[ 3] << 24; /* fallthrough */
		case 3 : k1 ^= (uint32_t)tail[ 2] << 16; /* fallthrough */
		case 2 : k1 ^= (uint32_t)tail[ 1] << 8; /* fallthrough */
		case 1 : k1 ^= (uint32_t)tail[ 0] << 0; /* fallthrough */
			k1 *= c1; k1 = util_rotl32(k1, 15); k1 *= c2; h1 ^= k1;
			/* fallthrough */
		}
	}

	/* finalization */
	h1 ^= size; h2 ^= size; h3 ^= size; h4 ^= size;

	h1 += h2; h1 += h3; h1 += h4;
	h2 += h1; h3 += h1; h4 += h1;

	h1 = fmix32(h1);
	h2 = fmix32(h2);
	h3 = fmix32(h3);
	h4 = fmix32(h4);

	h1 += h2; h1 += h3; h1 += h4;
	h2 += h1; h3 += h1; h4 += h1;

	util_write32(digest + 0, h1);
	util_write32(digest + 4, h2);
	util_write32(digest + 8, h3);
	util_write32(digest + 12, h4);
}

