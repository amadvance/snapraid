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

#ifndef __UTIL_H
#define __UTIL_H


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
void** malloc_nofail_vector_align(int nd, int n, size_t size, void** freeptr);

/**
 * Safe page vector allocation. Usable for direct io.
 * If no memory is available, it aborts.
 */
void** malloc_nofail_vector_direct(int nd, int n, size_t size, void** freeptr);

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
#if HAVE_SSE42
extern int crc_x86;
#endif

/**
 * Compute CRC-32 (Castagnoli) for a single byte without the IV.
 */
static inline uint32_t crc32c_plain_char(uint32_t crc, unsigned char c)
{
#if HAVE_SSE42
	if (tommy_likely(crc_x86)) {
		asm ("crc32b %1, %0\n" : "+r" (crc) : "m" (c));
		return crc;
	}
#endif
	return CRC32C_0[(crc ^ c) & 0xff] ^ (crc >> 8);
}

/**
 * Compute the CRC-32 (Castagnoli) without the IV.
 */
static inline uint32_t crc32c_gen_plain(uint32_t crc, const unsigned char* ptr, unsigned size)
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
#if HAVE_SSE42
static inline uint32_t crc32c_x86_plain(uint32_t crc, const unsigned char* ptr, unsigned size)
{
#ifdef CONFIG_X86_64
	uint64_t crc64 = crc;
	while (size >= 8) {
		asm ("crc32q %1, %0\n" : "+r" (crc64) : "m" (*(const uint64_t*)ptr));
		ptr += 8;
		size -= 8;
	}
	crc = crc64;
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

/**
 * Compute CRC-32 (Castagnoli) without the IV.
 */
static inline uint32_t crc32c_plain(uint32_t crc, const unsigned char* ptr, unsigned size)
{
#if HAVE_SSE42
	if (tommy_likely(crc_x86)) {
		return crc32c_x86_plain(crc, ptr, size);
	}
#endif
	return crc32c_gen_plain(crc, ptr, size);
}

/**
 * Compute the CRC-32 (Castagnoli)
 */
extern uint32_t (*crc32c)(uint32_t crc, const unsigned char* ptr, unsigned size);

/**
 * Internal entry points for testing.
 */
uint32_t crc32c_gen(uint32_t crc, const unsigned char* ptr, unsigned size);
uint32_t crc32c_x86(uint32_t crc, const unsigned char* ptr, unsigned size);

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

/**
 * Compute the HASH of a memory block.
 * Seed is a 128 bit vector.
 */
void memhash(unsigned kind, const unsigned char* seed, void* digest, const void* src, size_t size);

/**
 * Return the hash name.
 */
const char* hash_config_name(unsigned kind);

/**
 * Count the number of different bits in the two buffers.
 */
unsigned memdiff(const unsigned char* data1, const unsigned char* data2, size_t size);

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

#endif

