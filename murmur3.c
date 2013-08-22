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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

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
 * Warning!
 * Don't declare these variables static, otherwise the gcc optimizer
 * may generate very slow code for multiplication with these constants,
 * like:
 
-> .cpp
 k1 *= c1;
-> .asm
 152:   8d 14 80                lea    (%eax,%eax,4),%edx
 155:   8d 14 90                lea    (%eax,%edx,4),%edx
 158:   c1 e2 03                shl    $0x3,%edx
 15b:   29 c2                   sub    %eax,%edx
 15d:   8d 14 d2                lea    (%edx,%edx,8),%edx
 160:   8d 14 90                lea    (%eax,%edx,4),%edx
 163:   8d 14 d0                lea    (%eax,%edx,8),%edx
 166:   8d 14 90                lea    (%eax,%edx,4),%edx
 169:   8d 14 50                lea    (%eax,%edx,2),%edx
 16c:   8d 14 90                lea    (%eax,%edx,4),%edx
 16f:   8d 14 92                lea    (%edx,%edx,4),%edx
 172:   8d 14 50                lea    (%eax,%edx,2),%edx
 175:   8d 04 d0                lea    (%eax,%edx,8),%eax
 178:   8d 14 c5 00 00 00 00    lea    0x0(,%eax,8),%edx
 17f:   29 d0                   sub    %edx,%eax

 * resulting in speeds of 500 MB/s instead of 3000 MB/s.
 *
 * Verified with gcc 4.4.4 compiling with :
 *
 * g++ -g -c -O2 MurmurHash3.cpp -o MurmurHash3.o
 */
uint32_t c1 = 0x239b961b;
uint32_t c2 = 0xab0e9789;
uint32_t c3 = 0x38b34ae5;
uint32_t c4 = 0xa1e38b93;

void MurmurHash3_x86_128(const void* key, unsigned len, const uint8_t* seed, void* out)
{
	const uint8_t* data = key;
	unsigned nblocks = len / 16;
	unsigned i;
	const uint32_t* blocks;

	uint32_t h1 = ((uint32_t*)seed)[0];
	uint32_t h2 = ((uint32_t*)seed)[1];
	uint32_t h3 = ((uint32_t*)seed)[2];
	uint32_t h4 = ((uint32_t*)seed)[3];

#if WORDS_BIGENDIAN
	h1 = swap32(h1);
	h2 = swap32(h2);
	h3 = swap32(h3);
	h4 = swap32(h4);
#endif

	blocks = (const uint32_t*)data;

	/* body */

	for(i=0;i<nblocks;++i) {
		uint32_t k1 = blocks[0];
		uint32_t k2 = blocks[1];
		uint32_t k3 = blocks[2];
		uint32_t k4 = blocks[3];

#if WORDS_BIGENDIAN
		k1 = swap32(k1);
		k2 = swap32(k2);
		k3 = swap32(k3);
		k4 = swap32(k4);
#endif

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

#if WORDS_BIGENDIAN
	h1 = swap32(h1);
	h2 = swap32(h2);
	h3 = swap32(h3);
	h4 = swap32(h4);
#endif

	((uint32_t*)out)[0] = h1;
	((uint32_t*)out)[1] = h2;
	((uint32_t*)out)[2] = h3;
	((uint32_t*)out)[3] = h4;
}

