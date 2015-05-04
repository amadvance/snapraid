/*
 * Copyright (C) 2013 Andrea Mazzoleni
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
 * Derivative work from SpookyV2.cpp/h
 *
 * WARNING!!!! Note that this implementation doesn't use the short hash optimization
 * resulting in different hashes for any length shorter than 192 bytes
 *
 * SpookyHash
 * http://burtleburtle.net/bob/hash/spooky.html
 *
 * Exact source used as reference:
 * http://burtleburtle.net/bob/c/SpookyV2.h
 * http://burtleburtle.net/bob/c/SpookyV2.cpp
 */

// Spooky Hash
// A 128-bit noncryptographic hash, for checksums and table lookup
// By Bob Jenkins.  Public domain.
//   Oct 31 2010: published framework, disclaimer ShortHash isn't right
//   Nov 7 2010: disabled ShortHash
//   Oct 31 2011: replace End, ShortMix, ShortEnd, enable ShortHash again
//   April 10 2012: buffer overflow on platforms without unaligned reads
//   July 12 2012: was passing out variables in final to in/out in short
//   July 30 2012: I reintroduced the buffer overflow
//   August 5 2012: SpookyV2: d = should be d += in short hash, and remove extra mix from long hash
//
// Up to 3 bytes/cycle for long messages.  Reasonably fast for short messages.
// All 1 or 2 bit deltas achieve avalanche within 1% bias per output bit.
//
// This was developed for and tested on 64-bit x86-compatible processors.
// It assumes the processor is little-endian.  There is a macro
// controlling whether unaligned reads are allowed (by default they are).
// This should be an equally good hash on big-endian machines, but it will
// compute different results on them than on little-endian machines.
//
// Google's CityHash has similar specs to SpookyHash, and CityHash is faster
// on new Intel boxes.  MD4 and MD5 also have similar specs, but they are orders
// of magnitude slower.  CRCs are two or more times slower, but unlike
// SpookyHash, they have nice math for combining the CRCs of pieces to form
// the CRCs of wholes.  There are also cryptographic hashes, but those are even
// slower than MD5.
//

#define Mix(data, s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11) \
	s0 += data[0];   s2 ^= s10;  s11 ^= s0;   s0 = util_rotl64(s0, 11);   s11 += s1; \
	s1 += data[1];   s3 ^= s11;  s0 ^= s1;   s1 = util_rotl64(s1, 32);   s0 += s2; \
	s2 += data[2];   s4 ^= s0;   s1 ^= s2;   s2 = util_rotl64(s2, 43);   s1 += s3; \
	s3 += data[3];   s5 ^= s1;   s2 ^= s3;   s3 = util_rotl64(s3, 31);   s2 += s4; \
	s4 += data[4];   s6 ^= s2;   s3 ^= s4;   s4 = util_rotl64(s4, 17);   s3 += s5; \
	s5 += data[5];   s7 ^= s3;   s4 ^= s5;   s5 = util_rotl64(s5, 28);   s4 += s6; \
	s6 += data[6];   s8 ^= s4;   s5 ^= s6;   s6 = util_rotl64(s6, 39);   s5 += s7; \
	s7 += data[7];   s9 ^= s5;   s6 ^= s7;   s7 = util_rotl64(s7, 57);   s6 += s8; \
	s8 += data[8];   s10 ^= s6;   s7 ^= s8;   s8 = util_rotl64(s8, 55);   s7 += s9; \
	s9 += data[9];   s11 ^= s7;   s8 ^= s9;   s9 = util_rotl64(s9, 54);   s8 += s10; \
	s10 += data[10];  s0 ^= s8;   s9 ^= s10;  s10 = util_rotl64(s10, 22);  s9 += s11; \
	s11 += data[11];  s1 ^= s9;   s10 ^= s11;  s11 = util_rotl64(s11, 46);  s10 += s0;

#define EndPartial(h0, h1, h2, h3, h4, h5, h6, h7, h8, h9, h10, h11) \
	h11 += h1;   h2 ^= h11;  h1 = util_rotl64(h1, 44); \
	h0 += h2;   h3 ^= h0;   h2 = util_rotl64(h2, 15); \
	h1 += h3;   h4 ^= h1;   h3 = util_rotl64(h3, 34); \
	h2 += h4;   h5 ^= h2;   h4 = util_rotl64(h4, 21); \
	h3 += h5;   h6 ^= h3;   h5 = util_rotl64(h5, 38); \
	h4 += h6;   h7 ^= h4;   h6 = util_rotl64(h6, 33); \
	h5 += h7;   h8 ^= h5;   h7 = util_rotl64(h7, 10); \
	h6 += h8;   h9 ^= h6;   h8 = util_rotl64(h8, 13); \
	h7 += h9;   h10 ^= h7;   h9 = util_rotl64(h9, 38); \
	h8 += h10;  h11 ^= h8;   h10 = util_rotl64(h10, 53); \
	h9 += h11;  h0 ^= h9;   h11 = util_rotl64(h11, 42); \
	h10 += h0;   h1 ^= h10;  h0 = util_rotl64(h0, 54);

#define End(data, h0, h1, h2, h3, h4, h5, h6, h7, h8, h9, h10, h11) \
	h0 += data[0];  h1 += data[1];  h2 += data[2];    h3 += data[3]; \
	h4 += data[4];  h5 += data[5];  h6 += data[6];    h7 += data[7]; \
	h8 += data[8];  h9 += data[9];  h10 += data[10];   h11 += data[11]; \
	EndPartial(h0, h1, h2, h3, h4, h5, h6, h7, h8, h9, h10, h11); \
	EndPartial(h0, h1, h2, h3, h4, h5, h6, h7, h8, h9, h10, h11); \
	EndPartial(h0, h1, h2, h3, h4, h5, h6, h7, h8, h9, h10, h11);

// number of uint64_t's in internal state
#define sc_numVars 12

// size of the internal state
#define sc_blockSize (sc_numVars * 8)

// sc_const: a constant which:
//  * is not zero
//  * is odd
//  * is a not-very-regular mix of 1's and 0's
//  * does not need any other special mathematical properties
//
#define sc_const 0xdeadbeefdeadbeefLL

#define SPOOKY2_BLOCK_SIZE sc_blockSize

struct spooky2_context {
	uint64_t h0, h1, h2, h3, h4, h5, h6, h7, h8, h9, h10, h11;
};

void spooky2_init(struct spooky2_context* ctx, const uint8_t* seed)
{
	ctx->h9 = ((const uint64_t*)seed)[0];
	ctx->h10 = ((const uint64_t*)seed)[1];

#if WORDS_BIGENDIAN
	ctx->h9 = util_swap64(ctx->h9);
	ctx->h10 = util_swap64(ctx->h10);
#endif

	ctx->h0 = ctx->h3 = ctx->h6 = ctx->h9;
	ctx->h1 = ctx->h4 = ctx->h7 = ctx->h10;
	ctx->h2 = ctx->h5 = ctx->h8 = ctx->h11 = sc_const;
}

void spooky2_block(struct spooky2_context* ctx, const void* data, size_t size)
{
	uint64_t h0, h1, h2, h3, h4, h5, h6, h7, h8, h9, h10, h11;
#if WORDS_BIGENDIAN
	uint64_t buf[sc_numVars];
	size_t i;
#endif
	size_t nblocks;
	const uint64_t* blocks;
	const uint64_t* end;

	assert(size % SPOOKY2_BLOCK_SIZE == 0);

	blocks = (const uint64_t*)data;
	nblocks = size / SPOOKY2_BLOCK_SIZE;
	end = blocks + nblocks * sc_numVars;

	h0 = ctx->h0;
	h1 = ctx->h1;
	h2 = ctx->h2;
	h3 = ctx->h3;
	h4 = ctx->h4;
	h5 = ctx->h5;
	h6 = ctx->h6;
	h7 = ctx->h7;
	h8 = ctx->h8;
	h9 = ctx->h9;
	h10 = ctx->h10;
	h11 = ctx->h11;

	/* body */
	while (blocks < end) {
#if WORDS_BIGENDIAN
		for (i = 0; i < sc_numVars; ++i)
			buf[i] = util_swap64(blocks[i]);
		Mix(buf, h0, h1, h2, h3, h4, h5, h6, h7, h8, h9, h10, h11);
#else
		Mix(blocks, h0, h1, h2, h3, h4, h5, h6, h7, h8, h9, h10, h11);
#endif
		blocks += sc_numVars;
	}

	ctx->h0 = h0;
	ctx->h1 = h1;
	ctx->h2 = h2;
	ctx->h3 = h3;
	ctx->h4 = h4;
	ctx->h5 = h5;
	ctx->h6 = h6;
	ctx->h7 = h7;
	ctx->h8 = h8;
	ctx->h9 = h9;
	ctx->h10 = h10;
	ctx->h11 = h11;
}

void spooky2_final(struct spooky2_context* ctx, const void* data, size_t size, void* digest)
{
	uint64_t h0, h1, h2, h3, h4, h5, h6, h7, h8, h9, h10, h11;
	uint64_t buf[sc_numVars];
#if WORDS_BIGENDIAN
	size_t i;
#endif

	assert(size < SPOOKY2_BLOCK_SIZE);

	/* tail */
	memcpy(buf, data, size);
	memset(((uint8_t*)buf) + size, 0, SPOOKY2_BLOCK_SIZE - size);
	((uint8_t*)buf)[SPOOKY2_BLOCK_SIZE - 1] = size;

	/* finalization */
#if WORDS_BIGENDIAN
	for (i = 0; i < sc_numVars; ++i)
		buf[i] = util_swap64(buf[i]);
#endif

	h0 = ctx->h0;
	h1 = ctx->h1;
	h2 = ctx->h2;
	h3 = ctx->h3;
	h4 = ctx->h4;
	h5 = ctx->h5;
	h6 = ctx->h6;
	h7 = ctx->h7;
	h8 = ctx->h8;
	h9 = ctx->h9;
	h10 = ctx->h10;
	h11 = ctx->h11;

	End(buf, h0, h1, h2, h3, h4, h5, h6, h7, h8, h9, h10, h11);

#if WORDS_BIGENDIAN
	h0 = util_swap64(h0);
	h1 = util_swap64(h1);
#endif

	((uint64_t*)digest)[0] = h0;
	((uint64_t*)digest)[1] = h1;
}

void spooky2(const void* data, size_t size, const uint8_t* seed, uint8_t* digest)
{
	uint64_t h0, h1, h2, h3, h4, h5, h6, h7, h8, h9, h10, h11;
	uint64_t buf[sc_numVars];
#if WORDS_BIGENDIAN
	size_t i;
#endif
	size_t nblocks;
	const uint64_t* blocks;
	const uint64_t* end;
	size_t remainder;

	h9 = ((const uint64_t*)seed)[0];
	h10 = ((const uint64_t*)seed)[1];

#if WORDS_BIGENDIAN
	h9 = util_swap64(h9);
	h10 = util_swap64(h10);
#endif

	h0 = h3 = h6 = h9;
	h1 = h4 = h7 = h10;
	h2 = h5 = h8 = h11 = sc_const;

	blocks = (const uint64_t*)data;
	nblocks = size / SPOOKY2_BLOCK_SIZE;
	end = blocks + nblocks * sc_numVars;

	/* body */

	while (blocks < end) {
#if WORDS_BIGENDIAN
		for (i = 0; i < sc_numVars; ++i)
			buf[i] = util_swap64(u.p64[i]);
		Mix(buf, h0, h1, h2, h3, h4, h5, h6, h7, h8, h9, h10, h11);
#else
		Mix(blocks, h0, h1, h2, h3, h4, h5, h6, h7, h8, h9, h10, h11);
#endif
		blocks += sc_numVars;
	}

	/* tail */
	remainder = (size - ((const uint8_t*)end - (const uint8_t*)data));
	memcpy(buf, end, remainder);
	memset(((uint8_t*)buf) + remainder, 0, sc_blockSize - remainder);
	((uint8_t*)buf)[sc_blockSize - 1] = remainder;

	/* finalization */
#if WORDS_BIGENDIAN
	for (i = 0; i < sc_numVars; ++i)
		buf[i] = util_swap64(buf[i]);
#endif

	End(buf, h0, h1, h2, h3, h4, h5, h6, h7, h8, h9, h10, h11);

#if WORDS_BIGENDIAN
	h0 = util_swap64(h0);
	h1 = util_swap64(h1);
#endif

	((uint64_t*)digest)[0] = h0;
	((uint64_t*)digest)[1] = h1;
}

int spooky2_flip(void* void_data, uint32_t size, const uint8_t* seed, const void* digest)
{
	struct spooky2_context last_ctx;
	size_t last_off;
	size_t off;
	uint8_t* data = (uint8_t*)void_data;

	uint32_t tail = size % SPOOKY2_BLOCK_SIZE;
	uint32_t body = size - tail;

	spooky2_init(&last_ctx, seed);
	last_off = 0;

	for (off = 0; off < size; ++off) {
		unsigned bit;

		if (off == last_off + SPOOKY2_BLOCK_SIZE) {
			/* process one more block */
			spooky2_block(&last_ctx, data + last_off, SPOOKY2_BLOCK_SIZE);
			last_off += SPOOKY2_BLOCK_SIZE;
		}

		/* try all bits */
		for (bit = 0; bit < 8; ++bit) {
			struct spooky2_context ctx;
			uint8_t out[16];

			/* flip the bit */
			data[off] ^= 1 << bit;

			/* restore the latest context */
			ctx = last_ctx;

			/* compute the new hash from this point */
			spooky2_block(&last_ctx, data + last_off, body - last_off);
			spooky2_final(&ctx, data + body, tail, out);

			/* check if the digest is correct */
			if (memcmp(digest, out, 16) == 0)
				return 0;

			/* restore the bit */
			data[off] ^= 1 << bit;
		}
	}

	return -1;
}

