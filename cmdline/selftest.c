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

#include "portable.h"

#include "snapraid.h"
#include "util.h"
#include "raid/raid.h"
#include "raid/cpu.h"
#include "raid/combo.h"
#include "raid/internal.h"
#include "raid/test.h"
#include "elem.h"
#include "state.h"
#include "support.h"
#include "tommyds/tommyhash.h"
#include "tommyds/tommyarray.h"
#include "tommyds/tommyarrayblkof.h"
#include "tommyds/tommyhashdyn.h"

struct hash32_test_vector {
	const char* data;
	int len;
	uint32_t digest;
	uint32_t seed;
};

struct strhash32_test_vector {
	char* data;
	uint32_t digest;
	uint32_t seed;
};

struct hash64_test_vector {
	const char* data;
	int len;
	uint64_t digest;
	uint64_t seed;
};

struct hash_test_vector {
	const char* data;
	int len;
	unsigned char digest[HASH_MAX];
	unsigned char seed[HASH_MAX];
};

/**
 * Test vectors for tommy_hash32
 */
static struct hash32_test_vector TEST_HASH32[] = {
	{ "", 0, 0x8614384c, 0xa766795d },
	{ "a", 1, 0x12c16c36, 0xa766795d },
	{ "abc", 3, 0xc58e8af5, 0xa766795d },
	{ "message digest", 14, 0x006b32f1, 0xa766795d },
	{ "abcdefghijklmnopqrstuvwxyz", 26, 0x7e6fcfe0, 0xa766795d },
	{ "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789", 62, 0x8604adf8, 0xa766795d },
	{ "The quick brown fox jumps over the lazy dog", 43, 0xdeba3d3a, 0xa766795d },
	{ "\x00", 1, 0x4a7d1c33, 0xa766795d },
	{ "\x16\x27", 2, 0x8b50899b, 0xa766795d },
	{ "\xe2\x56\xb4", 3, 0x60406493, 0xa766795d },
	{ "\xc9\x4d\x9c\xda", 4, 0xa049144a, 0xa766795d },
	{ "\x79\xf1\x29\x69\x5d", 5, 0x4da2c2f1, 0xa766795d },
	{ "\x00\x7e\xdf\x1e\x31\x1c", 6, 0x59de30cf, 0xa766795d },
	{ "\x2a\x4c\xe1\xff\x9e\x6f\x53", 7, 0x219e149c, 0xa766795d },
	{ "\xba\x02\xab\x18\x30\xc5\x0e\x8a", 8, 0x25067520, 0xa766795d },
	{ "\xec\x4e\x7a\x72\x1e\x71\x2a\xc9\x33", 9, 0xa1f368d8, 0xa766795d },
	{ "\xfd\xe2\x9c\x0f\x72\xb7\x08\xea\xd0\x78", 10, 0x805fc63d, 0xa766795d },
	{ "\x65\xc4\x8a\xb8\x80\x86\x9a\x79\x00\xb7\xae", 11, 0x7f75dd0f, 0xa766795d },
	{ "\x77\xe9\xd7\x80\x0e\x3f\x5c\x43\xc8\xc2\x46\x39", 12, 0xb9154382, 0xa766795d },
	{ 0, 0, 0, 0 }
};

/**
 * Test vectors for tommy_strhash32
 */
struct strhash32_test_vector TEST_STRHASH32[] = {
	{ "", 0x0af1416d, 0xa766795d },
	{ "a", 0x68fa0f3f, 0xa766795d },
	{ "abc", 0xfc68ffc5, 0xa766795d },
	{ "message digest", 0x08477b63, 0xa766795d },
	{ "abcdefghijklmnopqrstuvwxyz", 0x5b9c25e5, 0xa766795d },
	{ "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789", 0x1e530ce7, 0xa766795d },
	{ "The quick brown fox jumps over the lazy dog", 0xaf93eefe, 0xa766795d },
	{ "\xff", 0xfc88801b, 0xa766795d },
	{ "\x16\x27", 0xcd7216db, 0xa766795d },
	{ "\xe2\x56\xb4", 0x05f98d02, 0xa766795d },
	{ "\xc9\x4d\x9c\xda", 0xf65206f8, 0xa766795d },
	{ "\x79\xf1\x29\x69\x5d", 0x72bd6bda, 0xa766795d },
	{ "\xff\x7e\xdf\x1e\x31\x1c", 0x57dfb9b4, 0xa766795d },
	{ "\x2a\x4c\xe1\xff\x9e\x6f\x53", 0x499ff634, 0xa766795d },
	{ "\xba\x02\xab\x18\x30\xc5\x0e\x8a", 0xe896b7ce, 0xa766795d },
	{ "\xec\x4e\x7a\x72\x1e\x71\x2a\xc9\x33", 0xfe3939f0, 0xa766795d },
	{ "\xfd\xe2\x9c\x0f\x72\xb7\x08\xea\xd0\x78", 0x4351d482, 0xa766795d },
	{ "\x65\xc4\x8a\xb8\x80\x86\x9a\x79\xff\xb7\xae", 0x88e92135, 0xa766795d },
	{ "\x77\xe9\xd7\x80\x0e\x3f\x5c\x43\xc8\xc2\x46\x39", 0x01109c16, 0xa766795d },
	{ "\x87\xd8\x61\x61\x4c\x89\x17\x4e\xa1\xa4\xef\x13\xa9", 0xbcb050dc, 0xa766795d },
	{ "\xfe\xa6\x5b\xc2\xda\xe8\x95\xd4\x64\xab\x4c\x39\x58\x29", 0xbe5e1fd5, 0xa766795d },
	{ "\x94\x49\xc0\x78\xa0\x80\xda\xc7\x71\x4e\x17\x37\xa9\x7c\x40", 0x70d8c97f, 0xa766795d },
	{ "\x53\x7e\x36\xb4\x2e\xc9\xb9\xcc\x18\x3e\x9a\x5f\xfc\xb7\xb0\x61", 0x957440a9, 0xa766795d },
	{ 0, 0, 0 }
};

/**
 * Test vectors for tommy_hash64
 */
static struct hash64_test_vector TEST_HASH64[] = {
	{ "", 0, 0x8614384cb5165fbfULL, 0x2f022773a766795dULL },
	{ "a", 1, 0x1a2e0298a8e94a3dULL, 0x2f022773a766795dULL },
	{ "abc", 3, 0x7555796b7a7d21ebULL, 0x2f022773a766795dULL },
	{ "message digest", 14, 0x9411a57d04b92fb4ULL, 0x2f022773a766795dULL },
	{ "abcdefghijklmnopqrstuvwxyz", 26, 0x3ca3f8d2b4e69832ULL, 0x2f022773a766795dULL },
	{ "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789", 62, 0x6dae542ba0015a4dULL, 0x2f022773a766795dULL },
	{ "The quick brown fox jumps over the lazy dog", 43, 0xe06d8cbb3d2ea1a6ULL, 0x2f022773a766795dULL },
	{ "\x00", 1, 0x201e664fb5f2c021ULL, 0x2f022773a766795dULL },
	{ "\x16\x27", 2, 0xef42fa8032c4b775ULL, 0x2f022773a766795dULL },
	{ "\xe2\x56\xb4", 3, 0x6e6c498a6688466cULL, 0x2f022773a766795dULL },
	{ "\xc9\x4d\x9c\xda", 4, 0x5195005419905423ULL, 0x2f022773a766795dULL },
	{ "\x79\xf1\x29\x69\x5d", 5, 0x221235b48afee7c1ULL, 0x2f022773a766795dULL },
	{ "\x00\x7e\xdf\x1e\x31\x1c", 6, 0x1b1f18b9266f095bULL, 0x2f022773a766795dULL },
	{ "\x2a\x4c\xe1\xff\x9e\x6f\x53", 7, 0x2cbafa8e741d49caULL, 0x2f022773a766795dULL },
	{ "\xba\x02\xab\x18\x30\xc5\x0e\x8a", 8, 0x4677f04c06e0758dULL, 0x2f022773a766795dULL },
	{ "\xec\x4e\x7a\x72\x1e\x71\x2a\xc9\x33", 9, 0x5afe09e8214e2163ULL, 0x2f022773a766795dULL },
	{ "\xfd\xe2\x9c\x0f\x72\xb7\x08\xea\xd0\x78", 10, 0x115b6276d209fab6ULL, 0x2f022773a766795dULL },
	{ "\x65\xc4\x8a\xb8\x80\x86\x9a\x79\x00\xb7\xae", 11, 0xd0636d2f01cf3a3eULL, 0x2f022773a766795dULL },
	{ "\x77\xe9\xd7\x80\x0e\x3f\x5c\x43\xc8\xc2\x46\x39", 12, 0x6d259f5fef74f93eULL, 0x2f022773a766795dULL },
	{ 0, 0, 0, 0 }
};

/**
 * Test vectors for MurmorHash3_x86_128
 */
static struct hash_test_vector TEST_MURMUR3[] = {
#include "murmur3test.c"
	{ 0, 0, { 0 }, { 0 } }
};

/**
 * Test vectors for SpookyHash_128
 */
static struct hash_test_vector TEST_SPOOKY2[] = {
#include "spooky2test.c"
	{ 0, 0, { 0 }, { 0 } }
};

/**
 * Test vectors for MetroHash_128_1
 */
static struct hash_test_vector TEST_METRO[] = {
#include "metrotest.c"
	{ 0, 0, { 0 }, { 0 } }
};

/**
 * Test vectors for MuseAirLoong
 */
static struct hash_test_vector TEST_MUSEAIR[] = {
#include "museairtest.c"
	{ 0, 0, { 0 }, { 0 } }
};

#define HASH_TEST_MAX 512 /* tests are never longer than 512 bytes */

static void test_hash(void)
{
	unsigned i;
	unsigned char* seed_aligned;
	void* seed_alloc;
	unsigned char* buffer_aligned;
	void* buffer_alloc;

	seed_aligned = malloc_nofail_align(HASH_MAX, &seed_alloc);
	buffer_aligned = malloc_nofail_align(HASH_TEST_MAX, &buffer_alloc);

	for (i = 0; TEST_HASH32[i].data; ++i) {
		uint32_t digest;
		memcpy(buffer_aligned, TEST_HASH32[i].data, TEST_HASH32[i].len);
		digest = tommy_hash_u32(TEST_HASH32[i].seed, buffer_aligned, TEST_HASH32[i].len);
		if (digest != TEST_HASH32[i].digest) {
			/* LCOV_EXCL_START */
			log_fatal("Failed hash32 test\n");
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}

	for (i = 0; TEST_STRHASH32[i].data; ++i) {
		uint32_t digest;
		memcpy(buffer_aligned, TEST_STRHASH32[i].data, strlen(TEST_STRHASH32[i].data) + 1);
		digest = tommy_strhash_u32(TEST_STRHASH32[i].seed, buffer_aligned);
		if (digest != TEST_STRHASH32[i].digest) {
			/* LCOV_EXCL_START */
			log_fatal("Failed strhash32 test\n");
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}

	for (i = 0; TEST_HASH64[i].data; ++i) {
		uint64_t digest;
		memcpy(buffer_aligned, TEST_HASH64[i].data, TEST_HASH64[i].len);
		digest = tommy_hash_u64(TEST_HASH64[i].seed, buffer_aligned, TEST_HASH64[i].len);
		if (digest != TEST_HASH64[i].digest) {
			/* LCOV_EXCL_START */
			log_fatal("Failed hash64 test\n");
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}


	for (i = 0; TEST_MURMUR3[i].data; ++i) {
		unsigned char digest[HASH_MAX];
		memcpy(buffer_aligned, TEST_MURMUR3[i].data, TEST_MURMUR3[i].len);
		memcpy(seed_aligned, TEST_MURMUR3[i].seed, HASH_MAX);
		memhash(HASH_MURMUR3, seed_aligned, digest, buffer_aligned, TEST_MURMUR3[i].len);
		if (memcmp(digest, TEST_MURMUR3[i].digest, HASH_MAX) != 0) {
			/* LCOV_EXCL_START */
			log_fatal("Failed Murmur3 test\n");
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}

	for (i = 0; TEST_SPOOKY2[i].data; ++i) {
		unsigned char digest[HASH_MAX];
		memcpy(buffer_aligned, TEST_SPOOKY2[i].data, TEST_SPOOKY2[i].len);
		memcpy(seed_aligned, TEST_SPOOKY2[i].seed, HASH_MAX);
		memhash(HASH_SPOOKY2, seed_aligned, digest, buffer_aligned, TEST_SPOOKY2[i].len);
		if (memcmp(digest, TEST_SPOOKY2[i].digest, HASH_MAX) != 0) {
			/* LCOV_EXCL_START */
			log_fatal("Failed Spooky2 test\n");
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}

	for (i = 0; TEST_METRO[i].data; ++i) {
		unsigned char digest[HASH_MAX];
		memcpy(buffer_aligned, TEST_METRO[i].data, TEST_METRO[i].len);
		memcpy(seed_aligned, TEST_METRO[i].seed, HASH_MAX);
		memhash(HASH_METRO, seed_aligned, digest, buffer_aligned, TEST_METRO[i].len);
		if (memcmp(digest, TEST_METRO[i].digest, HASH_MAX) != 0) {
			/* LCOV_EXCL_START */
			log_fatal("Failed Metro test\n");
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}

	for (i = 0; TEST_MUSEAIR[i].data; ++i) {
		unsigned char digest[HASH_MAX];
		memcpy(buffer_aligned, TEST_MUSEAIR[i].data, TEST_MUSEAIR[i].len);
		memcpy(seed_aligned, TEST_MUSEAIR[i].seed, HASH_MAX);
		memhash(HASH_MUSEAIR, seed_aligned, digest, buffer_aligned, TEST_MUSEAIR[i].len);
		if (memcmp(digest, TEST_MUSEAIR[i].digest, HASH_MAX) != 0) {
			/* LCOV_EXCL_START */
			log_fatal("Failed MuseAir test\n");
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}

	free(buffer_alloc);
	free(seed_alloc);
}

struct crc_test_vector {
	const char* data;
	int len;
	uint32_t digest;
};

/**
 * Test vectors for CRC32C (Castagnoli)
 */
static struct crc_test_vector TEST_CRC32C[] = {
	{ "", 0, 0 },
	{ "\x61", 1, 0xc1d04330 },
	{ "\x66\x6f\x6f", 3, 0xcfc4ae1d },
	{ "\x68\x65\x6c\x6c\x6f\x20\x77\x6f\x72\x6c\x64", 11, 0xc99465aa },
	{ "\x68\x65\x6c\x6c\x6f\x20", 6, 0x7e627e58 },
	{ "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 32, 0x8a9136aa },
	{ "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff", 32, 0x62a8ab43 },
	{ "\x1f\x1e\x1d\x1c\x1b\x1a\x19\x18\x17\x16\x15\x14\x13\x12\x11\x10\x0f\x0e\x0d\x0c\x0b\x0a\x09\x08\x07\x06\x05\x04\x03\x02\x01\x00", 32, 0x113fdb5c },
	{ "\x01\xc0\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x14\x00\x00\x00\x00\x00\x04\x00\x00\x00\x00\x14\x00\x00\x00\x18\x28\x00\x00\x00\x00\x00\x00\x00\x02\x00\x00\x00\x00\x00\x00\x00", 48, 0xd9963a56 },
	{ "\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1a\x1b\x1c\x1d\x1e\x1f", 32, 0x46dd794e },
	{ "\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1a\x1b\x1c\x1d\x1e\x1f\x20\x21\x22\x23\x24\x25\x26\x27\x28", 40, 0x0e2c157f },
	{ "\x29\x2a\x2b\x2c\x2d\x2e\x2f\x30\x31\x32\x33\x34\x35\x36\x37\x38\x39\x3a\x3b\x3c\x3d\x3e\x3f\x40\x41\x42\x43\x44\x45\x46\x47\x48\x49\x4a\x4b\x4c\x4d\x4e\x4f\x50", 40, 0xe980ebf6 },
	{ "\x51\x52\x53\x54\x55\x56\x57\x58\x59\x5a\x5b\x5c\x5d\x5e\x5f\x60\x61\x62\x63\x64\x65\x66\x67\x68\x69\x6a\x6b\x6c\x6d\x6e\x6f\x70\x71\x72\x73\x74\x75\x76\x77\x78", 40, 0xde74bded },
	{ "\x79\x7a\x7b\x7c\x7d\x7e\x7f\x80\x81\x82\x83\x84\x85\x86\x87\x88\x89\x8a\x8b\x8c\x8d\x8e\x8f\x90\x91\x92\x93\x94\x95\x96\x97\x98\x99\x9a\x9b\x9c\x9d\x9e\x9f\xa0", 40, 0xd579c862 },
	{ "\xa1\xa2\xa3\xa4\xa5\xa6\xa7\xa8\xa9\xaa\xab\xac\xad\xae\xaf\xb0\xb1\xb2\xb3\xb4\xb5\xb6\xb7\xb8\xb9\xba\xbb\xbc\xbd\xbe\xbf\xc0\xc1\xc2\xc3\xc4\xc5\xc6\xc7\xc8", 40, 0xba979ad0 },
	{ "\xc9\xca\xcb\xcc\xcd\xce\xcf\xd0\xd1\xd2\xd3\xd4\xd5\xd6\xd7\xd8\xd9\xda\xdb\xdc\xdd\xde\xdf\xe0\xe1\xe2\xe3\xe4\xe5\xe6\xe7\xe8\xe9\xea\xeb\xec\xed\xee\xef\xf0", 40, 0x2b29d913 },
	{ "\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1a\x1b\x1c\x1d\x1e\x1f\x20\x21\x22\x23\x24\x25\x26\x27\x28\x29\x2a\x2b\x2c\x2d\x2e\x2f\x30\x31\x32\x33\x34\x35\x36\x37\x38\x39\x3a\x3b\x3c\x3d\x3e\x3f\x40\x41\x42\x43\x44\x45\x46\x47\x48\x49\x4a\x4b\x4c\x4d\x4e\x4f\x50\x51\x52\x53\x54\x55\x56\x57\x58\x59\x5a\x5b\x5c\x5d\x5e\x5f\x60\x61\x62\x63\x64\x65\x66\x67\x68\x69\x6a\x6b\x6c\x6d\x6e\x6f\x70\x71\x72\x73\x74\x75\x76\x77\x78\x79\x7a\x7b\x7c\x7d\x7e\x7f\x80\x81\x82\x83\x84\x85\x86\x87\x88\x89\x8a\x8b\x8c\x8d\x8e\x8f\x90\x91\x92\x93\x94\x95\x96\x97\x98\x99\x9a\x9b\x9c\x9d\x9e\x9f\xa0\xa1\xa2\xa3\xa4\xa5\xa6\xa7\xa8\xa9\xaa\xab\xac\xad\xae\xaf\xb0\xb1\xb2\xb3\xb4\xb5\xb6\xb7\xb8\xb9\xba\xbb\xbc\xbd\xbe\xbf\xc0\xc1\xc2\xc3\xc4\xc5\xc6\xc7\xc8\xc9\xca\xcb\xcc\xcd\xce\xcf\xd0\xd1\xd2\xd3\xd4\xd5\xd6\xd7\xd8\xd9\xda\xdb\xdc\xdd\xde\xdf\xe0\xe1\xe2\xe3\xe4\xe5\xe6\xe7\xe8\xe9\xea\xeb\xec\xed\xee\xef\xf0", 240, 0x24c5d375 },
	{ 0, 0, 0 }
};

static void test_crc32c(void)
{
	unsigned i;

	for (i = 0; TEST_CRC32C[i].data; ++i) {
		uint32_t digest;
		uint32_t digest_gen;

		digest = crc32c(0, (const unsigned char*)TEST_CRC32C[i].data, TEST_CRC32C[i].len);
		digest_gen = crc32c_gen(0, (const unsigned char*)TEST_CRC32C[i].data, TEST_CRC32C[i].len);

		if (digest != TEST_CRC32C[i].digest || digest_gen != TEST_CRC32C[i].digest) {
			/* LCOV_EXCL_START */
			log_fatal("Failed CRC32C test\n");
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}
}

/**
 * Size of tommy data structures.
 */
#define TOMMY_SIZE 256

static int tommy_test_search(const void* arg, const void* obj)
{
	return arg != obj;
}

static int tommy_test_compare(const void* void_arg_a, const void* void_arg_b)
{
	if (void_arg_a < void_arg_b)
		return -1;
	if (void_arg_a > void_arg_b)
		return 1;
	return 0;
}

static unsigned tommy_test_foreach_count;

static void tommy_test_foreach(void* obj)
{
	(void)obj;

	++tommy_test_foreach_count;
}

static void tommy_test_foreach_arg(void* void_arg, void* obj)
{
	unsigned* arg = void_arg;

	(void)obj;

	++*arg;
}

static void test_tommy(void)
{
	tommy_array array;
	tommy_arrayblkof arrayblkof;
	tommy_list list;
	tommy_hashdyn hashdyn;
	tommy_tree tree;
	tommy_node node[TOMMY_SIZE + 1];
	unsigned i;

	tommy_array_init(&array);
	tommy_arrayblkof_init(&arrayblkof, sizeof(unsigned));

	for (i = 0; i < TOMMY_SIZE; ++i) {
		tommy_array_insert(&array, &node[i]);
		tommy_arrayblkof_grow(&arrayblkof, i + 1);
		*(unsigned*)tommy_arrayblkof_ref(&arrayblkof, i) = i;
	}

	tommy_array_grow(&array, TOMMY_SIZE);
	tommy_arrayblkof_grow(&arrayblkof, TOMMY_SIZE);

	if (tommy_array_memory_usage(&array) < TOMMY_SIZE * sizeof(void*)) {
		/* LCOV_EXCL_START */
		goto bail;
		/* LCOV_EXCL_STOP */
	}
	if (tommy_arrayblkof_memory_usage(&arrayblkof) < TOMMY_SIZE * sizeof(unsigned)) {
		/* LCOV_EXCL_START */
		goto bail;
		/* LCOV_EXCL_STOP */
	}

	for (i = 0; i < TOMMY_SIZE; ++i) {
		if (tommy_array_get(&array, i) != &node[i]) {
			/* LCOV_EXCL_START */
			goto bail;
			/* LCOV_EXCL_STOP */
		}
		if (*(unsigned*)tommy_arrayblkof_ref(&arrayblkof, i) != i) {
			/* LCOV_EXCL_START */
			goto bail;
			/* LCOV_EXCL_STOP */
		}
	}

	tommy_arrayblkof_done(&arrayblkof);
	tommy_array_done(&array);

	tommy_list_init(&list);

	if (!tommy_list_empty(&list)) {
		/* LCOV_EXCL_START */
		goto bail;
		/* LCOV_EXCL_STOP */
	}

	if (tommy_list_tail(&list)) {
		/* LCOV_EXCL_START */
		goto bail;
		/* LCOV_EXCL_STOP */
	}

	if (tommy_list_head(&list)) {
		/* LCOV_EXCL_START */
		goto bail;
		/* LCOV_EXCL_STOP */
	}

	tommy_list_insert_tail(&list, &node[0], &node[0]);

	if (tommy_list_tail(&list) != tommy_list_head(&list)) {
		/* LCOV_EXCL_START */
		goto bail;
		/* LCOV_EXCL_STOP */
	}

	tommy_hashdyn_init(&hashdyn);

	for (i = 0; i < TOMMY_SIZE; ++i)
		tommy_hashdyn_insert(&hashdyn, &node[i], &node[i], i % 64);

	if (tommy_hashdyn_count(&hashdyn) != TOMMY_SIZE) {
		/* LCOV_EXCL_START */
		goto bail;
		/* LCOV_EXCL_STOP */
	}

	if (tommy_hashdyn_memory_usage(&hashdyn) < TOMMY_SIZE * sizeof(tommy_node)) {
		/* LCOV_EXCL_START */
		goto bail;
		/* LCOV_EXCL_STOP */
	}

	tommy_test_foreach_count = 0;
	tommy_hashdyn_foreach(&hashdyn, tommy_test_foreach);
	if (tommy_test_foreach_count != TOMMY_SIZE) {
		/* LCOV_EXCL_START */
		goto bail;
		/* LCOV_EXCL_STOP */
	}

	tommy_test_foreach_count = 0;
	tommy_hashdyn_foreach_arg(&hashdyn, tommy_test_foreach_arg, &tommy_test_foreach_count);
	if (tommy_test_foreach_count != TOMMY_SIZE) {
		/* LCOV_EXCL_START */
		goto bail;
		/* LCOV_EXCL_STOP */
	}

	for (i = 0; i < TOMMY_SIZE / 2; ++i)
		tommy_hashdyn_remove_existing(&hashdyn, &node[i]);

	for (i = 0; i < TOMMY_SIZE / 2; ++i) {
		if (tommy_hashdyn_remove(&hashdyn, tommy_test_search, &node[i], i % 64) != 0) {
			/* LCOV_EXCL_START */
			goto bail;
			/* LCOV_EXCL_STOP */
		}
	}
	for (i = TOMMY_SIZE / 2; i < TOMMY_SIZE; ++i) {
		if (tommy_hashdyn_remove(&hashdyn, tommy_test_search, &node[i], i % 64) == 0) {
			/* LCOV_EXCL_START */
			goto bail;
			/* LCOV_EXCL_STOP */
		}
	}

	if (tommy_hashdyn_count(&hashdyn) != 0) {
		/* LCOV_EXCL_START */
		goto bail;
		/* LCOV_EXCL_STOP */
	}

	tommy_hashdyn_done(&hashdyn);

	tommy_tree_init(&tree, tommy_test_compare);

	for (i = 0; i < TOMMY_SIZE; ++i)
		tommy_tree_insert(&tree, &node[i], (void*)(uintptr_t)(i + 1));

	/* try to insert a duplicate, count should not change */
	tommy_tree_insert(&tree, &node[TOMMY_SIZE], (void*)(uintptr_t)1);

	if (tommy_tree_count(&tree) != TOMMY_SIZE) {
		/* LCOV_EXCL_START */
		goto bail;
		/* LCOV_EXCL_STOP */
	}
	if (tommy_tree_memory_usage(&tree) < TOMMY_SIZE * sizeof(tommy_node)) {
		/* LCOV_EXCL_START */
		goto bail;
		/* LCOV_EXCL_STOP */
	}
	if (tommy_tree_search(&tree, (void*)1) != (void*)1) {
		/* LCOV_EXCL_START */
		goto bail;
		/* LCOV_EXCL_STOP */
	}
	if (tommy_tree_search(&tree, (void*)-1) != 0) {
		/* LCOV_EXCL_START */
		goto bail;
		/* LCOV_EXCL_STOP */
	}
	if (tommy_tree_search_compare(&tree, tommy_test_compare, (void*)1) != (void*)1) {
		/* LCOV_EXCL_START */
		goto bail;
		/* LCOV_EXCL_STOP */
	}
	if (tommy_tree_search_compare(&tree, tommy_test_compare, (void*)-1) != 0) {
		/* LCOV_EXCL_START */
		goto bail;
		/* LCOV_EXCL_STOP */
	}

	tommy_test_foreach_count = 0;
	tommy_tree_foreach(&tree, tommy_test_foreach);
	if (tommy_test_foreach_count != TOMMY_SIZE) {
		/* LCOV_EXCL_START */
		goto bail;
		/* LCOV_EXCL_STOP */
	}

	tommy_test_foreach_count = 0;
	tommy_tree_foreach_arg(&tree, tommy_test_foreach_arg, &tommy_test_foreach_count);
	if (tommy_test_foreach_count != TOMMY_SIZE) {
		/* LCOV_EXCL_START */
		goto bail;
		/* LCOV_EXCL_STOP */
	}

	for (i = 0; i < TOMMY_SIZE / 2; ++i)
		tommy_tree_remove_existing(&tree, &node[i]);

	for (i = 0; i < TOMMY_SIZE / 2; ++i) {
		if (tommy_tree_remove(&tree, (void*)(uintptr_t)(i + 1)) != 0) {
			/* LCOV_EXCL_START */
			goto bail;
			/* LCOV_EXCL_STOP */
		}
	}

	for (i = TOMMY_SIZE / 2; i < TOMMY_SIZE; ++i) {
		if (tommy_tree_remove(&tree, (void*)(uintptr_t)(i + 1)) == 0) {
			/* LCOV_EXCL_START */
			goto bail;
			/* LCOV_EXCL_STOP */
		}
	}

	if (tommy_tree_count(&tree) != 0) {
		/* LCOV_EXCL_START */
		goto bail;
		/* LCOV_EXCL_STOP */
	}

	return;
bail:
	/* LCOV_EXCL_START */
	log_fatal("Failed tommy test\n");
	exit(EXIT_FAILURE);
	/* LCOV_EXCL_STOP */
}

void selftest(void)
{
	log_tag("selftest:\n");
	log_flush();

	msg_progress("Self test...\n");

	/* large file check */
	if (sizeof(off_t) < sizeof(uint64_t)) {
		/* LCOV_EXCL_START */
		log_fatal("Missing support for large files\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	if (util_selftest() != 0) {
		/* LCOV_EXCL_START */
		log_fatal("Failed UTIL test\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}
	test_hash();
	test_crc32c();
	test_tommy();
	if (raid_selftest() != 0) {
		/* LCOV_EXCL_START */
		log_fatal("Failed SELF test\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}
	if (raid_test_sort() != 0) {
		/* LCOV_EXCL_START */
		log_fatal("Failed SORT test\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}
	if (raid_test_insert() != 0) {
		/* LCOV_EXCL_START */
		log_fatal("Failed INSERT test\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}
	if (raid_test_combo() != 0) {
		/* LCOV_EXCL_START */
		log_fatal("Failed COMBO test\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}
	if (raid_test_par(RAID_MODE_VANDERMONDE, 32, 256) != 0) {
		/* LCOV_EXCL_START */
		log_fatal("Failed GEN Vandermonde test\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}
	if (raid_test_rec(RAID_MODE_VANDERMONDE, 12, 256) != 0) {
		/* LCOV_EXCL_START */
		log_fatal("Failed REC Vandermonde test\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}
	if (raid_test_par(RAID_MODE_CAUCHY, 32, 256) != 0) {
		/* LCOV_EXCL_START */
		log_fatal("Failed GEN Cauchy test\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}
	if (raid_test_rec(RAID_MODE_CAUCHY, 12, 256) != 0) {
		/* LCOV_EXCL_START */
		log_fatal("Failed REC Cauchy test\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}
	if (raid_test_par(RAID_MODE_CAUCHY, 1, 256) != 0) {
		/* LCOV_EXCL_START */
		log_fatal("Failed GEN Cauchy test single data disk\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}
}

