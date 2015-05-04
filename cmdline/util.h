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
 * Safe aligned vector allocation.
 * If no memory is available, it aborts.
 */
void** malloc_nofail_vector_align(int nd, int n, size_t size, void** freeptr);

/**
 * Safe allocation with memory test.
 */
void* malloc_nofail_test(size_t size);

/**
 * Tests the memory vector for RAM problems.
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

#if HAVE_SSE42
/**
 * If the CPU support the CRC instructions.
 */
extern int crc_x86;
#endif

/**
 * Computes CRC-32 (Castagnoli) for a single byte without IV.
 */
static inline uint32_t crc32c_plain(uint32_t crc, unsigned char c)
{
#if HAVE_SSE42
	if (tommy_likely(crc_x86)) {
		asm("crc32b %1, %0\n" : "+r" (crc) : "m" (c));
		return crc;
	} else
#endif
	{
		return CRC32C_0[(crc ^ c) & 0xff] ^ (crc >> 8);
	}
}

/**
 * Computes the CRC-32 (Castagnoli)
 */
uint32_t (*crc32c)(uint32_t crc, const unsigned char* ptr, unsigned size);
uint32_t crc32c_gen(uint32_t crc, const unsigned char* ptr, unsigned size);
uint32_t crc32c_x86(uint32_t crc, const unsigned char* ptr, unsigned size);

/**
 * Initializes the CRC-32 (Castagnoli) support.
 */
void crc32c_init(void);

/****************************************************************************/
/* hash */

/**
 * Size of the hash.
 */

#define HASH_SIZE 16

/**
 * Hash kinds.
 */
#define HASH_UNDEFINED 0
#define HASH_MURMUR3 1
#define HASH_SPOOKY2 2

/**
 * Computes the HASH of a memory block.
 * Seed is a 128 bit vector.
 */
void memhash(unsigned kind, const unsigned char* seed, void* digest, const void* src, unsigned size);

/**
 * Flips some bit in the data to make it to match the HASH.
 * Seed is a 128 bit vector.
 * Returns 0 on success.
 */
int memflip(unsigned kind, const unsigned char* seed, const void* digest, void* src, unsigned size);

/**
 * Return the hash name.
 */
const char* hash_config_name(unsigned kind);

/****************************************************************************/
/* lock */

/**
 * Creates and locks the lock file.
 * Returns -1 on error, otherwise it's the file handle to pass to lock_unlock().
 */
int lock_lock(const char* file);

/**
 * Unlocks the lock file.
 * Returns -1 on error.
 */
int lock_unlock(int f);

#endif

