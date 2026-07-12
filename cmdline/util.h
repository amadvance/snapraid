// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2011 Andrea Mazzoleni

#ifndef __UTIL_H
#define __UTIL_H

#include "tommyds/tommytypes.h"

/****************************************************************************/
/* memory */

/**
 * Safe aligned malloc.
 * If no memory is available, it aborts.
 */
void* malloc_nofail_align(size_t size, void** freeptr);

/**
 * Safe aligned malloc. Usable for direct io.
 */
void* malloc_nofail_direct(size_t size, void** freeptr);

/**
 * Safe aligned vector allocation.
 * If no memory is available, it aborts.
 */
void** malloc_nofail_vector_align(int n, size_t size, void** freeptr);

/**
 * Safe page vector allocation. Usable for direct io.
 * If no memory is available, it aborts.
 */
void** malloc_nofail_vector_direct(int n, size_t size, void** freeptr);

/**
 * Safe allocation with memory test.
 */
void* malloc_nofail_test(size_t size);

/**
 * Test the memory vector for RAM problems.
 * If a problem is found, it crashes.
 */
void mtest_vector(int n, size_t size, void** vv);

/****************************************************************************/
/* crc */

/**
 * CRC initial value.
 * Using a not zero value allows to detect a leading run of zeros.
 */
#define CRC_IV 0xffffffffU

/**
 * CRC-32 (Castagnoli) table.
 */
extern uint32_t CRC32C_0[256];
extern uint32_t CRC32C_1[256];
extern uint32_t CRC32C_2[256];
extern uint32_t CRC32C_3[256];

/**
 * If the CPU support the CRC instructions.
 */
#if CONFIG_X86
extern int crc_x86;
#endif

/**
 * Compute CRC-32 (Castagnoli) for a single byte without the IV.
 */
static inline uint32_t crc32c_plain_char(uint32_t crc, unsigned char c)
{
#if CONFIG_X86
	if (tommy_likely(crc_x86)) {
		asm ("crc32b %1, %0\n" : "+r" (crc) : "m" (c));
		return crc;
	}
#endif
#if CONFIG_ARM_CRC
	asm ("crc32cb %w0, %w0, %w1\n" : "+r" (crc) : "r" (c));
	return crc;
#else
	return CRC32C_0[(crc ^ c) & 0xff] ^ (crc >> 8);
#endif
}

/**
 * Compute the CRC-32 (Castagnoli) without the IV.
 */
static inline uint32_t crc32c_gen_plain(uint32_t crc, const unsigned char* ptr, size_t size)
{
	while (size >= 4) {
		crc ^= ptr[0] | (uint32_t)ptr[1] << 8 | (uint32_t)ptr[2] << 16 | (uint32_t)ptr[3] << 24;
		crc = CRC32C_3[crc & 0xff] ^ CRC32C_2[(crc >> 8) & 0xff] ^ CRC32C_1[(crc >> 16) & 0xff] ^ CRC32C_0[crc >> 24];
		ptr += 4;
		size -= 4;
	}

	while (size) {
		crc = CRC32C_0[(crc ^ *ptr) & 0xff] ^ (crc >> 8);
		++ptr;
		--size;
	}

	return crc;
}

/**
 * Compute the CRC-32 (Castagnoli) without the IV.
 */
#if CONFIG_X86
static inline uint32_t crc32c_x86_plain(uint32_t crc, const unsigned char* ptr, size_t size)
{
#ifdef CONFIG_X86_64
	uint64_t crc64 = crc;
	while (size >= 8) {
		asm ("crc32q %1, %0\n" : "+r" (crc64) : "m" (*(const uint64_t*)ptr));
		ptr += 8;
		size -= 8;
	}
	crc = (uint32_t)crc64;
#else
	while (size >= 4) {
		asm ("crc32l %1, %0\n" : "+r" (crc) : "m" (*(const uint32_t*)ptr));
		ptr += 4;
		size -= 4;
	}
#endif
	while (size) {
		asm ("crc32b %1, %0\n" : "+r" (crc) : "m" (*ptr));
		++ptr;
		--size;
	}

	return crc;
}
#endif

#if CONFIG_ARM_CRC
static inline uint32_t crc32c_arm64_plain(uint32_t crc, const unsigned char* ptr, size_t size)
{
	while (size >= 8) {
		uint64_t val;
		__builtin_memcpy(&val, ptr, 8);
		asm ("crc32cx %w0, %w0, %x1\n" : "+r" (crc) : "r" (val));
		ptr += 8;
		size -= 8;
	}
	if (size >= 4) {
		uint32_t val;
		__builtin_memcpy(&val, ptr, 4);
		asm ("crc32cw %w0, %w0, %w1\n" : "+r" (crc) : "r" (val));
		ptr += 4;
		size -= 4;
	}
	while (size) {
		asm ("crc32cb %w0, %w0, %w1\n" : "+r" (crc) : "r" (*ptr));
		++ptr;
		--size;
	}

	return crc;
}
#endif

/**
 * Compute CRC-32 (Castagnoli) without the IV.
 */
static inline uint32_t crc32c_plain(uint32_t crc, const unsigned char* ptr, size_t size)
{
#if CONFIG_X86
	if (tommy_likely(crc_x86)) {
		return crc32c_x86_plain(crc, ptr, size);
	}
#endif
#if CONFIG_ARM_CRC
	return crc32c_arm64_plain(crc, ptr, size);
#else
	return crc32c_gen_plain(crc, ptr, size);
#endif
}

/**
 * Compute the CRC-32 (Castagnoli)
 */
extern uint32_t (*crc32c)(uint32_t crc, const unsigned char* ptr, size_t size);

/**
 * Internal entry points for testing.
 */
uint32_t crc32c_gen(uint32_t crc, const unsigned char* ptr, size_t size);
uint32_t crc32c_x86(uint32_t crc, const unsigned char* ptr, size_t size);
uint32_t crc32c_arm64(uint32_t crc, const unsigned char* ptr, size_t size);

/**
 * Initialize the CRC-32 (Castagnoli) support.
 */
void crc32c_init(void);

/****************************************************************************/
/* hash */

/**
 * Size of the hash.
 */
#define HASH_MAX 16

/**
 * Hash kinds.
 */
#define HASH_UNDEFINED 0
#define HASH_MURMUR3 1
#define HASH_SPOOKY2 2
#define HASH_METRO 3
#define HASH_MUSEAIR 4

/**
 * Return the hash that is expected to be the fastest in this architecture
 */
unsigned membesthash(void);

/**
 * Return the name of the hash
 */
const char* memhashname(unsigned kind);

/**
 * Compute the HASH of a memory block.
 * Seed is a 128 bit vector.
 */
void SpookyHash128(const void* data, size_t size, const uint8_t* seed, uint8_t* digest);
void SpookyHash128SSE(const void* data0, const void* data1, size_t size, const uint8_t* seed0, const uint8_t* seed1, uint8_t* digest0, uint8_t* digest1);
void SpookyHash128AVX(const void* data0, const void* data1, const void* data2, const void* data3, size_t size, const uint8_t* seed0, const uint8_t* seed1, const uint8_t* seed2, const uint8_t* seed3, uint8_t* digest0, uint8_t* digest1, uint8_t* digest2, uint8_t* digest3);
void MuseAirLoongSSE(const void* data0, const void* data1, size_t size, const uint8_t* seed0, const uint8_t* seed1, uint8_t* digest0, uint8_t* digest1);
void MuseAirLoongAVX(const void* data0, const void* data1, const void* data2, const void* data3, size_t size, const uint8_t* seed0, const uint8_t* seed1, const uint8_t* seed2, const uint8_t* seed3, uint8_t* digest0, uint8_t* digest1, uint8_t* digest2, uint8_t* digest3);
void MuseAirLoong(const void* bytes, size_t len, const uint8_t* seed, uint8_t* out);
void memhash(unsigned kind, const unsigned char* seed, void* digest, const void* src, size_t size);

/**
 * Compute the HASH of multiple memory blocks at the same time.
 * Seed is a 128 bit vector.
 */
void memhash_multi(unsigned kind, const unsigned char* seed, void** digest, const void** src, const size_t* size, unsigned count);

/**
 * Return the hash name.
 */
const char* hash_config_name(unsigned kind);

/**
 * Count the number of different bits in the two buffers.
 */
unsigned memdiff(const unsigned char* data1, const unsigned char* data2, size_t size);

/**
 * Unit test
 */
int util_selftest(void);

/****************************************************************************/
/* lock */

/**
 * Create and locks the lock file.
 * Return -1 on error, otherwise it's the file handle to pass to lock_unlock().
 */
int lock_lock(const char* file);

/**
 * Unlock the lock file.
 * Return -1 on error.
 */
int lock_unlock(int f);

/****************************************************************************/
/* bitvect */

typedef unsigned char bit_vect_t;
#define BIT_VECT_SIZE (sizeof(bit_vect_t) * 8)

static inline size_t bit_vect_size(size_t max)
{
	return (max + BIT_VECT_SIZE - 1) / BIT_VECT_SIZE;
}

static inline void bit_vect_set(bit_vect_t* bit_vect, size_t off)
{
	bit_vect_t mask = 1 << (off % BIT_VECT_SIZE);
	bit_vect[off / BIT_VECT_SIZE] |= mask;
}

static inline void bit_vect_clear(bit_vect_t* bit_vect, size_t off)
{
	bit_vect_t mask = 1 << (off % BIT_VECT_SIZE);
	bit_vect[off / BIT_VECT_SIZE] &= ~mask;
}

static inline int bit_vect_test(bit_vect_t* bit_vect, size_t off)
{
	bit_vect_t mask = 1 << (off % BIT_VECT_SIZE);
	return (bit_vect[off / BIT_VECT_SIZE] & mask) != 0;
}

/****************************************************************************/
/* muldiv */

unsigned muldiv(uint64_t v, uint64_t mul, uint64_t div);
unsigned muldiv_upper(uint64_t v, uint64_t mul, uint64_t div);

#endif

