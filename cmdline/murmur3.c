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

struct murmur3_context {
	uint32_t h1;
	uint32_t h2;
	uint32_t h3;
	uint32_t h4;
	uint32_t size;
};

#define MURMUR3_BLOCK_SIZE 16

void murmur3_init(struct murmur3_context* ctx, const uint8_t* seed)
{
	ctx->h1 = ((const uint32_t*)seed)[0];
	ctx->h2 = ((const uint32_t*)seed)[1];
	ctx->h3 = ((const uint32_t*)seed)[2];
	ctx->h4 = ((const uint32_t*)seed)[3];

#if WORDS_BIGENDIAN
	ctx->h1 = util_swap32(h1);
	ctx->h2 = util_swap32(h2);
	ctx->h3 = util_swap32(h3);
	ctx->h4 = util_swap32(h4);
#endif

	ctx->size = 0;
}

void murmur3_block(struct murmur3_context* ctx, const void* data, size_t size)
{
	uint32_t h1, h2, h3, h4;
	size_t nblocks;
	const uint32_t* blocks;
	const uint32_t* end;

	h1 = ctx->h1;
	h2 = ctx->h2;
	h3 = ctx->h3;
	h4 = ctx->h4;

	assert(size % MURMUR3_BLOCK_SIZE == 0);

	blocks = (const uint32_t*)data;
	nblocks = size / MURMUR3_BLOCK_SIZE;
	end = blocks + nblocks * 4;

	/* body */
	ctx->size += size;
	while (blocks < end) {
		uint32_t k1 = blocks[0];
		uint32_t k2 = blocks[1];
		uint32_t k3 = blocks[2];
		uint32_t k4 = blocks[3];

#if WORDS_BIGENDIAN
		k1 = util_swap32(k1);
		k2 = util_swap32(k2);
		k3 = util_swap32(k3);
		k4 = util_swap32(k4);
#endif

		k1 *= c1; k1 = util_rotl32(k1, 15); k1 *= c2; h1 ^= k1;

		h1 = util_rotl32(h1, 19); h1 += h2; h1 = h1 * 5 + 0x561ccd1b;

		k2 *= c2; k2 = util_rotl32(k2, 16); k2 *= c3; h2 ^= k2;

		h2 = util_rotl32(h2, 17); h2 += h3; h2 = h2 * 5 + 0x0bcaa747;

		k3 *= c3; k3 = util_rotl32(k3, 17); k3 *= c4; h3 ^= k3;

		h3 = util_rotl32(h3, 15); h3 += h4; h3 = h3 * 5 + 0x96cd1c35;

		k4 *= c4; k4 = util_rotl32(k4, 18); k4 *= c1; h4 ^= k4;

		h4 = util_rotl32(h4, 13); h4 += h1; h4 = h4 * 5 + 0x32ac3b17;

		blocks += 4;
	}

	ctx->h1 = h1;
	ctx->h2 = h2;
	ctx->h3 = h3;
	ctx->h4 = h4;
}

void murmur3_final(struct murmur3_context* ctx, const void* data, size_t size, void* digest)
{
	const uint8_t* tail = (const uint8_t*)data;

	uint32_t k1 = 0;
	uint32_t k2 = 0;
	uint32_t k3 = 0;
	uint32_t k4 = 0;

	assert(size < MURMUR3_BLOCK_SIZE);

	/* tail */
	ctx->size += size;
	switch (size) {
	case 15 : k4 ^= tail[14] << 16;
	case 14 : k4 ^= tail[13] << 8;
	case 13 : k4 ^= tail[12] << 0;
		k4 *= c4; k4 = util_rotl32(k4, 18); k4 *= c1; ctx->h4 ^= k4;
	case 12 : k3 ^= tail[11] << 24;
	case 11 : k3 ^= tail[10] << 16;
	case 10 : k3 ^= tail[ 9] << 8;
	case 9 : k3 ^= tail[ 8] << 0;
		k3 *= c3; k3 = util_rotl32(k3, 17); k3 *= c4; ctx->h3 ^= k3;
	case 8 : k2 ^= tail[ 7] << 24;
	case 7 : k2 ^= tail[ 6] << 16;
	case 6 : k2 ^= tail[ 5] << 8;
	case 5 : k2 ^= tail[ 4] << 0;
		k2 *= c2; k2 = util_rotl32(k2, 16); k2 *= c3; ctx->h2 ^= k2;
	case 4 : k1 ^= tail[ 3] << 24;
	case 3 : k1 ^= tail[ 2] << 16;
	case 2 : k1 ^= tail[ 1] << 8;
	case 1 : k1 ^= tail[ 0] << 0;
		k1 *= c1; k1 = util_rotl32(k1, 15); k1 *= c2; ctx->h1 ^= k1;
	}

	/* finalization */
	ctx->h1 ^= ctx->size; ctx->h2 ^= ctx->size; ctx->h3 ^= ctx->size; ctx->h4 ^= ctx->size;

	ctx->h1 += ctx->h2;
	ctx->h1 += ctx->h3;
	ctx->h1 += ctx->h4;
	ctx->h2 += ctx->h1;
	ctx->h3 += ctx->h1;
	ctx->h4 += ctx->h1;

	ctx->h1 = fmix32(ctx->h1);
	ctx->h2 = fmix32(ctx->h2);
	ctx->h3 = fmix32(ctx->h3);
	ctx->h4 = fmix32(ctx->h4);

	ctx->h1 += ctx->h2;
	ctx->h1 += ctx->h3;
	ctx->h1 += ctx->h4;
	ctx->h2 += ctx->h1;
	ctx->h3 += ctx->h1;
	ctx->h4 += ctx->h1;

#if WORDS_BIGENDIAN
	ctx->h1 = util_swap32(ctx->h1);
	ctx->h2 = util_swap32(ctx->h2);
	ctx->h3 = util_swap32(ctx->h3);
	ctx->h4 = util_swap32(ctx->h4);
#endif

	((uint32_t*)digest)[0] = ctx->h1;
	((uint32_t*)digest)[1] = ctx->h2;
	((uint32_t*)digest)[2] = ctx->h3;
	((uint32_t*)digest)[3] = ctx->h4;
}

void murmur3(const void* void_data, size_t size, const uint8_t* seed, void* digest)
{
	struct murmur3_context ctx;
	const uint8_t* data = (const uint8_t*)void_data;

	size_t tail = size % MURMUR3_BLOCK_SIZE;
	size_t body = size - tail;

	murmur3_init(&ctx, seed);
	murmur3_block(&ctx, data, body);
	murmur3_final(&ctx, data + body, tail, digest);
}

int murmur3_flip(void* void_data, size_t size, const uint8_t* seed, const void* digest)
{
	struct murmur3_context last_ctx;
	size_t last_off;
	size_t off;
	uint8_t* data = (uint8_t*)void_data;

	size_t tail = size % MURMUR3_BLOCK_SIZE;
	size_t body = size - tail;

	murmur3_init(&last_ctx, seed);
	last_off = 0;

	for (off = 0; off < size; ++off) {
		unsigned bit;

		if (off == last_off + MURMUR3_BLOCK_SIZE) {
			/* process one more block */
			murmur3_block(&last_ctx, data + last_off, MURMUR3_BLOCK_SIZE);
			last_off += MURMUR3_BLOCK_SIZE;
		}

		/* try all bits */
		for (bit = 0; bit < 8; ++bit) {
			struct murmur3_context ctx;
			uint8_t out[16];

			/* flip the bit */
			data[off] ^= 1 << bit;

			/* compute the new hash from this point */
			ctx = last_ctx;
			murmur3_block(&ctx, data + last_off, body - last_off);
			murmur3_final(&ctx, data + body, tail, out);

			/* check if the digest is correct */
			if (memcmp(digest, out, 16) == 0)
				return 0;

			/* restore the bit */
			data[off] ^= 1 << bit;
		}
	}

	return -1;
}

