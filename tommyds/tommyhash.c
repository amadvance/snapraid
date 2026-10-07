// SPDX-License-Identifier: BSD-2-Clause
// Copyright (C) 2010 Andrea Mazzoleni

#include "tommyhash.h"

/******************************************************************************/
/* hash */

#include <string.h> /* for memcpy, strlen */

/**
 * Swap endianness.
 * They are needed only if BigEndian.
 */
#if defined(__GNUC__)
#define tommy_swap32(x) __builtin_bswap32(x)
#define tommy_swap64(x) __builtin_bswap64(x)
#else
tommy_inline tommy_uint32_t tommy_swap32(tommy_uint32_t v)
{
	return ((v & 0xFF000000) >> 24) |
	       ((v & 0x00FF0000) >> 8) |
	       ((v & 0x0000FF00) << 8) |
	       ((v & 0x000000FF) << 24);
}

tommy_inline tommy_uint64_t tommy_swap64(tommy_uint64_t v)
{
	return ((v & 0xFF00000000000000ULL) >> 56) |
	       ((v & 0x00FF000000000000ULL) >> 40) |
	       ((v & 0x0000FF0000000000ULL) >> 24) |
	       ((v & 0x000000FF00000000ULL) >> 8) |
	       ((v & 0x00000000FF000000ULL) << 8) |
	       ((v & 0x0000000000FF0000ULL) << 24) |
	       ((v & 0x000000000000FF00ULL) << 40) |
	       ((v & 0x00000000000000FFULL) << 56);
}
#endif

tommy_inline tommy_uint32_t tommy_le_uint32_read(const void* ptr)
{
	tommy_uint32_t v;
	memcpy(&v, ptr, sizeof(v));
#if defined(WORDS_BIGENDIAN) || defined(__BIG_ENDIAN__) || \
	(defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
	v = tommy_swap32(v);
#endif
	return v;
}

tommy_inline tommy_uint64_t tommy_le_uint64_read(const void* ptr)
{
	tommy_uint64_t v;
	memcpy(&v, ptr, sizeof(v));
#if defined(WORDS_BIGENDIAN) || defined(__BIG_ENDIAN__) || \
	(defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
	v = tommy_swap64(v);
#endif
	return v;
}

#define tommy_rot(x, k) \
	(((x) << (k)) | ((x) >> (32 - (k))))

#define tommy_rot64(x, k) \
	(((x) << (k)) | ((x) >> (64 - (k))))

TOMMY_API tommy_uint32_t tommy_hash_u32(tommy_uint32_t init_val, const void* void_key, tommy_size_t key_len)
{
	const unsigned char* key = tommy_cast(const unsigned char*, void_key);
	const tommy_uint32_t c1 = 0xcc9e2d51;
	const tommy_uint32_t c2 = 0x1b873593;
	tommy_uint32_t h = init_val;
	tommy_size_t nblocks = key_len / 4;
	const unsigned char* tail = key + nblocks * 4;

	while (nblocks >= 2) {
		tommy_uint32_t k0 = tommy_le_uint32_read(key);
		tommy_uint32_t k1 = tommy_le_uint32_read(key + 4);

		k0 *= c1;
		k1 *= c1;

		k0 = tommy_rot(k0, 15);
		k1 = tommy_rot(k1, 15);

		k0 *= c2;
		k1 *= c2;

		h ^= k0;
		h = tommy_rot(h, 13);
		h = h * 5 + 0xe6546b64;

		h ^= k1;
		h = tommy_rot(h, 13);
		h = h * 5 + 0xe6546b64;

		key += 8;
		nblocks -= 2;
	}

	if (nblocks) {
		tommy_uint32_t k = tommy_le_uint32_read(key);
		k *= c1;
		k = tommy_rot(k, 15);
		k *= c2;
		h ^= k;
		h = tommy_rot(h, 13);
		h = h * 5 + 0xe6546b64;
	}

	tommy_uint32_t k = 0;
	switch (key_len & 3) {
	case 3 : k ^= ((tommy_uint32_t)tail[2]) << 16; /* fallthrough */
	case 2 : k ^= ((tommy_uint32_t)tail[1]) << 8;  /* fallthrough */
	case 1 : k ^= (tommy_uint32_t)tail[0];
		k *= c1;
		k = tommy_rot(k, 15);
		k *= c2;
		h ^= k;
	}

	h ^= (tommy_uint32_t)key_len;
	h ^= h >> 16;
	h *= 0x85ebca6b;
	h ^= h >> 13;
	h *= 0xc2b2ae35;
	h ^= h >> 16;

	return h;
}

TOMMY_API tommy_uint64_t tommy_hash_u64(tommy_uint64_t init_val, const void* void_key, tommy_size_t key_len)
{
	const unsigned char* key = tommy_cast(const unsigned char*, void_key);
	const tommy_uint64_t c1 = 0x87c37b91114253d5ULL;
	const tommy_uint64_t c2 = 0x4cf5ad432745937fULL;
	tommy_uint64_t h = init_val;
	tommy_size_t nblocks = key_len / 8;
	const unsigned char* tail = key + nblocks * 8;

	while (nblocks >= 2) {
		tommy_uint64_t k0 = tommy_le_uint64_read(key);
		tommy_uint64_t k1 = tommy_le_uint64_read(key + 8);

		k0 *= c1;
		k1 *= c1;

		k0 = tommy_rot64(k0, 31);
		k1 = tommy_rot64(k1, 31);

		k0 *= c2;
		k1 *= c2;

		h ^= k0;
		h = tommy_rot64(h, 27);
		h = h * 5 + 0x52dce729ULL;

		h ^= k1;
		h = tommy_rot64(h, 27);
		h = h * 5 + 0x52dce729ULL;

		key += 16;
		nblocks -= 2;
	}

	if (nblocks) {
		tommy_uint64_t k = tommy_le_uint64_read(key);
		k *= c1;
		k = tommy_rot64(k, 31);
		k *= c2;
		h ^= k;
		h = tommy_rot64(h, 27);
		h = h * 5 + 0x52dce729ULL;
	}

	tommy_uint64_t k = 0;
	switch (key_len & 7) {
	case 7 : k ^= ((tommy_uint64_t)tail[6]) << 48; /* fallthrough */
	case 6 : k ^= ((tommy_uint64_t)tail[5]) << 40; /* fallthrough */
	case 5 : k ^= ((tommy_uint64_t)tail[4]) << 32; /* fallthrough */
	case 4 : k ^= ((tommy_uint64_t)tail[3]) << 24; /* fallthrough */
	case 3 : k ^= ((tommy_uint64_t)tail[2]) << 16; /* fallthrough */
	case 2 : k ^= ((tommy_uint64_t)tail[1]) << 8;  /* fallthrough */
	case 1 : k ^= (tommy_uint64_t)tail[0];
		k *= c1;
		k = tommy_rot64(k, 31);
		k *= c2;
		h ^= k;
	}

	h ^= (tommy_uint64_t)key_len;
	h ^= h >> 33;
	h *= 0xff51afd7ed558ccdULL;
	h ^= h >> 33;
	h *= 0xc4ceb9fe1a85ec53ULL;
	h ^= h >> 33;

	return h;
}

TOMMY_API tommy_uint32_t tommy_strhash_u32(tommy_uint32_t init_val, const void* void_key)
{
	const unsigned char* p = tommy_cast(const unsigned char*, void_key);
	const tommy_uint32_t c1 = 0xcc9e2d51;
	const tommy_uint32_t c2 = 0x1b873593;
	tommy_uint32_t h = init_val;
	tommy_size_t len = 0;

	if (((tommy_uintptr_t)p & 3) != 0)
		return tommy_hash_u32(init_val, void_key, strlen(tommy_cast(const char*, void_key)));

	/*
	 * Fast path: string pointer is already aligned to 4 bytes.
	 * This assumes page-granularity protection and page boundaries aligned to
	 * 4 bytes, so each aligned load stays within a readable page.
	 * The final load may read up to 3 bytes past the terminator and outside
	 * the allocated object. Staying within the page avoids a page-boundary
	 * fault, but an out-of-object read is still undefined behavior in ISO C
	 * and can be reported by ASan. This path assumes the compiler and runtime
	 * tolerate these overreads.
	 */
	while (1) {
		tommy_uint32_t v = tommy_le_uint32_read(p);

		if (tommy_haszero_u32(v)) {
			tommy_uint32_t z = (v - 0x01010101) & ~v & 0x80808080;
			tommy_uint_t zero_idx = tommy_ctz_u32(z) / 8;

			if (zero_idx != 0) {
				tommy_uint32_t mask = (1U << (zero_idx * 8)) - 1;
				tommy_uint32_t k = v & mask;
				k *= c1;
				k = tommy_rot(k, 15);
				k *= c2;
				h ^= k;
				len += zero_idx;
			}
			break;
		}

		tommy_uint32_t k = v;
		k *= c1;
		k = tommy_rot(k, 15);
		k *= c2;
		h ^= k;
		h = tommy_rot(h, 13);
		h = h * 5 + 0xe6546b64;

		p += 4;
		len += 4;
	}

	h ^= len;
	h ^= h >> 16;
	h *= 0x85ebca6b;
	h ^= h >> 13;
	h *= 0xc2b2ae35;
	h ^= h >> 16;

	return h;
}

TOMMY_API tommy_uint64_t tommy_strhash_u64(tommy_uint64_t init_val, const void* void_key)
{
	const unsigned char* p = tommy_cast(const unsigned char*, void_key);
	const tommy_uint64_t c1 = 0x87c37b91114253d5ULL;
	const tommy_uint64_t c2 = 0x4cf5ad432745937fULL;
	tommy_uint64_t h = init_val;
	tommy_size_t len = 0;

	if (((tommy_uintptr_t)p & 7) != 0)
		return tommy_hash_u64(init_val, void_key, strlen(tommy_cast(const char*, void_key)));

	/*
	 * Fast path: string pointer is already aligned to 8 bytes.
	 * This assumes page-granularity protection and page boundaries aligned to
	 * 8 bytes, so each aligned load stays within a readable page.
	 * The final load may read up to 7 bytes past the terminator and outside
	 * the allocated object. Staying within the page avoids a page-boundary
	 * fault, but an out-of-object read is still undefined behavior in ISO C
	 * and can be reported by ASan. This path assumes the compiler and runtime
	 * tolerate these overreads.
	 */
	while (1) {
		tommy_uint64_t v = tommy_le_uint64_read(p);

		if (tommy_haszero_u64(v)) {
			tommy_uint64_t z = (v - 0x0101010101010101ULL) & ~v & 0x8080808080808080ULL;
			tommy_uint_t zero_idx = tommy_ctz_u64(z) / 8;

			if (zero_idx != 0) {
				tommy_uint64_t mask = (1ULL << (zero_idx * 8)) - 1;
				tommy_uint64_t k = v & mask;
				k *= c1;
				k = tommy_rot64(k, 31);
				k *= c2;
				h ^= k;
				len += zero_idx;
			}
			break;
		}

		tommy_uint64_t k = v;
		k *= c1;
		k = tommy_rot64(k, 31);
		k *= c2;
		h ^= k;
		h = tommy_rot64(h, 27);
		h = h * 5 + 0x52dce729ULL;

		p += 8;
		len += 8;
	}

	h ^= len;
	h ^= h >> 33;
	h *= 0xff51afd7ed558ccdULL;
	h ^= h >> 33;
	h *= 0xc4ceb9fe1a85ec53ULL;
	h ^= h >> 33;

	return h;
}

