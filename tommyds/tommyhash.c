// SPDX-License-Identifier: BSD-2-Clause AND MIT
// Copyright (C) 2010 Andrea Mazzoleni

#include "tommyhash.h"

/******************************************************************************/
/* hash */

#include <string.h> /* for memcpy */

/**
 * Swap endianness.
 * They are needed only if BigEndian.
 */
#if defined(__GNUC__)
#define tommy_imp_hash_swap32(x) __builtin_bswap32(x)
#else
tommy_inline tommy_uint32_t tommy_imp_hash_swap32(tommy_uint32_t v)
{
	return ((v & 0xFF000000) >> 24) |
	       ((v & 0x00FF0000) >> 8) |
	       ((v & 0x0000FF00) << 8) |
	       ((v & 0x000000FF) << 24);
}
#endif

tommy_inline tommy_uint32_t tommy_imp_hash_le_uint32_read(const void* ptr)
{
	tommy_uint32_t v;
	memcpy(&v, ptr, sizeof(v));
#if defined(WORDS_BIGENDIAN) || defined(__BIG_ENDIAN__) || \
	(defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
	v = tommy_imp_hash_swap32(v);
#endif
	return v;
}

/******************************************************************************/
/* a5hash */

/**
 * a5hash32 and a5hash (64-bit) by Aleksey Vaneev, from https://github.com/avaneev/a5hash.
 * Adapted from version 5.25 with little-endian word reads.
 * The 32-bit variant additionally mixes the expanded seed asymmetrically into
 * h[2] and h[3] before processing the input and produces different hash values.
 * Copyright (c) 2025 Aleksey Vaneev. Released under the MIT license, see LICENSES/MIT.
 */

tommy_inline void tommy_mul(tommy_uint32_t* lo, tommy_uint32_t* hi)
{
	/* both operands are 32-bit; the full product supplies two state words */
	tommy_uint64_t v = (tommy_uint64_t)*lo * *hi;
	*lo = (tommy_uint32_t)v;
	*hi = v >> 32;
}

TOMMY_API tommy_uint32_t tommy_hash_u32(tommy_uint32_t init_val, const void* void_key, tommy_size_t key_len)
{
	const unsigned char* key = tommy_cast(const unsigned char*, void_key);
	tommy_uint32_t val01 = 0x55555555;
	tommy_uint32_t val10 = 0xaaaaaaaa;
	tommy_uint32_t h[4] = { 0x243f6a88, 0x85a308d3, 0xfb0bd3ea, 0x0f58fd47 };
	tommy_uint32_t a, b;
	tommy_size_t len = key_len;

	h[0] ^= key_len;
	h[1] ^= key_len;
#if TOMMY_SIZE_BIT == 64
	h[2] = (key_len >> 32) ^ 0x452821e6;
	h[3] = (key_len >> 32) ^ 0x38d01377;
	tommy_mul(&h[2], &h[3]);
#endif
	h[0] ^= init_val & val01;
	h[1] ^= init_val & val10;
	tommy_mul(&h[0], &h[1]);

	/*
	 * Mix the expanded seed asymmetrically into h[2] and h[3] so that short keys
	 * skipping the main loop still depend on the seed, and neither h[0] nor h[1]
	 * cancels out when folding h[0] ^= h[2] and h[1] ^= h[3] at the end.
	 */
	h[2] ^= h[0] ^ h[1];
	h[3] ^= h[0];

	if (len <= 16) {
		if (len >= 4) {
			const unsigned char* tail = key + len - 4;
			a = tommy_imp_hash_le_uint32_read(key);
			b = tommy_imp_hash_le_uint32_read(tail);
			if (len >= 9) {
				tommy_size_t offset = (len / 8) * 4;
				h[2] += tommy_imp_hash_le_uint32_read(key + offset);
				h[3] += tommy_imp_hash_le_uint32_read(tail - offset);
				tommy_mul(&h[2], &h[3]);
			}
		} else {
			a = 0;
			b = 0;
			if (len != 0) {
				a = key[0];
				if (len >= 2)
					a |= key[1] << 8;
				if (len == 3)
					a |= key[2] << 16;
			}
		}
	} else {
		val01 ^= h[0];
		val10 ^= h[1];
		do {
			tommy_uint32_t s1 = h[0];
			tommy_uint32_t s4 = h[3];
			h[0] += tommy_imp_hash_le_uint32_read(key);
			h[1] += tommy_imp_hash_le_uint32_read(key + 4);
			tommy_mul(&h[0], &h[1]);
			h[2] += tommy_imp_hash_le_uint32_read(key + 8);
			h[3] += tommy_imp_hash_le_uint32_read(key + 12);
			tommy_mul(&h[2], &h[3]);
			h[0] += val01;
			h[1] += s4;
			h[2] += s1;
			h[3] += val10;
			key += 16;
			len -= 16;
		} while (len > 16);

		/* the last words may overlap the previous block, but stay within the input */
		const unsigned char* tail = key + len;
		a = tommy_imp_hash_le_uint32_read(tail - 8);
		b = tommy_imp_hash_le_uint32_read(tail - 4);
		if (len >= 9) {
			h[2] += tommy_imp_hash_le_uint32_read(tail - 16);
			h[3] += tommy_imp_hash_le_uint32_read(tail - 12);
			tommy_mul(&h[2], &h[3]);
		}
	}

	h[0] ^= h[2];
	h[1] ^= h[3];
	h[0] += a;
	h[1] += b;
	tommy_mul(&h[0], &h[1]);
	h[0] ^= val01;
	tommy_mul(&h[0], &h[1]);
	return h[0] ^ h[1];
}

TOMMY_API tommy_uint64_t tommy_hash_u64(tommy_uint64_t init_val, const void* void_key, tommy_size_t key_len)
{
	const unsigned char* key = tommy_cast(const unsigned char*, void_key);
	tommy_uint64_t val01 = 0x5555555555555555ULL;
	tommy_uint64_t val10 = 0xaaaaaaaaaaaaaaaaULL;
	tommy_uint64_t h[2] = { (0x452821e638d01377ULL ^ key_len) ^ (init_val & val10), (0x243f6a8885a308d3ULL ^ key_len) ^ (init_val & val01) };
	tommy_size_t len = key_len;

	tommy_mum(&h[0], &h[1]);
	if (len > 16) {
		val01 ^= h[0];
		val10 ^= h[1];
		do {
			/* each 64-bit operand puts the first little-endian word in its high half */
			h[0] ^= (tommy_uint64_t)tommy_imp_hash_le_uint32_read(key) << 32 ^ tommy_imp_hash_le_uint32_read(key + 4);
			h[1] ^= (tommy_uint64_t)tommy_imp_hash_le_uint32_read(key + 8) << 32 ^ tommy_imp_hash_le_uint32_read(key + 12);
			tommy_mum(&h[0], &h[1]);
			h[0] += val01;
			h[1] += val10;
			key += 16;
			len -= 16;
		} while (len > 16);
	}

	if (len >= 4) {
		const unsigned char* tail = key + len - 4;
		tommy_size_t offset = (len / 8) * 4;
		h[0] ^= (tommy_uint64_t)tommy_imp_hash_le_uint32_read(key) << 32 | tommy_imp_hash_le_uint32_read(tail);
		h[1] ^= (tommy_uint64_t)tommy_imp_hash_le_uint32_read(key + offset) << 32 | tommy_imp_hash_le_uint32_read(tail - offset);
	} else if (len != 0) {
		h[0] ^= key[0];
		if (len >= 2)
			h[0] ^= key[1] << 8;
		if (len == 3)
			h[0] ^= key[2] << 16;
	}

	tommy_mum(&h[0], &h[1]);
	h[0] ^= val01;
	tommy_mum(&h[0], &h[1]);
	return h[0] ^ h[1];
}

