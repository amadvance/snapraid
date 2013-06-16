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
	s0 += data[0];   s2 ^= s10;  s11^= s0;   s0 = rotl64(s0,11);   s11 += s1; \
	s1 += data[1];   s3 ^= s11;  s0 ^= s1;   s1 = rotl64(s1,32);   s0 += s2; \
	s2 += data[2];   s4 ^= s0;   s1 ^= s2;   s2 = rotl64(s2,43);   s1 += s3; \
	s3 += data[3];   s5 ^= s1;   s2 ^= s3;   s3 = rotl64(s3,31);   s2 += s4; \
	s4 += data[4];   s6 ^= s2;   s3 ^= s4;   s4 = rotl64(s4,17);   s3 += s5; \
	s5 += data[5];   s7 ^= s3;   s4 ^= s5;   s5 = rotl64(s5,28);   s4 += s6; \
	s6 += data[6];   s8 ^= s4;   s5 ^= s6;   s6 = rotl64(s6,39);   s5 += s7; \
	s7 += data[7];   s9 ^= s5;   s6 ^= s7;   s7 = rotl64(s7,57);   s6 += s8; \
	s8 += data[8];   s10^= s6;   s7 ^= s8;   s8 = rotl64(s8,55);   s7 += s9; \
	s9 += data[9];   s11^= s7;   s8 ^= s9;   s9 = rotl64(s9,54);   s8 += s10; \
	s10+= data[10];  s0 ^= s8;   s9 ^= s10;  s10= rotl64(s10,22);  s9 += s11; \
	s11+= data[11];  s1 ^= s9;   s10^= s11;  s11= rotl64(s11,46);  s10 += s0;

#define EndPartial(h0, h1, h2, h3, h4, h5, h6, h7, h8, h9, h10, h11) \
	h11+= h1;   h2 ^= h11;  h1 = rotl64(h1,44); \
	h0 += h2;   h3 ^= h0;   h2 = rotl64(h2,15); \
	h1 += h3;   h4 ^= h1;   h3 = rotl64(h3,34); \
	h2 += h4;   h5 ^= h2;   h4 = rotl64(h4,21); \
	h3 += h5;   h6 ^= h3;   h5 = rotl64(h5,38); \
	h4 += h6;   h7 ^= h4;   h6 = rotl64(h6,33); \
	h5 += h7;   h8 ^= h5;   h7 = rotl64(h7,10); \
	h6 += h8;   h9 ^= h6;   h8 = rotl64(h8,13); \
	h7 += h9;   h10^= h7;   h9 = rotl64(h9,38); \
	h8 += h10;  h11^= h8;   h10= rotl64(h10,53); \
	h9 += h11;  h0 ^= h9;   h11= rotl64(h11,42); \
	h10+= h0;   h1 ^= h10;  h0 = rotl64(h0,54);

#define End(data, h0, h1, h2, h3, h4, h5, h6, h7, h8, h9, h10,h11) \
	h0 += data[0];  h1 += data[1];  h2 += data[2];    h3 += data[3]; \
	h4 += data[4];  h5 += data[5];  h6 += data[6];    h7 += data[7]; \
	h8 += data[8];  h9 += data[9];  h10+= data[10];   h11+= data[11]; \
	EndPartial(h0,h1,h2,h3,h4,h5,h6,h7,h8,h9,h10,h11); \
	EndPartial(h0,h1,h2,h3,h4,h5,h6,h7,h8,h9,h10,h11); \
	EndPartial(h0,h1,h2,h3,h4,h5,h6,h7,h8,h9,h10,h11);

// number of uint64_t's in internal state
#define sc_numVars 12

// size of the internal state
#define sc_blockSize (sc_numVars*8)
 
//
// sc_const: a constant which:
//  * is not zero
//  * is odd
//  * is a not-very-regular mix of 1's and 0's
//  * does not need any other special mathematical properties
//
#define sc_const 0xdeadbeefdeadbeefLL

void SpookyHash128(const void* message, size_t length, const uint8_t* seed, uint8_t* digest)
{
	uint64_t h0, h1, h2, h3, h4, h5, h6, h7, h8, h9, h10, h11;
	uint64_t buf[sc_numVars];
	uint64_t* end;
	union {
		const uint8_t* p8;
		uint64_t* p64;
		size_t i;
	} u;
	size_t remainder;
#if WORDS_BIGENDIAN
	unsigned i;
#endif

	h9 = ((uint64_t*)seed)[0];
	h10 = ((uint64_t*)seed)[1];

#if WORDS_BIGENDIAN
	h9 = swap64(h9);
	h10 = swap64(h10);
#endif

	h0 = h3 = h6 = h9;
	h1 = h4 = h7 = h10;
	h2 = h5 = h8 = h11 = sc_const;

	u.p8 = message;
	end = u.p64 + (length/sc_blockSize)*sc_numVars;

	/* body */

	while (u.p64 < end) {
#if WORDS_BIGENDIAN
		for(i=0;i<sc_numVars;++i)
			buf[i] = swap64(u.p64[i]);
		Mix(buf,h0,h1,h2,h3,h4,h5,h6,h7,h8,h9,h10,h11);
#else
		Mix(u.p64,h0,h1,h2,h3,h4,h5,h6,h7,h8,h9,h10,h11);
#endif
		u.p64 += sc_numVars;
	}

	/* tail */

	remainder = (length - ((const uint8_t *)end-(const uint8_t *)message));
	memcpy(buf, end, remainder);
	memset(((uint8_t *)buf)+remainder, 0, sc_blockSize-remainder);
	((uint8_t *)buf)[sc_blockSize-1] = remainder;

	/* finalization */
#if WORDS_BIGENDIAN
	for(i=0;i<sc_numVars;++i)
		buf[i] = swap64(buf[i]);
#endif

	End(buf,h0,h1,h2,h3,h4,h5,h6,h7,h8,h9,h10,h11);

#if WORDS_BIGENDIAN
	h0 = swap64(h0);
	h1 = swap64(h1);
#endif

	((uint64_t*)digest)[0] = h0;
	((uint64_t*)digest)[1] = h1;
}

