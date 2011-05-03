/*
 * Copyright (C) 2011 Andrea Mazzoleni
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.	If not, see <http://www.gnu.org/licenses/>.
 */

/* Derivative work from MurmorHash3.cpp from http://code.google.com/p/smhasher/ */

// MurmurHash3 was written by Austin Appleby, and is placed in the public
// domain. The author hereby disclaims copyright to this source code.

// Note - The x86 and x64 versions do _not_ produce the same results, as the
// algorithms are optimized for their respective platforms. You can still
// compile and run any of them on any platform, but your performance with the
// non-native version will be less than optimal.

/* Rotate left */
inline uint32_t rotl32(uint32_t x, int8_t r)
{
	return (x << r) | (x >> (32 - r));
}

/* Swap endianess */
#if WORDS_BIGENDIAN
static inline uint32_t swap32(uint32_t v)
{
	return ((v & 0xff) << 24) | ((v & 0xff00) << 8) | ((v & 0xff0000) >> 8) | ((v >> 24) & 0xff);
}
#else
static inline uint32_t swap32(uint32_t v)
{
	return v;
}
#endif

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

uint32_t c1 = 0x239b961b;
uint32_t c2 = 0xab0e9789;
uint32_t c3 = 0x38b34ae5;
uint32_t c4 = 0xa1e38b93;

void MurmurHash3_x86_128(const void * key, unsigned len, uint32_t seed, void* out)
{
	const uint8_t * data = (const uint8_t*)key;
	unsigned nblocks = len / 16;
	unsigned i;

	uint32_t h1 = seed;
	uint32_t h2 = seed;
	uint32_t h3 = seed;
	uint32_t h4 = seed;

	const uint32_t* blocks = (const uint32_t*)data;

	/* body */

	for(i=0;i<nblocks;++i) {
		uint32_t k1 = swap32(blocks[0]);
		uint32_t k2 = swap32(blocks[1]);
		uint32_t k3 = swap32(blocks[2]);
		uint32_t k4 = swap32(blocks[3]);

		k1 *= c1; k1 = rotl32(k1,15); k1 *= c2; h1 ^= k1;

		h1 = rotl32(h1,19); h1 += h2; h1 = h1*5+0x561ccd1b;

		k2 *= c2; k2 = rotl32(k2,16); k2 *= c3; h2 ^= k2;

		h2 = rotl32(h2,17); h2 += h3; h2 = h2*5+0x0bcaa747;

		k3 *= c3; k3 = rotl32(k3,17); k3 *= c4; h3 ^= k3;

		h3 = rotl32(h3,15); h3 += h4; h3 = h3*5+0x96cd1c35;

		k4 *= c4; k4 = rotl32(k4,18); k4 *= c1; h4 ^= k4;

		h4 = rotl32(h4,13); h4 += h1; h4 = h4*5+0x32ac3b17;

		blocks += 4;
	}

	/* tail */
	{
		const uint8_t* tail = (const uint8_t*)blocks;

		uint32_t k1 = 0;
		uint32_t k2 = 0;
		uint32_t k3 = 0;
		uint32_t k4 = 0;

		switch (len & 15) {
		case 15 : k4 ^= tail[14] << 16;
		case 14 : k4 ^= tail[13] << 8;
		case 13 : k4 ^= tail[12] << 0;
			k4 *= c4; k4 = rotl32(k4,18); k4 *= c1; h4 ^= k4;
		case 12 : k3 ^= tail[11] << 24;
		case 11 : k3 ^= tail[10] << 16;
		case 10 : k3 ^= tail[ 9] << 8;
		case 9 : k3 ^= tail[ 8] << 0;
			k3 *= c3; k3 = rotl32(k3,17); k3 *= c4; h3 ^= k3;
		case 8 : k2 ^= tail[ 7] << 24;
		case 7 : k2 ^= tail[ 6] << 16;
		case 6 : k2 ^= tail[ 5] << 8;
		case 5 : k2 ^= tail[ 4] << 0;
			k2 *= c2; k2 = rotl32(k2,16); k2 *= c3; h2 ^= k2;
		case 4 : k1 ^= tail[ 3] << 24;
		case 3 : k1 ^= tail[ 2] << 16;
		case 2 : k1 ^= tail[ 1] << 8;
		case 1 : k1 ^= tail[ 0] << 0;
			k1 *= c1; k1 = rotl32(k1,15); k1 *= c2; h1 ^= k1;
		};
	}

	/* finalization */

	h1 ^= len; h2 ^= len; h3 ^= len; h4 ^= len;

	h1 += h2; h1 += h3; h1 += h4;
	h2 += h1; h3 += h1; h4 += h1;

	h1 = fmix32(h1);
	h2 = fmix32(h2);
	h3 = fmix32(h3);
	h4 = fmix32(h4);

	h1 += h2; h1 += h3; h1 += h4;
	h2 += h1; h3 += h1; h4 += h1;

	((uint32_t*)out)[0] = swap32(h1);
	((uint32_t*)out)[1] = swap32(h2);
	((uint32_t*)out)[2] = swap32(h3);
	((uint32_t*)out)[3] = swap32(h4);
}

