// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2013 Andrea Mazzoleni

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

#define rd(data, x) util_read64((const uint8_t*)(data) + (x) * 8)

#define Mix(data, s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11) \
	s0 += rd(data, 0);   s2 ^= s10;  s11 ^= s0;   s0 = util_rotl64(s0, 11);   s11 += s1; \
	s1 += rd(data, 1);   s3 ^= s11;  s0 ^= s1;   s1 = util_rotl64(s1, 32);   s0 += s2; \
	s2 += rd(data, 2);   s4 ^= s0;   s1 ^= s2;   s2 = util_rotl64(s2, 43);   s1 += s3; \
	s3 += rd(data, 3);   s5 ^= s1;   s2 ^= s3;   s3 = util_rotl64(s3, 31);   s2 += s4; \
	s4 += rd(data, 4);   s6 ^= s2;   s3 ^= s4;   s4 = util_rotl64(s4, 17);   s3 += s5; \
	s5 += rd(data, 5);   s7 ^= s3;   s4 ^= s5;   s5 = util_rotl64(s5, 28);   s4 += s6; \
	s6 += rd(data, 6);   s8 ^= s4;   s5 ^= s6;   s6 = util_rotl64(s6, 39);   s5 += s7; \
	s7 += rd(data, 7);   s9 ^= s5;   s6 ^= s7;   s7 = util_rotl64(s7, 57);   s6 += s8; \
	s8 += rd(data, 8);   s10 ^= s6;  s7 ^= s8;   s8 = util_rotl64(s8, 55);   s7 += s9; \
	s9 += rd(data, 9);   s11 ^= s7;  s8 ^= s9;   s9 = util_rotl64(s9, 54);   s8 += s10; \
	s10 += rd(data, 10); s0 ^= s8;   s9 ^= s10;  s10 = util_rotl64(s10, 22); s9 += s11; \
	s11 += rd(data, 11); s1 ^= s9;   s10 ^= s11; s11 = util_rotl64(s11, 46); s10 += s0;

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
	h0 += rd(data, 0);   h1 += rd(data, 1);   h2 += rd(data, 2);    h3 += rd(data, 3); \
	h4 += rd(data, 4);   h5 += rd(data, 5);   h6 += rd(data, 6);    h7 += rd(data, 7); \
	h8 += rd(data, 8);   h9 += rd(data, 9);   h10 += rd(data, 10);  h11 += rd(data, 11); \
	EndPartial(h0, h1, h2, h3, h4, h5, h6, h7, h8, h9, h10, h11); \
	EndPartial(h0, h1, h2, h3, h4, h5, h6, h7, h8, h9, h10, h11); \
	EndPartial(h0, h1, h2, h3, h4, h5, h6, h7, h8, h9, h10, h11);

// number of uint64_t's in internal state
#define sc_numVars 12

// size of the internal state
#define sc_blockSize (sc_numVars * 8)

//
// sc_const: a constant which:
//  * is not zero
//  * is odd
//  * is a not-very-regular mix of 1's and 0's
//  * does not need any other special mathematical properties
//
#define sc_const 0xdeadbeefdeadbeefULL

void SpookyHash128(const void* data, size_t size, const uint8_t* seed, uint8_t* digest)
{
	uint64_t h0, h1, h2, h3, h4, h5, h6, h7, h8, h9, h10, h11;
	uint8_t buf[sc_blockSize];
	size_t nblocks;
	const uint8_t* p;
	const uint8_t* end;
	size_t size_remainder;

	h9 = util_read64(seed + 0);
	h10 = util_read64(seed + 8);

	h0 = h3 = h6 = h9;
	h1 = h4 = h7 = h10;
	h2 = h5 = h8 = h11 = sc_const;

	nblocks = size / sc_blockSize;
	p = data;
	end = p + nblocks * sc_blockSize;

	/* body */
	while (p < end) {
		Mix(p, h0, h1, h2, h3, h4, h5, h6, h7, h8, h9, h10, h11);
		p += sc_blockSize;
	}

	/* tail */
	size_remainder = size - nblocks * sc_blockSize;
	memcpy(buf, p, size_remainder);
	memset(buf + size_remainder, 0, sc_blockSize - size_remainder);
	buf[sc_blockSize - 1] = size_remainder;

	/* finalization */
	End(buf, h0, h1, h2, h3, h4, h5, h6, h7, h8, h9, h10, h11);

	util_write64(digest + 0, h0);
	util_write64(digest + 8, h1);
}

