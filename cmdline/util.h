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

/**
 * Return !=0 if the memory region is completely filled with zeros.
 */
static inline int mem_is_zero(const void* ptr, size_t size)
{
	const unsigned char* p = ptr;

	while (size != 0 && ((uintptr_t)p % sizeof(size_t)) != 0) {
		if (*p != 0)
			return 0;
		++p;
		--size;
	}

	while (size >= sizeof(size_t)) {
		if (*(const size_t*)p != 0)
			return 0;
		p += sizeof(size_t);
		size -= sizeof(size_t);
	}

	while (size != 0) {
		if (*p != 0)
			return 0;
		++p;
		--size;
	}

	return 1;
}

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

#define CRC32C_X86_64_BLOCK0_SIZE 5456
#define CRC32C_X86_64_BLOCK1_SIZE 2720
#define CRC32C_X86_64_BLOCK2_SIZE 1360
#define CRC32C_X86_64_BLOCK3_SIZE 672
#define CRC32C_X86_64_BLOCK4_SIZE 336
#define CRC32C_X86_64_BLOCK5_SIZE 160
#ifdef CONFIG_X86_64
extern uint32_t CRC32C_X86_64_SKIP[6][8][16];
#endif

#define CRC32C_ARM64_BLOCK0_SIZE 8192
#define CRC32C_ARM64_BLOCK1_SIZE 4096
#define CRC32C_ARM64_BLOCK2_SIZE 2048
#define CRC32C_ARM64_BLOCK3_SIZE 1024
#define CRC32C_ARM64_BLOCK4_SIZE 512
#define CRC32C_ARM64_BLOCK5_SIZE 256
#if CONFIG_ARM_CRC
extern uint32_t CRC32C_ARM64_SKIP[6][8][16];
#endif

/**
 * Advance a raw CRC-32C by a number of zero bytes.
 * Used to combine independent hardware CRC lanes.
 */
#if defined(CONFIG_X86_64) || CONFIG_ARM_CRC
uint32_t crc32c_shift(uint32_t crc, size_t size);
#endif

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
#ifdef CONFIG_X86_64
static __always_inline uint32_t crc32c_x86_64_skip(uint32_t crc, const uint32_t table[8][16])
{
	return table[0][crc & 0xf]
	       ^ table[1][(crc >> 4) & 0xf]
	       ^ table[2][(crc >> 8) & 0xf]
	       ^ table[3][(crc >> 12) & 0xf]
	       ^ table[4][(crc >> 16) & 0xf]
	       ^ table[5][(crc >> 20) & 0xf]
	       ^ table[6][(crc >> 24) & 0xf]
	       ^ table[7][crc >> 28];
}

static __always_inline uint32_t crc32c_x86_64_3lane(uint32_t crc, const unsigned char* ptr, size_t lane_size, const uint32_t table[8][16])
{
	const unsigned char* ptr0 = ptr;
	const unsigned char* ptr1 = ptr + lane_size;
	const unsigned char* ptr2 = ptr1 + lane_size;
	const unsigned char* end = ptr1;
	uint64_t crc0 = crc;
	uint64_t crc1 = 0;
	uint64_t crc2 = 0;

	/*
	 * Keep three dependency chains in flight to hide the CRC32 latency.
	 * Lane sizes are multiples of 16 to avoid a separate tail loop.
	 */
	while (ptr0 < end) {
		asm volatile ("crc32q %1, %0\n" : "+r" (crc0) : "m" (*(const uint64_t*)ptr0));
		asm volatile ("crc32q %1, %0\n" : "+r" (crc1) : "m" (*(const uint64_t*)ptr1));
		asm volatile ("crc32q %1, %0\n" : "+r" (crc2) : "m" (*(const uint64_t*)ptr2));
		asm volatile ("crc32q %1, %0\n" : "+r" (crc0) : "m" (*(const uint64_t*)(ptr0 + 8)));
		asm volatile ("crc32q %1, %0\n" : "+r" (crc1) : "m" (*(const uint64_t*)(ptr1 + 8)));
		asm volatile ("crc32q %1, %0\n" : "+r" (crc2) : "m" (*(const uint64_t*)(ptr2 + 8)));
		ptr0 += 16;
		ptr1 += 16;
		ptr2 += 16;
	}

	crc = crc32c_x86_64_skip((uint32_t)crc0, table) ^ (uint32_t)crc1;

	return crc32c_x86_64_skip(crc, table) ^ (uint32_t)crc2;
}
#endif

static __always_inline uint32_t crc32c_x86_plain(uint32_t crc, const unsigned char* ptr, size_t size)
{
#ifdef CONFIG_X86_64
	uint64_t crc64 = crc;

	if (size >= 3 * CRC32C_X86_64_BLOCK5_SIZE) {
		while (size >= 3 * CRC32C_X86_64_BLOCK0_SIZE) {
			crc64 = crc32c_x86_64_3lane((uint32_t)crc64, ptr, CRC32C_X86_64_BLOCK0_SIZE, CRC32C_X86_64_SKIP[0]);
			ptr += 3 * CRC32C_X86_64_BLOCK0_SIZE;
			size -= 3 * CRC32C_X86_64_BLOCK0_SIZE;
		}
		while (size >= 3 * CRC32C_X86_64_BLOCK1_SIZE) {
			crc64 = crc32c_x86_64_3lane((uint32_t)crc64, ptr, CRC32C_X86_64_BLOCK1_SIZE, CRC32C_X86_64_SKIP[1]);
			ptr += 3 * CRC32C_X86_64_BLOCK1_SIZE;
			size -= 3 * CRC32C_X86_64_BLOCK1_SIZE;
		}
		while (size >= 3 * CRC32C_X86_64_BLOCK2_SIZE) {
			crc64 = crc32c_x86_64_3lane((uint32_t)crc64, ptr, CRC32C_X86_64_BLOCK2_SIZE, CRC32C_X86_64_SKIP[2]);
			ptr += 3 * CRC32C_X86_64_BLOCK2_SIZE;
			size -= 3 * CRC32C_X86_64_BLOCK2_SIZE;
		}
		while (size >= 3 * CRC32C_X86_64_BLOCK3_SIZE) {
			crc64 = crc32c_x86_64_3lane((uint32_t)crc64, ptr, CRC32C_X86_64_BLOCK3_SIZE, CRC32C_X86_64_SKIP[3]);
			ptr += 3 * CRC32C_X86_64_BLOCK3_SIZE;
			size -= 3 * CRC32C_X86_64_BLOCK3_SIZE;
		}
		while (size >= 3 * CRC32C_X86_64_BLOCK4_SIZE) {
			crc64 = crc32c_x86_64_3lane((uint32_t)crc64, ptr, CRC32C_X86_64_BLOCK4_SIZE, CRC32C_X86_64_SKIP[4]);
			ptr += 3 * CRC32C_X86_64_BLOCK4_SIZE;
			size -= 3 * CRC32C_X86_64_BLOCK4_SIZE;
		}
		while (size >= 3 * CRC32C_X86_64_BLOCK5_SIZE) {
			crc64 = crc32c_x86_64_3lane((uint32_t)crc64, ptr, CRC32C_X86_64_BLOCK5_SIZE, CRC32C_X86_64_SKIP[5]);
			ptr += 3 * CRC32C_X86_64_BLOCK5_SIZE;
			size -= 3 * CRC32C_X86_64_BLOCK5_SIZE;
		}
	}
	while (size >= 16) {
		asm volatile ("crc32q %1, %0\n" : "+r" (crc64) : "m" (*(const uint64_t*)ptr));
		asm volatile ("crc32q %1, %0\n" : "+r" (crc64) : "m" (*(const uint64_t*)(ptr + 8)));
		ptr += 16;
		size -= 16;
	}
	if (size >= 8) {
		asm volatile ("crc32q %1, %0\n" : "+r" (crc64) : "m" (*(const uint64_t*)ptr));
		ptr += 8;
		size -= 8;
	}
	crc = (uint32_t)crc64;
#else
	while (size >= 16) {
		asm volatile ("crc32l %1, %0\n" : "+r" (crc) : "m" (*(const uint32_t*)ptr));
		asm volatile ("crc32l %1, %0\n" : "+r" (crc) : "m" (*(const uint32_t*)(ptr + 4)));
		asm volatile ("crc32l %1, %0\n" : "+r" (crc) : "m" (*(const uint32_t*)(ptr + 8)));
		asm volatile ("crc32l %1, %0\n" : "+r" (crc) : "m" (*(const uint32_t*)(ptr + 12)));
		ptr += 16;
		size -= 16;
	}
	if (size >= 8) {
		asm volatile ("crc32l %1, %0\n" : "+r" (crc) : "m" (*(const uint32_t*)ptr));
		asm volatile ("crc32l %1, %0\n" : "+r" (crc) : "m" (*(const uint32_t*)(ptr + 4)));
		ptr += 8;
		size -= 8;
	}
#endif
	if (size >= 4) {
		asm volatile ("crc32l %1, %0\n" : "+r" (crc) : "m" (*(const uint32_t*)ptr));
		ptr += 4;
		size -= 4;
	}
	if (size >= 2) {
		asm volatile ("crc32w %1, %0\n" : "+r" (crc) : "m" (*(const uint16_t*)ptr));
		ptr += 2;
		size -= 2;
	}
	if (size) {
		asm volatile ("crc32b %1, %0\n" : "+r" (crc) : "m" (*ptr));
	}

	return crc;
}
#endif

#if CONFIG_ARM_CRC
static __always_inline uint32_t crc32c_arm64_skip(uint32_t crc, const uint32_t table[8][16])
{
	return table[0][crc & 0xf]
	       ^ table[1][(crc >> 4) & 0xf]
	       ^ table[2][(crc >> 8) & 0xf]
	       ^ table[3][(crc >> 12) & 0xf]
	       ^ table[4][(crc >> 16) & 0xf]
	       ^ table[5][(crc >> 20) & 0xf]
	       ^ table[6][(crc >> 24) & 0xf]
	       ^ table[7][crc >> 28];
}

static __always_inline uint32_t crc32c_arm64_2lane(uint32_t crc, const unsigned char* ptr, size_t lane_size, const uint32_t table[8][16])
{
	const unsigned char* ptr0 = ptr;
	const unsigned char* ptr1 = ptr + lane_size;
	const unsigned char* end = ptr1;
	uint32_t crc0 = crc;
	uint32_t crc1 = 0;

	/*
	 * Keep two dependency chains in flight to hide the CRC32 latency.
	 */
	while (ptr0 < end) {
		uint64_t val00;
		uint64_t val01;
		uint64_t val02;
		uint64_t val03;
		uint64_t val10;
		uint64_t val11;
		uint64_t val12;
		uint64_t val13;

		__builtin_memcpy(&val00, ptr0, 8);
		__builtin_memcpy(&val10, ptr1, 8);
		__builtin_memcpy(&val01, ptr0 + 8, 8);
		__builtin_memcpy(&val11, ptr1 + 8, 8);
		__builtin_memcpy(&val02, ptr0 + 16, 8);
		__builtin_memcpy(&val12, ptr1 + 16, 8);
		__builtin_memcpy(&val03, ptr0 + 24, 8);
		__builtin_memcpy(&val13, ptr1 + 24, 8);
		asm volatile ("crc32cx %w0, %w0, %x1\n" : "+r" (crc0) : "r" (val00));
		asm volatile ("crc32cx %w0, %w0, %x1\n" : "+r" (crc1) : "r" (val10));
		asm volatile ("crc32cx %w0, %w0, %x1\n" : "+r" (crc0) : "r" (val01));
		asm volatile ("crc32cx %w0, %w0, %x1\n" : "+r" (crc1) : "r" (val11));
		asm volatile ("crc32cx %w0, %w0, %x1\n" : "+r" (crc0) : "r" (val02));
		asm volatile ("crc32cx %w0, %w0, %x1\n" : "+r" (crc1) : "r" (val12));
		asm volatile ("crc32cx %w0, %w0, %x1\n" : "+r" (crc0) : "r" (val03));
		asm volatile ("crc32cx %w0, %w0, %x1\n" : "+r" (crc1) : "r" (val13));
		ptr0 += 32;
		ptr1 += 32;
	}

	return crc32c_arm64_skip(crc0, table) ^ crc1;
}

static __always_inline uint32_t crc32c_arm64_plain(uint32_t crc, const unsigned char* ptr, size_t size)
{
	if (size >= 2 * CRC32C_ARM64_BLOCK5_SIZE) {
		while (size >= 2 * CRC32C_ARM64_BLOCK0_SIZE) {
			crc = crc32c_arm64_2lane(crc, ptr, CRC32C_ARM64_BLOCK0_SIZE, CRC32C_ARM64_SKIP[0]);
			ptr += 2 * CRC32C_ARM64_BLOCK0_SIZE;
			size -= 2 * CRC32C_ARM64_BLOCK0_SIZE;
		}
		while (size >= 2 * CRC32C_ARM64_BLOCK1_SIZE) {
			crc = crc32c_arm64_2lane(crc, ptr, CRC32C_ARM64_BLOCK1_SIZE, CRC32C_ARM64_SKIP[1]);
			ptr += 2 * CRC32C_ARM64_BLOCK1_SIZE;
			size -= 2 * CRC32C_ARM64_BLOCK1_SIZE;
		}
		while (size >= 2 * CRC32C_ARM64_BLOCK2_SIZE) {
			crc = crc32c_arm64_2lane(crc, ptr, CRC32C_ARM64_BLOCK2_SIZE, CRC32C_ARM64_SKIP[2]);
			ptr += 2 * CRC32C_ARM64_BLOCK2_SIZE;
			size -= 2 * CRC32C_ARM64_BLOCK2_SIZE;
		}
		while (size >= 2 * CRC32C_ARM64_BLOCK3_SIZE) {
			crc = crc32c_arm64_2lane(crc, ptr, CRC32C_ARM64_BLOCK3_SIZE, CRC32C_ARM64_SKIP[3]);
			ptr += 2 * CRC32C_ARM64_BLOCK3_SIZE;
			size -= 2 * CRC32C_ARM64_BLOCK3_SIZE;
		}
		while (size >= 2 * CRC32C_ARM64_BLOCK4_SIZE) {
			crc = crc32c_arm64_2lane(crc, ptr, CRC32C_ARM64_BLOCK4_SIZE, CRC32C_ARM64_SKIP[4]);
			ptr += 2 * CRC32C_ARM64_BLOCK4_SIZE;
			size -= 2 * CRC32C_ARM64_BLOCK4_SIZE;
		}
		while (size >= 2 * CRC32C_ARM64_BLOCK5_SIZE) {
			crc = crc32c_arm64_2lane(crc, ptr, CRC32C_ARM64_BLOCK5_SIZE, CRC32C_ARM64_SKIP[5]);
			ptr += 2 * CRC32C_ARM64_BLOCK5_SIZE;
			size -= 2 * CRC32C_ARM64_BLOCK5_SIZE;
		}
	}

	while (size >= 8) {
		uint64_t val;
		__builtin_memcpy(&val, ptr, 8);
		asm volatile ("crc32cx %w0, %w0, %x1\n" : "+r" (crc) : "r" (val));
		ptr += 8;
		size -= 8;
	}
	if (size >= 4) {
		uint32_t val;
		__builtin_memcpy(&val, ptr, 4);
		asm volatile ("crc32cw %w0, %w0, %w1\n" : "+r" (crc) : "r" (val));
		ptr += 4;
		size -= 4;
	}
	while (size) {
		asm volatile ("crc32cb %w0, %w0, %w1\n" : "+r" (crc) : "r" (*ptr));
		++ptr;
		--size;
	}

	return crc;
}
#endif

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
#define HASH_MUSEAIR 3

/**
 * Return the hash that is expected to be the fastest in this architecture
 */
unsigned membesthash(void);

/**
 * Return the name of the hash
 */
const char* memhashname(unsigned kind);

/**
 * Compute the HASH of a memory buffer.
 * Seed is a 128 bit vector.
 *
 * This is the raw hash primitive. It hashes exactly size bytes and does not
 * apply SnapRAID block semantics.
 */
void memhash(unsigned kind, const unsigned char* seed, void* digest, const void* src, size_t size);

/**
 * Compute the hash of a SnapRAID data block.
 *
 * logical_size is the number of bytes logically belonging to the file.
 * block_size is the complete zero-padded RAID block size.
 *
 * Legacy hashes preserve their historical semantics and hash only
 * logical_size bytes.
 *
 * MuseAir hashes the complete canonical RAID block, including the zero
 * padding up to block_size.
 *
 * The caller must guarantee that logical_size <= block_size and, when
 * logical_size < block_size, src[logical_size .. block_size-1] == 0.
 */
void memhash_block(unsigned kind, const unsigned char* seed, void* digest, const void* src, size_t logical_size, size_t block_size);

/**
 * Return !=0 if the hash kind operates on the complete canonical RAID block.
 *
 * Legacy hashes (Murmur3, Spooky2) preserve historical semantics and hash
 * only the logical file bytes.
 * Modern hashes (MuseAir) hash the complete zero-padded RAID block up to
 * block_size.
 */
static inline int memhash_is_block(unsigned kind)
{
	return kind == HASH_MUSEAIR;
}

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
	return (max / BIT_VECT_SIZE) + ((max % BIT_VECT_SIZE) != 0);
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

