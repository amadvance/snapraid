// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2011 Andrea Mazzoleni

#include "os/portable.h"

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
#include "stream.h"
#include "parity.h"
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
			log_fatal(EINTERNAL, "Failed hash32 test\n");
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
			log_fatal(EINTERNAL, "Failed strhash32 test\n");
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
			log_fatal(EINTERNAL, "Failed hash64 test\n");
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
			log_fatal(EINTERNAL, "Failed Murmur3 test\n");
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
			log_fatal(EINTERNAL, "Failed Spooky2 test\n");
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
			log_fatal(EINTERNAL, "Failed MuseAir test\n");
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
			log_fatal(EINTERNAL, "Failed CRC32C test\n");
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}
}

static void test_parity(void)
{
	static const struct {
		unsigned split_mac;
		data_off_t size[2];
		data_off_t physical_reach_size[2];
		data_off_t expected;
	} test[] = {
		{ 1, { 100, 0 }, { 100, 0 }, 100 },
		{ 1, { 100, 0 }, { 90, 0 }, 90 },
		{ 1, { 100, 0 }, { 99, 0 }, 99 },
		{ 2, { 100, 100 }, { 90, 100 }, 90 },
		{ 2, { 100, 100 }, { 100, 70 }, 170 },
		{ 2, { 100, 100 }, { 110, 100 }, 200 },
		{ 2, { 100, 100 }, { 0, 100 }, 0 },
		{ 0, { 0, 0 }, { 0, 0 }, 0 }
	};
	unsigned i;

	for (i = 0; test[i].split_mac != 0; ++i) {
		struct snapraid_parity_handle handle;
		data_off_t size;
		unsigned s;

		memset(&handle, 0, sizeof(handle));
		handle.split_mac = test[i].split_mac;
		for (s = 0; s < handle.split_mac; ++s) {
			handle.split_map[s].size = test[i].size[s];
			handle.split_map[s].physical_reach_size = test[i].physical_reach_size[s];
		}

		parity_physical_reach_size(&handle, &size);
		if (size != test[i].expected) {
			/* LCOV_EXCL_START */
			log_fatal(EINTERNAL, "Failed parity physical reach size test\n");
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
	log_fatal(EINTERNAL, "Failed tommy test\n");
	exit(EXIT_FAILURE);
	/* LCOV_EXCL_STOP */
}

struct {
	const char* pattern;
	const char* text;
	int match_sub;
	int result;
} WNMATCH_TEST[] = {
	/* basic literal matching */
	{ "hello", "hello", 0, 0 },
	{ "hello", "world", 0, 1 },
	{ "", "", 0, 0 },
	{ "hello", "", 0, 1 },
	{ "", "hello", 0, 1 },

	/* single asterisk (*) */
	{ "*", "anything", 0, 0 },
	{ "*", "", 0, 0 },
	{ "*.txt", "file.txt", 0, 0 },
	{ "*.txt", "file.doc", 0, 1 },
	{ "*file*", "myfile.txt", 0, 0 },
	{ "f*le", "file", 0, 0 },
	{ "f*le", "fiiiile", 0, 0 },
	{ "f*le", "folder", 0, 1 },
	{ "a*b*c", "abc", 0, 0 },
	{ "a*b*c", "aXbYc", 0, 0 },
	{ "a*b*c", "aXXbYYc", 0, 0 },
	{ "a*b*c", "ac", 0, 1 },
	{ "***", "anything", 0, 0 },
	{ "*.txt", "notes.txt", 0, 0 },
	{ "*.txt", "folder/notes.txt", 0, 1 },
	{ "*.js", "main.jsx", 0, 1 },

	/* single asterisk with / */
	{ "*", "a/b", 0, 1 },
	{ "*.txt", "dir/file.txt", 0, 1 },
	{ "dir/*.txt", "dir/file.txt", 0, 0 },
	{ "*/*.txt", "dir/file.txt", 0, 0 },
	{ "*/*.txt", "a/b/file.txt", 0, 1 },

	/* question mark (?) */
	{ "?", "a", 0, 0 },
	{ "?", "ab", 0, 1 },
	{ "?", "", 0, 1 },
	{ "file?.txt", "file1.txt", 0, 0 },
	{ "file?.txt", "fileA.txt", 0, 0 },
	{ "file?.txt", "file10.txt", 0, 1 },
	{ "???", "abc", 0, 0 },
	{ "???", "ab", 0, 1 },
	{ "a?c", "abc", 0, 0 },
	{ "a?c", "ac", 0, 1 },
	{ "file?.txt", "file1.txt", 0, 0 },
	{ "file?.txt", "file12.txt", 0, 1 },

	/* question mark with / */
	{ "?", "/", 0, 1 },
	{ "dir?file", "dir/file", 0, 1 },
	{ "dir?file", "dirAfile", 0, 0 },

	/* character classes [...] */
	{ "[abc]", "a", 0, 0 },
	{ "[abc]", "b", 0, 0 },
	{ "[abc]", "c", 0, 0 },
	{ "[abc]", "d", 0, 1 },
	{ "[abc]", "", 0, 1 },
	{ "file[0-9].txt", "file5.txt", 0, 0 },
	{ "file[0-9].txt", "fileA.txt", 0, 1 },
	{ "[a-z]", "m", 0, 0 },
	{ "[A-Z]", "M", 0, 0 },
	{ "[0-9a-f]", "a", 0, 0 },
	{ "[0-9a-f]", "5", 0, 0 },
	{ "[0-9a-f]", "g", 0, 1 },
	{ "[a-z].js", "p.js", 0, 0 },
	{ "[a-z].js", "1.js", 0, 1 },
	{ "[0-9].txt", "a.txt", 0, 1 },
	{ "[!a-z].js", "b.js", 0, 1 },

	/* negated character classes [!...] */
	{ "[!abc]", "d", 0, 0 },
	{ "[!abc]", "a", 0, 1 },
	{ "[!0-9]", "a", 0, 0 },
	{ "[!0-9]", "5", 0, 1 },
	{ "file[!0-9].txt", "filea.txt", 0, 0 },
	{ "file[!0-9].txt", "file5.txt", 0, 1 },
	{ "[^abc]", "d", 0, 0 },
	{ "[^abc]", "a", 0, 1 },

	/* character classes with / */
	{ "[a-z]", "/", 0, 1 },
	{ "dir[/]file", "dir/file", 0, 1 },

#ifdef WIN32
	/* case */
	{ "hello", "HELLO", 0, 0 },
	{ "Hello", "hello", 0, 0 },
	{ "*.TXT", "file.txt", 0, 0 },
	{ "FILE.txt", "file.TXT", 0, 0 },
	{ "[a-z]", "A", 0, 0 },
	{ "[ABC]", "b", 0, 0 },
	{ "[a-z].js", "A.js", 0, 0 },
	{ "[a-z]", "M", 0, 0 },
#else
	{ "hello", "HELLO", 0, 1 },
	{ "Hello", "hello", 0, 1 },
	{ "*.TXT", "file.txt", 0, 1 },
	{ "FILE.txt", "file.TXT", 0, 1 },
	{ "[a-z]", "A", 0, 1 },
	{ "[ABC]", "b", 0, 1 },
	{ "[a-z].js", "A.js", 0, 1 },
	{ "[a-z]", "M", 0, 1 },
#endif

	/* the /xx/ collapse case */
	{ "a/**/b", "a/b", 0, 0 },
	{ "a/**/b", "a/x/b", 0, 0 },

	/* double asterisk (xx/) at start */
	{ "**/*.txt", "file.txt", 0, 0 },
	{ "**/*.txt", "dir/file.txt", 0, 0 },
	{ "**/*.txt", "a/b/c/file.txt", 0, 0 },
	{ "**/*.txt", "file.doc", 0, 1 },
	{ "**/test.txt", "test.txt", 0, 0 },
	{ "**/test.txt", "a/b/test.txt", 0, 0 },
	{ "**/test.txt", "a/b/other.txt", 0, 1 },
	{ "**/*file*", "myfile.txt", 0, 0 },
	{ "**/*file*", "dir/myfile.txt", 0, 0 },

	/* double asterisk (/xx) at end */
	{ "src/**", "src/", 0, 0 },
	{ "src/**", "src/file.c", 0, 0 },
	{ "src/**", "src/a/b/c/file.c", 0, 0 },
	{ "src/**", "other/file.c", 0, 1 },
	{ "src/**", "src", 0, 1 },
	{ "dir/**", "dir/subdir/", 0, 0 },

	/* double asterisk (/xx/) in middle */
	{ "src/**/*.c", "src/file.c", 0, 0 },
	{ "src/**/*.c", "src/lib/file.c", 0, 0 },
	{ "src/**/*.c", "src/lib/util/file.c", 0, 0 },
	{ "src/**/*.c", "src/file.h", 0, 1 },
	{ "src/**/*.c", "other/file.c", 0, 1 },
	{ "a/**/b/**/c", "a/b/c", 0, 0 },
	{ "a/**/b/**/c", "a/x/b/y/c", 0, 0 },
	{ "a/**/b/**/c", "a/x/y/b/z/w/c", 0, 0 },
	{ "a/**/b", "a/x/y/z/b", 0, 0 },
	{ "/docs/**/api.md", "/docs/api.md", 0, 0 },
	{ "a/**/b", "a/xb", 0, 1 },
	{ "a/**/b", "ax/b", 0, 1 },
	{ "a/**/b", "a/c/xb", 0, 1 },
	{ "a/**/b", "ax/c/b", 0, 1 },
	{ "a/**/b", "a/x/y/c", 0, 1 },
	{ "a/**/b", "axb", 0, 1 },

	/* multiple recursion segments */
	{ "**/**/file", "file", 0, 0 },
	{ "**/**/file", "a/b/c/file", 0, 0 },
	{ "a/**/b/**/c", "a/b/c", 0, 0 },
	{ "a/**/b/**/c", "a/1/b/2/c", 0, 0 },

	/* combined patterns */
	{ "*.{txt,doc}", "file.txt", 0, 1 },
	{ "file[0-9]*.txt", "file5abc.txt", 0, 0 },
	{ "dir/*/file?.txt", "dir/sub/file1.txt", 0, 0 },
	{ "**/src/**/*.c", "project/src/lib/file.c", 0, 0 },

	/* edge cases */
	{ "**", "anything", 0, 0 },
	{ "a/**", "a/b", 0, 0 },
	{ "a**", "aaa", 0, 0 },
	{ "**a", "xxa", 0, 0 },
	{ "a/**/", "a/b/", 0, 0 },
	{ "a/**/b", "a/b", 0, 0 },
	{ "a/**/b", "a//b", 0, 0 },
	{ "*/**/file.txt", "a/file.txt", 0, 0 },
	{ "*/**/file.txt", "a/b/c/file.txt", 0, 0 },
	{ "a**/file.txt", "afile.txt", 0, 1 },
	{ "/*/**/file.txt", "/file.txt", 0, 1 },
	{ "*/**/file.txt", "/file.txt", 0, 0 },
	{ "src/**/**/file.txt", "src/file.txt", 0, 0 },
	{ "src/**/**/file.txt", "src/sub/file.txt", 0, 0 },
	{ "src/***/file.txt", "src/file.txt", 0, 0 },
	{ "src/***/file.txt", "src/sub/file.txt", 0, 0 },
	{ "src/***/file.txt", "src/sub/sub/file.txt", 0, 0 },
	{ "src/****/file.txt", "src/file.txt", 0, 0 },
	{ "src/****/file.txt", "src/sub/file.txt", 0, 0 },
	{ "src/****/file.txt", "src/sub/sub/file.txt", 0, 0 },
	{ "**/build", "a/b/build", 0, 0 },
	{ "src/**/test.js", "src/ui/test.js", 0, 0 },
	{ "dist/**", "dist/bin/app.exe", 0, 0 },
	{ "src-**", "src-folder/file.js", 0, 1 },
	{ "**pkg/init.py", "libs/core/pkg/init.py", 0, 1 },
	{ "**.log", "error.log", 0, 0 },
	{ "**.log", "var/log/sys.log", 0, 1 },
	{ "**.jpg", "photo.jpg", 0, 0 },
	{ "**.jpg", "dir/photo.jpg", 0, 1 },
	{ "foo/*.jpg", "foo/photo.jpg", 0, 0 },
	{ "foo/*.jpg", "foo/a/photo.jpg", 0, 1 },
	{ "src/**.js", "src/app.js", 0, 0 },
	{ "src/**.js", "src/components/ui/button.js", 0, 0 }, /* <<<<< .GITIGNORE DIFFERS */
	{ "a**b.txt", "ab.txt", 0, 0 },
	{ "a**b.txt", "axxb.txt", 0, 0 },
	{ "a**b.txt", "a/b.txt", 0, 1 },
	{ "a**b.txt", "a/subdir/b.txt", 0, 1 },
	{ "a**b.txt", "a_folder/sub/b.txt", 0, 1 },
	{ "a**b.txt", "a_folder/sub/folter_b.txt", 0, 1 },
	{ "**/build", "build", 0, 0 },
	{ "**/build", "project/out/build", 0, 0 },

	/* negative tests */
	{ "/docs/**/api.md", "docs/api.txt", 0, 1 },
	{ "a/**/b", "a/b/c", 0, 1 },
	{ "static/**", "static", 0, 1 },
	{ "src/*.js", "src/ui/app.js", 0, 1 },
	{ "/config.*", "etc/config.json", 0, 1 },
	{ "*/*/*.c", "main.c", 0, 1 },
	{ "a/*/b", "a/b", 0, 1 },
	{ "a/**/b", "axb", 0, 1 },
	{ "a/**/b", "ab", 0, 1 },
	{ "/**/logs", "logs", 0, 1 },
	{ "/src/**/logs", "src/web/log", 0, 1 },
	{ "foo//bar", "foo/bar", 0, 1 },
	{ "a/**/b", "a/b/", 0, 1 },
	{ "a**b**c", "acb", 0, 1 },
	{ "src-**-pkg", "src-pkg", 0, 1 },
	{ "**/test/*.js", "test/ui/app.js", 0, 1 },

	/* complex real-world patterns */
	{ "**/.git/**", ".git/config", 0, 0 },
	{ "**/.git/**", "project/.git/hooks/pre-commit", 0, 0 },
	{ "**/node_modules/**", "node_modules/pkg/index.js", 0, 0 },
	{ "**/node_modules/**", "app/node_modules/pkg/file.js", 0, 0 },
	{ "src/**/*.{c,h}", "src/main.c", 0, 1 },
	{ "**/test_*.py", "test_example.py", 0, 0 },
	{ "**/test_*.py", "tests/test_feature.py", 0, 0 },

	/* performance/stress patterns */
	{ "a*b*c*d*e*f*g*h*i*j*k", "abcdefghijk", 0, 0 },
	{ "a*b*c*d*e*f*g*h*i*j*k", "aXbXcXdXeXfXgXhXiXjXk", 0, 0 },
	{ "**/**/**/*.txt", "a/b/c/d/e/f.txt", 0, 0 },

	/* trailing/leading slashes */
	{ "dir/", "dir/", 0, 0 },
	{ "/root/*", "/root/file", 0, 0 },

	/* complex embedded patterns */
	{ "src-**-pkg/*.js", "src-web-pkg/main.js", 0, 0 },
	{ "src-**-pkg/*.js", "src-lazy-load-ui-pkg/main.js", 0, 0 },
	{ "src-**-pkg/*.js", "src-web-pkg/subdir/main.js", 0, 1 },

	/* anchoring and slashes */
	{ "/root.txt", "root.txt", 0, 1 },
	{ "/root.txt", "subdir/root.txt", 0, 1 },
	{ "docs/", "docs", 0, 1 },
	{ "docs/", "docs/", 0, 0 },
	{ "**/temp/", "src/temp/", 0, 0 },
	{ "**/temp/", "src/temp", 0, 1 },

	/* directory subtree matching (match_sub = 1) */
	{ "Movies*", "Movies4/file.mkv", 1, 0 },
	{ "Movies*", "Movies4/sub/file.mkv", 1, 0 },
	{ "Movies*", "Movies/file.mkv", 1, 0 },
	{ "Movies*", "MoviesCollection/file.mkv", 1, 0 },
	{ "Movies*", "Movies4", 1, 1 },
	{ "Movies*", "Other/file.mkv", 1, 1 },
	{ "foo/Movies*", "foo/Movies4/file.mkv", 1, 0 },
	{ "foo/Movies*", "foo/Other/file.mkv", 1, 1 },
	{ "Movies*", "Movies4", 0, 0 },
	{ "Movies*", "Movies4/file.mkv", 0, 1 },

	/* escaping of wildcard metacharacters */
#ifdef _WIN32
	{ "foo^*bar", "foo*bar", 0, 0 },
	{ "foo^?bar", "foo?bar", 0, 0 },
	{ "foo^[bar", "foo[bar", 0, 0 },
	{ "foo^]bar", "foo]bar", 0, 0 },
	{ "foo^^bar", "foo^bar", 0, 0 },
	{ "foo^*bar", "fooXXXbar", 0, 1 },
	{ "foo^?bar", "fooXbar", 0, 1 },
	{ "foo^[bar", "fooXbar", 0, 1 },
	{ "foo^]bar", "fooXbar", 0, 1 },
	{ "foo*bar", "fooXXXbar", 0, 0 },
	{ "foo?bar", "fooXbar", 0, 0 },
	{ "foo[abc]bar", "fooabar", 0, 0 },
	{ "foo^abar", "foo^abar", 0, 0 },
	{ "foo^abar", "fooabar", 0, 1 },
	{ "foo^", "foo^", 0, 0 },
	{ "foo^", "foo", 0, 1 },
	{ "foo^bar", "foo^bar", 0, 0 },
#else
	{ "foo\\*bar", "foo*bar", 0, 0 },
	{ "foo\\?bar", "foo?bar", 0, 0 },
	{ "foo\\[bar", "foo[bar", 0, 0 },
	{ "foo\\]bar", "foo]bar", 0, 0 },
	{ "foo\\\\bar", "foo\\bar", 0, 0 },
	{ "foo\\*bar", "fooXXXbar", 0, 1 },
	{ "foo\\?bar", "fooXbar", 0, 1 },
	{ "foo\\[bar", "fooXbar", 0, 1 },
	{ "foo\\]bar", "fooXbar", 0, 1 },
	{ "foo*bar", "fooXXXbar", 0, 0 },
	{ "foo?bar", "fooXbar", 0, 0 },
	{ "foo[abc]bar", "fooabar", 0, 0 },
	{ "foo\\abar", "foo\\abar", 0, 0 },
	{ "foo\\abar", "fooabar", 0, 1 },
	{ "foo\\", "foo\\", 0, 0 },
	{ "foo\\", "foo", 0, 1 },
	{ "foo^bar", "foo^bar", 0, 0 },
#endif

	{ 0 }
};

static const struct {
	const char* pattern;
	const char* text;
	int match_sub;
	int result;
} WNMATCH_ABSOLUTE_TEST[] = {
	/* an absolute filter skips its slash but preserves it as initial context */
	{ "**.jpg", "photo.jpg", 0, 0 },
	{ "**.jpg", "dir/photo.jpg", 0, 0 },
	{ "**.jpg", "a/b/photo.jpg", 0, 0 },
	{ "**.jpg", "a/b/photo.png", 0, 1 },
	{ "**foo", "foo", 0, 0 },
	{ "**foo", "a/b/foo", 0, 0 },
	{ "**foo", "a/b/bar", 0, 1 },
	{ "**", "a/b/file", 0, 0 },
	{ "**foo", "a/b/foo/file", 1, 0 },
	{ "**foo", "a/b/bar/file", 1, 1 },
	{ "**/foo", "foo", 0, 0 },
	{ "**/foo", "a/b/foo", 0, 0 },
	{ "foo/**/bar", "foo/a/b/bar", 0, 0 },
	{ 0 }
};

static void test_wnmatch(void)
{
	for (int i = 0; WNMATCH_TEST[i].pattern; ++i) {
		if (wnmatch_sub(WNMATCH_TEST[i].pattern, WNMATCH_TEST[i].text, WNMATCH_TEST[i].match_sub) != WNMATCH_TEST[i].result) {
			/* LCOV_EXCL_START */
			log_fatal(EINTERNAL, "Failed wnmatch test %s %s, expected %d\n", WNMATCH_TEST[i].pattern, WNMATCH_TEST[i].text, WNMATCH_TEST[i].result);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}

	for (int i = 0; WNMATCH_ABSOLUTE_TEST[i].pattern; ++i) {
		if (wnmatch_sub_prev(WNMATCH_ABSOLUTE_TEST[i].pattern, WNMATCH_ABSOLUTE_TEST[i].text, WNMATCH_ABSOLUTE_TEST[i].match_sub, '/') != WNMATCH_ABSOLUTE_TEST[i].result) {
			/* LCOV_EXCL_START */
			log_fatal(EINTERNAL, "Failed absolute wnmatch test %s %s, expected %d\n", WNMATCH_ABSOLUTE_TEST[i].pattern, WNMATCH_ABSOLUTE_TEST[i].text, WNMATCH_ABSOLUTE_TEST[i].result);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}
}

static void test_path(void)
{
	char dst[PATH_MAX];

	/*
	 * Use strcmp instead of pathcmp because pathcmp on Windows treats
	 * '/' and '\' as equivalent, which would prevent detecting bugs in
	 * separator conversion by pathimport/pathexport.
	 */
#ifdef _WIN32
	pathimport(dst, sizeof(dst), "foo\\bar");
	if (strcmp(dst, "foo/bar") != 0) {
		/* LCOV_EXCL_START */
		log_fatal(EINTERNAL, "Failed pathimport Windows separator test\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	pathimport(dst, sizeof(dst), "foo^bar");
	if (strcmp(dst, "foo^bar") != 0) {
		/* LCOV_EXCL_START */
		log_fatal(EINTERNAL, "Failed pathimport Windows caret test\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	pathexport(dst, sizeof(dst), "foo/bar");
	if (strcmp(dst, "foo\\bar") != 0) {
		/* LCOV_EXCL_START */
		log_fatal(EINTERNAL, "Failed pathexport Windows separator test\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	pathexport(dst, sizeof(dst), "foo^bar");
	if (strcmp(dst, "foo^bar") != 0) {
		/* LCOV_EXCL_START */
		log_fatal(EINTERNAL, "Failed pathexport Windows caret test\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}
#else
	pathimport(dst, sizeof(dst), "foo/bar");
	if (strcmp(dst, "foo/bar") != 0) {
		/* LCOV_EXCL_START */
		log_fatal(EINTERNAL, "Failed pathimport Unix test\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	pathimport(dst, sizeof(dst), "foo^bar");
	if (strcmp(dst, "foo^bar") != 0) {
		/* LCOV_EXCL_START */
		log_fatal(EINTERNAL, "Failed pathimport Unix caret test\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	pathexport(dst, sizeof(dst), "foo/bar");
	if (strcmp(dst, "foo/bar") != 0) {
		/* LCOV_EXCL_START */
		log_fatal(EINTERNAL, "Failed pathexport Unix test\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	pathexport(dst, sizeof(dst), "foo^bar");
	if (strcmp(dst, "foo^bar") != 0) {
		/* LCOV_EXCL_START */
		log_fatal(EINTERNAL, "Failed pathexport Unix caret test\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}
#endif

	static const struct {
		const char* path;
		int expected;
	} sub_tests[] = {
		{ "", 0 },
		{ "/", 0 },
		{ "/a", 0 },
		{ "a/", 0 },
		{ "a//b", 0 },
		{ ".", 0 },
		{ "..", 0 },
		{ "./a", 0 },
		{ "../a", 0 },
		{ "a/.", 0 },
		{ "a/..", 0 },
		{ "a/./b", 0 },
		{ "a/../b", 0 },
#ifdef _WIN32
		{ "\\a", 0 },
		{ "a\\b", 0 },
		{ "C:a", 0 },
		{ "C:/a", 0 },
		{ "d:file.txt", 0 },
#else
		{ "\\a", 1 },
		{ "a\\b", 1 },
		{ "C:a", 1 },
		{ "C:/a", 1 },
		{ "d:file.txt", 1 },
#endif
		{ "a", 1 },
		{ "file.txt", 1 },
		{ "dir/file.txt", 1 },
		{ "a/b/c/d.txt", 1 },
		{ ".hidden", 1 },
		{ "dir/.hidden", 1 },
		{ ".../file", 1 },
		{ "..file", 1 },
		{ "file..", 1 },
		{ "a/b:c/d", 1 },
	};
	for (unsigned i = 0; i < sizeof(sub_tests) / sizeof(sub_tests[0]); ++i) {
		if (path_is_sub(sub_tests[i].path) != sub_tests[i].expected) {
			/* LCOV_EXCL_START */
			log_fatal(EINTERNAL, "Failed path_is_sub test for '%s', expected %d\n",
				sub_tests[i].path, sub_tests[i].expected);
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}
}

struct {
	const char* pattern;
	int valid;
} FILTER_TEST[] = {
	/* invalid empty pattern and path components */
	{ "", 0 },
	{ "foo//bar", 0 },
	{ "foo/./bar", 0 },
	{ "foo/../bar", 0 },
	{ "foo/.", 0 },
	{ "foo/..", 0 },

	/* invalid standalone root and standalone double-star patterns */
	{ "/", 0 },
	{ "/**/", 0 },
	{ ".", 0 },
	{ "..", 0 },
	{ "/./", 0 },
	{ "/../", 0 },

	/* valid single and multi-level patterns */
	{ "...", 1 },
	{ ".../", 1 },
	{ "/.../", 1 },
	{ "....", 1 },
	{ "/foo/.../bar", 1 },
	{ "/foo/", 1 },
	{ "/foo/**/", 1 },
	{ "/**/foo/", 1 },
	{ "/foo/**/bar", 1 },
	{ "/foo/**/bar/", 1 },
	{ "foo/**/bar", 1 },
	{ "foo/**/", 1 },
	{ "**", 1 },
	{ "**/", 1 },
	{ "foo/.hidden", 1 },
	{ "foo/..hidden", 1 },

	/* invalid unterminated character classes */
	{ "[", 0 },
	{ "foo[", 0 },
	{ "[abc", 0 },
	{ "foo/[ab", 0 },
	{ "/foo/[ab/bar", 0 },
	{ "foo/[a-z", 0 },

	/* valid character classes */
	{ "[abc]", 1 },
	{ "foo[abc]", 1 },
	{ "foo/[ab]/bar", 1 },
	{ "foo/[a-z]/bar", 1 },

#ifdef _WIN32
	/* escaped '[' is literal */
	{ "foo^[bar", 1 },

	/* escaped '^' followed by '[' starts a class */
	{ "foo^^[bar", 0 },
#else
	/* escaped '[' is literal */
	{ "foo\\[bar", 1 },

	/* escaped '\' followed by '[' starts a class */
	{ "foo\\\\[bar", 0 },
#endif

	{ 0, 0 }
};

static const struct {
	const char* pattern;
	int valid;
} FILTER_DISK_TEST[] = {
	{ "", 0 },
	{ "disk/path", 0 },
	{ "disk", 1 },
	{ "disk?", 1 },
	{ "data*", 1 },
	{ "[ab", 0 },
	{ "disk[12", 0 },
	{ "disk[12]", 1 },
	{ 0, 0 }
};

static void test_filter_validity(void)
{
	for (int i = 0; FILTER_TEST[i].pattern; ++i) {
		struct snapraid_filter* f = filter_alloc_file(1, "", FILTER_TEST[i].pattern);
		if ((f != 0) != FILTER_TEST[i].valid) {
			/* LCOV_EXCL_START */
			log_fatal(EINTERNAL, "Failed filter test '%s', expected %s\n",
				FILTER_TEST[i].pattern, FILTER_TEST[i].valid ? "valid" : "invalid");
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
		if (f != 0)
			filter_free(f);
	}

	for (int i = 0; FILTER_DISK_TEST[i].pattern; ++i) {
		struct snapraid_filter* f = filter_alloc_disk(1, FILTER_DISK_TEST[i].pattern);
		if ((f != 0) != FILTER_DISK_TEST[i].valid) {
			/* LCOV_EXCL_START */
			log_fatal(EINTERNAL, "Failed disk filter test '%s', expected %s\n",
				FILTER_DISK_TEST[i].pattern, FILTER_DISK_TEST[i].valid ? "valid" : "invalid");
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
		if (f != 0)
			filter_free(f);
	}
}

struct filter_rule_spec {
	int direction;      /* 1 = include, -1 = exclude, 0 = end of rules */
	int is_disk;        /* 1 = disk rule, 0 = file/dir rule */
	const char* root;   /* "" for global, or "dir/" for scoped */
	const char* pattern;
};

struct filter_test_case {
	const char* disk;
	const char* path;
	int exp_path;       /* expected return from filter_path: 0 = include, -1 = exclude */
	int exp_subdir;     /* expected return from filter_subdir: 0 = include (traverse), -1 = exclude */
	int exp_emptydir;   /* expected return from filter_emptydir: 0 = include, -1 = exclude */
};

struct filter_scenario {
	const char* desc;
	struct filter_rule_spec rules[8];
	struct filter_test_case cases[12];
};

static const struct filter_scenario FILTER_SCENARIOS[] = {
	{
		"empty filter list (default include)",
		{ { 0 } },
		{
			{ "d1", "file.txt", 0, 0, 0 },
			{ "d1", "sub/file.txt", 0, 0, 0 },
			{ "d1", "dir", 0, 0, 0 },
			{ 0 }
		}
	},
	{
		"only exclude rules",
		{
			{ -1, 0, "", "*.tmp" },
			{ -1, 0, "", "/cache/" },
			{ -1, 0, "", "tmp/" },
			{ 0 }
		},
		{
			{ "d1", "file.tmp", -1, 0, 0 },
			{ "d1", "sub/file.tmp", -1, 0, 0 },
			{ "d1", "cache", 0, -1, -1 },
			{ "d1", "cache/data.bin", -1, 0, 0 },
			{ "d1", "sub/tmp/data.bin", -1, 0, 0 },
			{ "d1", "sub/tmp", 0, -1, -1 },
			{ "d1", "doc.pdf", 0, 0, 0 },
			{ "d1", "other/doc.pdf", 0, 0, 0 },
			{ "d1", "other", 0, 0, 0 },
			{ 0 }
		}
	},
	{
		"only include rules",
		{
			{ 1, 0, "", "*.mp3" },
			{ 1, 0, "", "/music/" },
			{ 0 }
		},
		{
			{ "d1", "song.mp3", 0, 0, -1 },
			{ "d1", "audio/song.mp3", 0, 0, -1 },
			{ "d1", "music/track.flac", 0, 0, -1 },
			{ "d1", "music", -1, 0, 0 },
			{ "d1", "photo.jpg", -1, 0, -1 },
			{ "d1", "photos/photo.jpg", -1, 0, -1 },
			{ "d1", "photos", -1, 0, -1 },
			{ 0 }
		}
	},
	{
		"directory exclude prunes scanner before file include can match",
		{
			{ 1, 0, "", "/keep/file.dat" },
			{ -1, 0, "", "/keep/" },
			{ 0 }
		},
		{
			{ "d1", "keep", 0, -1, -1 },
			{ "d1", "keep/file.dat", 0, 0, 0 },
			{ "d1", "keep/other.dat", -1, 0, 0 },
			{ "d1", "other/file.txt", 0, 0, 0 },
			{ 0 }
		}
	},
	{
		"file exclude inside directory allows scanner to find specific file include",
		{
			{ 1, 0, "", "/keep/file.dat" },
			{ -1, 0, "", "/keep/*" },
			{ 0 }
		},
		{
			{ "d1", "keep", 0, 0, 0 },
			{ "d1", "keep/file.dat", 0, 0, 0 },
			{ "d1", "keep/other.dat", -1, 0, 0 },
			{ "d1", "other/file.txt", 0, 0, 0 },
			{ 0 }
		}
	},
	{
		"include exception before exclude rule",
		{
			{ 1, 0, "", "/logs/important.log" },
			{ -1, 0, "", "*.log" },
			{ 0 }
		},
		{
			{ "d1", "logs/important.log", 0, 0, 0 },
			{ "d1", "logs/app.log", -1, 0, 0 },
			{ "d1", "logs/readme.txt", 0, 0, 0 },
			{ 0 }
		}
	},
	{
		"exclude rule before include exception",
		{
			{ -1, 0, "", "*.log" },
			{ 1, 0, "", "/logs/important.log" },
			{ 0 }
		},
		{
			{ "d1", "logs/important.log", -1, 0, -1 },
			{ "d1", "logs/app.log", -1, 0, -1 },
			{ "d1", "logs/readme.txt", -1, 0, -1 },
			{ 0 }
		}
	},
	{
		"last unmatched rule defines mixed-list default",
		{
			{ -1, 0, "", "*.tmp" },
			{ 1, 0, "", "/media/" },
			{ 0 }
		},
		{
			{ "d1", "cache.tmp", -1, 0, -1 },
			{ "d1", "media", -1, 0, 0 },
			{ "d1", "media/movie.mkv", 0, 0, -1 },
			{ "d1", "docs/readme.txt", -1, 0, -1 },
			{ 0 }
		}
	},
	{
		"directory rule vs file rule semantics",
		{
			{ -1, 0, "", "/logs/" },
			{ -1, 0, "", "/report" },
			{ 0 }
		},
		{
			{ "d1", "logs", 0, -1, -1 },
			{ "d1", "logs/a.txt", -1, 0, 0 },
			{ "d1", "report", -1, 0, 0 },
			{ "d1", "report/a.txt", 0, 0, 0 },
			{ 0 }
		}
	},
	{
		"absolute vs relative rules",
		{
			{ -1, 0, "", "/abs_tmp/" },
			{ -1, 0, "", "rel_tmp/" },
			{ 0 }
		},
		{
			{ "d1", "abs_tmp", 0, -1, -1 },
			{ "d1", "abs_tmp/file.txt", -1, 0, 0 },
			{ "d1", "sub/abs_tmp/file.txt", 0, 0, 0 },
			{ "d1", "rel_tmp", 0, -1, -1 },
			{ "d1", "rel_tmp/file.txt", -1, 0, 0 },
			{ "d1", "sub/rel_tmp", 0, -1, -1 },
			{ "d1", "sub/rel_tmp/file.txt", -1, 0, 0 },
			{ 0 }
		}
	},
	{
		"disk filters",
		{
			{ 1, 1, "", "d1" },
			{ -1, 1, "", "d2" },
			{ 0 }
		},
		{
			{ "d1", "file.txt", 0, 0, 0 },
			{ "d2", "file.txt", -1, -1, -1 },
			{ "d3", "file.txt", 0, 0, 0 },
			{ 0 }
		}
	},
	{
		"scoped local root filter",
		{
			{ -1, 0, "projects/sub/", "*.o" },
			{ 0 }
		},
		{
			{ "d1", "projects/sub/main.o", -1, 0, 0 },
			{ "d1", "projects/main.o", 0, 0, 0 },
			{ "d1", "other/main.o", 0, 0, 0 },
			{ 0 }
		}
	},
	{
		"scoped root boundary and local absolute rule",
		{
			{ -1, 0, "projects/sub/", "/build/" },
			{ 0 }
		},
		{
			{ "d1", "projects/sub/build", 0, -1, -1 },
			{ "d1", "projects/sub/build/output.o", -1, 0, 0 },
			{ "d1", "projects/sub/nested/build", 0, 0, 0 },
			{ "d1", "projects/sub/nested/build/output.o", 0, 0, 0 },
			{ "d1", "projects/submarine/build", 0, 0, 0 },
			{ "d1", "projects/submarine/build/output.o", 0, 0, 0 },
			{ 0 }
		}
	},
	{
		"double-star wildcard combinations",
		{
			{ -1, 0, "", "src/**/test/" },
			{ 1, 0, "", "src/**.js" },
			{ 0 }
		},
		{
			{ "d1", "src/app.js", 0, 0, -1 },
			{ "d1", "src/components/ui/button.js", 0, 0, -1 },
			{ "d1", "src/test", -1, -1, -1 },
			{ "d1", "src/components/test", -1, -1, -1 },
			{ "d1", "src/test/app.js", -1, 0, -1 },
			{ "d1", "src/components/test/app.js", -1, 0, -1 },
			{ "d1", "src/app.css", -1, 0, -1 },
			{ 0 }
		}
	},
	{
		"absolute leading double-star include",
		{
			{ 1, 0, "", "/**.jpg" },
			{ 0 }
		},
		{
			{ "d1", "photo.jpg", 0, 0, -1 },
			{ "d1", "dir/photo.jpg", 0, 0, -1 },
			{ "d1", "a/b/photo.jpg", 0, 0, -1 },
			{ "d1", "photo.png", -1, 0, -1 },
			{ "d1", "dir/photo.png", -1, 0, -1 },
			{ 0 }
		}
	},
	{
		"absolute leading double-star exclude",
		{
			{ -1, 0, "", "/**.tmp" },
			{ 0 }
		},
		{
			{ "d1", "file.tmp", -1, 0, 0 },
			{ "d1", "a/file.tmp", -1, 0, 0 },
			{ "d1", "a/b/file.tmp", -1, 0, 0 },
			{ "d1", "file.txt", 0, 0, 0 },
			{ "d1", "a/b/file.txt", 0, 0, 0 },
			{ 0 }
		}
	},
	{
		"local absolute leading double-star exclude",
		{
			{ -1, 0, "projects/sub/", "/**.tmp" },
			{ 0 }
		},
		{
			{ "d1", "projects/sub/file.tmp", -1, 0, 0 },
			{ "d1", "projects/sub/a/file.tmp", -1, 0, 0 },
			{ "d1", "projects/sub/a/b/file.tmp", -1, 0, 0 },
			{ "d1", "projects/file.tmp", 0, 0, 0 },
			{ "d1", "projects/submarine/file.tmp", 0, 0, 0 },
			{ 0 }
		}
	},
	{
		"absolute leading double-star suffix",
		{
			{ 1, 0, "", "/**foo" },
			{ 0 }
		},
		{
			{ "d1", "foo", 0, 0, -1 },
			{ "d1", "dir/foo", 0, 0, -1 },
			{ "d1", "a/b/foo", 0, 0, -1 },
			{ "d1", "bar", -1, 0, -1 },
			{ 0 }
		}
	},
	{
		"absolute trailing double-star",
		{
			{ 1, 0, "", "/**" },
			{ 0 }
		},
		{
			{ "d1", "file", 0, 0, -1 },
			{ "d1", "dir/file", 0, 0, -1 },
			{ "d1", "a/b/file", 0, 0, -1 },
			{ 0 }
		}
	},
	{
		"absolute leading double-star directory",
		{
			{ -1, 0, "", "/**foo/" },
			{ 0 }
		},
		{
			{ "d1", "foo", 0, -1, -1 },
			{ "d1", "a/b/foo", 0, -1, -1 },
			{ "d1", "a/b/foo/file", -1, 0, 0 },
			{ "d1", "a/b/bar", 0, 0, 0 },
			{ 0 }
		}
	},
	{
		"established absolute double-star forms",
		{
			{ 1, 0, "", "/**/foo" },
			{ 1, 0, "", "/foo/**/bar" },
			{ 0 }
		},
		{
			{ "d1", "foo", 0, 0, -1 },
			{ "d1", "a/b/foo", 0, 0, -1 },
			{ "d1", "foo/bar", 0, 0, -1 },
			{ "d1", "foo/a/b/bar", 0, 0, -1 },
			{ "d1", "other/bar", -1, 0, -1 },
			{ 0 }
		}
	},
	{
		"filter with literal caret in pattern and root",
		{
			{ 1, 0, "", "/foo^bar" },
			{ -1, 0, "proj^ect/sub/", "*.tmp" },
			{ 0 }
		},
		{
			{ "d1", "foo^bar", 0, 0, 0 },
			{ "d1", "foo_bar", 0, 0, 0 },
			{ "d1", "proj^ect/sub/file.tmp", -1, 0, 0 },
			{ "d1", "proj^ect/sub/file.txt", 0, 0, 0 },
			{ 0 }
		}
	}
};

static void test_filter_scenarios(void)
{
	for (unsigned s = 0; s < sizeof(FILTER_SCENARIOS) / sizeof(FILTER_SCENARIOS[0]); ++s) {
		const struct filter_scenario* sc = &FILTER_SCENARIOS[s];
		tommy_list filterlist;
		tommy_list_init(&filterlist);

		for (int r = 0; sc->rules[r].direction != 0; ++r) {
			struct snapraid_filter* f;
			if (sc->rules[r].is_disk)
				f = filter_alloc_disk(sc->rules[r].direction, sc->rules[r].pattern);
			else
				f = filter_alloc_file(sc->rules[r].direction, sc->rules[r].root, sc->rules[r].pattern);

			if (!f) {
				/* LCOV_EXCL_START */
				log_fatal(EINTERNAL, "Failed to allocate filter in scenario '%s'\n", sc->desc);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
			tommy_list_insert_tail(&filterlist, &f->node, f);
		}

		for (int c = 0; sc->cases[c].disk != 0; ++c) {
			const struct filter_test_case* tc = &sc->cases[c];
			struct snapraid_filter* reason = 0;

			int res_path = filter_path(&filterlist, &reason, tc->disk, tc->path);
			if (res_path != tc->exp_path) {
				/* LCOV_EXCL_START */
				log_fatal(EINTERNAL, "Scenario '%s': filter_path('%s', '%s') = %d, expected %d\n",
					sc->desc, tc->disk, tc->path, res_path, tc->exp_path);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			int res_subdir = filter_subdir(&filterlist, &reason, tc->disk, tc->path);
			if (res_subdir != tc->exp_subdir) {
				/* LCOV_EXCL_START */
				log_fatal(EINTERNAL, "Scenario '%s': filter_subdir('%s', '%s') = %d, expected %d\n",
					sc->desc, tc->disk, tc->path, res_subdir, tc->exp_subdir);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}

			int res_emptydir = filter_emptydir(&filterlist, &reason, tc->disk, tc->path);
			if (res_emptydir != tc->exp_emptydir) {
				/* LCOV_EXCL_START */
				log_fatal(EINTERNAL, "Scenario '%s': filter_emptydir('%s', '%s') = %d, expected %d\n",
					sc->desc, tc->disk, tc->path, res_emptydir, tc->exp_emptydir);
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
		}

		tommy_node* node = tommy_list_head(&filterlist);
		while (node) {
			struct snapraid_filter* f = node->data;
			node = node->next;
			filter_free(f);
		}
	}
}

static void test_filter(void)
{
	test_filter_validity();
	test_filter_scenarios();
}

static void test_parse_smartctl(void)
{
	char smartctl[SMART_MAX];
	char smartctl_info[SMART_MAX];

	/* empty default case */
	if (parse_smartctl("smartctl d1 %s", smartctl, sizeof(smartctl), smartctl_info, sizeof(smartctl_info)) != 0) {
		/* LCOV_EXCL_START */
		log_fatal(EINTERNAL, "Failed parse_smartctl empty test 1\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}
	if (strcmp(smartctl, "smartctl d1 %s") != 0 || smartctl_info[0] != 0) {
		/* LCOV_EXCL_START */
		log_fatal(EINTERNAL, "Failed parse_smartctl empty test 2\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	if (parse_smartctl("smartctl d1 /dev/sda", smartctl, sizeof(smartctl), smartctl_info, sizeof(smartctl_info)) != 0) {
		/* LCOV_EXCL_START */
		log_fatal(EINTERNAL, "Failed parse_smartctl empty test 3\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}
	if (strcmp(smartctl, "smartctl d1 /dev/sda") != 0 || smartctl_info[0] != 0) {
		/* LCOV_EXCL_START */
		log_fatal(EINTERNAL, "Failed parse_smartctl empty test 4\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* basic default case */
	if (parse_smartctl("smartctl d1 -d sat %s", smartctl, sizeof(smartctl), smartctl_info, sizeof(smartctl_info)) != 0) {
		/* LCOV_EXCL_START */
		log_fatal(EINTERNAL, "Failed parse_smartctl basic test 1\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}
	if (strcmp(smartctl, "smartctl d1 -d sat %s") != 0 || smartctl_info[0] != 0) {
		/* LCOV_EXCL_START */
		log_fatal(EINTERNAL, "Failed parse_smartctl basic test 2\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* case with [info: xxx] tag */
	if (parse_smartctl("smartctl d1 [info: -H -i -A] -d sat %s", smartctl, sizeof(smartctl), smartctl_info, sizeof(smartctl_info)) != 0) {
		/* LCOV_EXCL_START */
		log_fatal(EINTERNAL, "Failed parse_smartctl info test 1\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}
	if (strcmp(smartctl, "smartctl d1  -d sat %s") != 0 || strcmp(smartctl_info, "-H -i -A") != 0) {
		/* LCOV_EXCL_START */
		log_fatal(EINTERNAL, "Failed parse_smartctl info test 2\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* case with spaces in tag */
	if (parse_smartctl("smartctl d2 [info:  -H -I ] -d sat", smartctl, sizeof(smartctl), smartctl_info, sizeof(smartctl_info)) != 0) {
		/* LCOV_EXCL_START */
		log_fatal(EINTERNAL, "Failed parse_smartctl space test 1\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}
	if (strcmp(smartctl, "smartctl d2  -d sat") != 0 || strcmp(smartctl_info, "-H -I") != 0) {
		/* LCOV_EXCL_START */
		log_fatal(EINTERNAL, "Failed parse_smartctl space test 2\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* invalid case: multiple info tags */
	if (parse_smartctl("smartctl [info: -H] [info: -i] %s", smartctl, sizeof(smartctl), smartctl_info, sizeof(smartctl_info)) == 0) {
		/* LCOV_EXCL_START */
		log_fatal(EINTERNAL, "Failed parse_smartctl multiple tags test\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* invalid case: missing closing bracket */
	if (parse_smartctl("smartctl [info: -H %s", smartctl, sizeof(smartctl), smartctl_info, sizeof(smartctl_info)) == 0) {
		/* LCOV_EXCL_START */
		log_fatal(EINTERNAL, "Failed parse_smartctl missing bracket test\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}
}

static void test_smart_ignore(void)
{
	struct snapraid_state state;
	devinfo_t devinfo;
	int i;

	memset(&state, 0, sizeof(state));
	memset(&devinfo, 0, sizeof(devinfo));

	/* Initialize devinfo attributes with mock data */
	for (i = 0; i < SMART_COUNT; ++i) {
		devinfo.smart[i].raw = 100;
		devinfo.smart[i].norm = 100;
		snprintf(devinfo.smart[i].name, sizeof(devinfo.smart[i].name), "Attr_%d", i);
	}
	/* Special name for test */
	pathcpy(devinfo.smart[5].name, sizeof(devinfo.smart[5].name), "Reallocated_Sector_Ct");
	pathcpy(devinfo.smart[197].name, sizeof(devinfo.smart[197].name), "Current_Pending_Sector");

	/* 1. Test numerical global ignore */
	state.smartignore[0].attr_index = 5;
	state.smartignore[0].attr_name[0] = 0;

	/* 2. Test name global ignore (case-insensitive) */
	state.smartignore[1].attr_index = 0;
	pathcpy(state.smartignore[1].attr_name, sizeof(state.smartignore[1].attr_name), "current_pending_sector");

	/* 3. Test numerical devinfo ignore */
	devinfo.smartignore[0].attr_index = 10;
	devinfo.smartignore[0].attr_name[0] = 0;

	/* 4. Test name devinfo ignore (case-insensitive) */
	devinfo.smartignore[1].attr_index = 0;
	pathcpy(devinfo.smartignore[1].attr_name, sizeof(devinfo.smartignore[1].attr_name), "aTtR_20");

	state_smart_ignore(&state, &devinfo);

	/* Check that ignored attributes are set to SMART_UNASSIGNED */
	if (devinfo.smart[5].raw != SMART_UNASSIGNED || devinfo.smart[5].norm != SMART_UNASSIGNED) {
		log_fatal(EINTERNAL, "test_smart_ignore: numerical global ignore failed\n");
		exit(EXIT_FAILURE);
	}
	if (devinfo.smart[197].raw != SMART_UNASSIGNED || devinfo.smart[197].norm != SMART_UNASSIGNED) {
		log_fatal(EINTERNAL, "test_smart_ignore: name global ignore failed\n");
		exit(EXIT_FAILURE);
	}
	if (devinfo.smart[10].raw != SMART_UNASSIGNED || devinfo.smart[10].norm != SMART_UNASSIGNED) {
		log_fatal(EINTERNAL, "test_smart_ignore: numerical devinfo ignore failed\n");
		exit(EXIT_FAILURE);
	}
	if (devinfo.smart[20].raw != SMART_UNASSIGNED || devinfo.smart[20].norm != SMART_UNASSIGNED) {
		log_fatal(EINTERNAL, "test_smart_ignore: name devinfo ignore failed\n");
		exit(EXIT_FAILURE);
	}

	/* Check that non-ignored attributes are still present */
	if (devinfo.smart[6].raw != 100) {
		log_fatal(EINTERNAL, "test_smart_ignore: unmodified attribute was cleared\n");
		exit(EXIT_FAILURE);
	}
}

static void test_stream(void)
{
	STREAM f;
	uint32_t v32;
	uint64_t v64;

	/* 32-bit valid boundary */
	unsigned char buf_v32[] = { 0x7f, 0x7f, 0x7f, 0x7f, 0x8f };
	memset(&f, 0, sizeof(f));
	f.pos = buf_v32;
	f.end = buf_v32 + sizeof(buf_v32);
	if (sgetb32(&f, &v32) != 0 || v32 != 0xffffffffU) {
		/* LCOV_EXCL_START */
		log_fatal(EINTERNAL, "test_stream: valid uint32 max failed\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* 32-bit overflow payload in 5th terminal byte */
	unsigned char buf_o32[] = { 0x00, 0x00, 0x00, 0x00, 0xf1 };
	memset(&f, 0, sizeof(f));
	f.pos = buf_o32;
	f.end = buf_o32 + sizeof(buf_o32);
	if (sgetb32(&f, &v32) == 0) {
		/* LCOV_EXCL_START */
		log_fatal(EINTERNAL, "test_stream: uint32 overflow accepted\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* 32-bit 5th byte non-terminal */
	unsigned char buf_nt32[] = { 0x00, 0x00, 0x00, 0x00, 0x01, 0x80 };
	memset(&f, 0, sizeof(f));
	f.pos = buf_nt32;
	f.end = buf_nt32 + sizeof(buf_nt32);
	if (sgetb32(&f, &v32) == 0) {
		/* LCOV_EXCL_START */
		log_fatal(EINTERNAL, "test_stream: uint32 5th non-terminal accepted\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* 64-bit valid boundary */
	unsigned char buf_v64[] = { 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x81 };
	memset(&f, 0, sizeof(f));
	f.pos = buf_v64;
	f.end = buf_v64 + sizeof(buf_v64);
	if (sgetb64(&f, &v64) != 0 || v64 != 0xffffffffffffffffULL) {
		/* LCOV_EXCL_START */
		log_fatal(EINTERNAL, "test_stream: valid uint64 max failed\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* 64-bit overflow payload in 10th terminal byte */
	unsigned char buf_o64[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x83 };
	memset(&f, 0, sizeof(f));
	f.pos = buf_o64;
	f.end = buf_o64 + sizeof(buf_o64);
	if (sgetb64(&f, &v64) == 0) {
		/* LCOV_EXCL_START */
		log_fatal(EINTERNAL, "test_stream: uint64 overflow accepted\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* 64-bit 10th byte non-terminal */
	unsigned char buf_nt64[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x80 };
	memset(&f, 0, sizeof(f));
	f.pos = buf_nt64;
	f.end = buf_nt64 + sizeof(buf_nt64);
	if (sgetb64(&f, &v64) == 0) {
		/* LCOV_EXCL_START */
		log_fatal(EINTERNAL, "test_stream: uint64 10th non-terminal accepted\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}
}

static void test_raid(void)
{
	/* vandermonde raid parity generation with 32 data disks */
	if (raid_test_par(RAID_MODE_VANDERMONDE_RAID, 32, 256) != 0) {
		/* LCOV_EXCL_START */
		log_fatal(EINTERNAL, "Failed GEN Vandermonde RAID test\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* vandermonde raid parity generation with a single data disk */
	if (raid_test_par(RAID_MODE_VANDERMONDE_RAID, 1, 256) != 0) {
		/* LCOV_EXCL_START */
		log_fatal(EINTERNAL, "Failed GEN Vandermonde RAID test single data disk\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* vandermonde raid parity generation with maximum data disks */
	if (raid_test_par(RAID_MODE_VANDERMONDE_RAID, RAID_DATA_MAX, 256) != 0) {
		/* LCOV_EXCL_START */
		log_fatal(EINTERNAL, "Failed GEN Vandermonde RAID test max data disks\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* vandermonde raid recovery with combinations of missing data disks and parities */
	if (raid_test_rec(RAID_MODE_VANDERMONDE_RAID, 12, 256) != 0) {
		/* LCOV_EXCL_START */
		log_fatal(EINTERNAL, "Failed REC Vandermonde RAID test\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* vandermonde raid tail recovery across all data disk counts up to maximum */
	if (raid_test_tail(RAID_MODE_VANDERMONDE_RAID, 64, 256) != 0) {
		/* LCOV_EXCL_START */
		log_fatal(EINTERNAL, "Failed TAIL Vandermonde RAID test\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* cauchy raid matrix invertibility and polynomial checks */
	if (raid_test_poly(RAID_MODE_CAUCHY_RAID) != 0) {
		/* LCOV_EXCL_START */
		log_fatal(EINTERNAL, "Failed POLY Cauchy RAID test\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* cauchy raid parity generation with 32 data disks */
	if (raid_test_par(RAID_MODE_CAUCHY_RAID, 32, 256) != 0) {
		/* LCOV_EXCL_START */
		log_fatal(EINTERNAL, "Failed GEN Cauchy RAID test\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* cauchy raid parity generation with a single data disk */
	if (raid_test_par(RAID_MODE_CAUCHY_RAID, 1, 256) != 0) {
		/* LCOV_EXCL_START */
		log_fatal(EINTERNAL, "Failed GEN Cauchy RAID test single data disk\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* cauchy raid parity generation with maximum data disks */
	if (raid_test_par(RAID_MODE_CAUCHY_RAID, RAID_DATA_MAX, 256) != 0) {
		/* LCOV_EXCL_START */
		log_fatal(EINTERNAL, "Failed GEN Cauchy RAID test max data disks\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* cauchy raid recovery with combinations of missing data disks and parities */
	if (raid_test_rec(RAID_MODE_CAUCHY_RAID, 12, 256) != 0) {
		/* LCOV_EXCL_START */
		log_fatal(EINTERNAL, "Failed REC Cauchy RAID test\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* cauchy raid tail recovery across all data disk counts up to maximum */
	if (raid_test_tail(RAID_MODE_CAUCHY_RAID, 64, 256) != 0) {
		/* LCOV_EXCL_START */
		log_fatal(EINTERNAL, "Failed TAIL Cauchy RAID test\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* cauchy aes matrix invertibility and polynomial checks */
	if (raid_test_poly(RAID_MODE_CAUCHY_AES) != 0) {
		/* LCOV_EXCL_START */
		log_fatal(EINTERNAL, "Failed POLY Cauchy AES test\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* cauchy aes parity generation with 32 data disks */
	if (raid_test_par(RAID_MODE_CAUCHY_AES, 32, 256) != 0) {
		/* LCOV_EXCL_START */
		log_fatal(EINTERNAL, "Failed GEN Cauchy AES test\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* cauchy aes parity generation with a single data disk */
	if (raid_test_par(RAID_MODE_CAUCHY_AES, 1, 256) != 0) {
		/* LCOV_EXCL_START */
		log_fatal(EINTERNAL, "Failed GEN Cauchy AES test single data disk\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* cauchy aes parity generation with maximum data disks */
	if (raid_test_par(RAID_MODE_CAUCHY_AES, RAID_DATA_MAX, 256) != 0) {
		/* LCOV_EXCL_START */
		log_fatal(EINTERNAL, "Failed GEN Cauchy AES test max data disks\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* cauchy aes recovery with combinations of missing data disks and parities */
	if (raid_test_rec(RAID_MODE_CAUCHY_AES, 12, 256) != 0) {
		/* LCOV_EXCL_START */
		log_fatal(EINTERNAL, "Failed REC Cauchy AES test\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* cauchy aes tail recovery across all data disk counts up to maximum */
	if (raid_test_tail(RAID_MODE_CAUCHY_AES, 64, 256) != 0) {
		/* LCOV_EXCL_START */
		log_fatal(EINTERNAL, "Failed TAIL Cauchy AES test\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* verify raid_mode get/set behavior */
	int cur = raid_mode(RAID_MODE_GET);
	int prev = raid_mode(RAID_MODE_CAUCHY_AES);
	if (prev != cur) {
		/* LCOV_EXCL_START */
		log_fatal(EINTERNAL, "raid_mode set did not return previous mode\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}
	if (raid_mode(RAID_MODE_GET) != RAID_MODE_CAUCHY_AES) {
		/* LCOV_EXCL_START */
		log_fatal(EINTERNAL, "raid_mode GET did not return active mode\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* restore default mode */
	raid_mode(RAID_MODE_CAUCHY_RAID);
}

static void test_misc(int argc, char* argv[])
{
	int i;
	char buffer[ESC_MAX];

	assert(strcmp(strpolish(strcpy(buffer, "\r \n\xFF")), "    ") == 0);
	assert(strcmp(strtrim(strcpy(buffer, " trim trim \n\r")), "trim trim") == 0);
	assert(strcmp(strlwr(strcpy(buffer, " LoWer\n\r")), " lower\n\r") == 0);

	assert(worddigitstr("longneedlestring", "needle") == 0);
	assert(worddigitstr("longneedlestring", "") == 0);
	assert(worddigitstr("long needle string", "needle") != 0);
	assert(worddigitstr("long1needle2string", "needle") != 0);
	assert(worddigitstr("long\rneedle3string", "needle") != 0);
	assert(worddigitstr("long1needle", "needle") != 0);
	assert(worddigitstr("needle2string", "needle") != 0);
	assert(worddigitstr("needle", "needle") != 0);

	assert(strcmp(esc_tag("simple"), "simple") == 0);
	assert(strcmp(esc_tag("line1\nline2"), "line1\\nline2") == 0);
	assert(strcmp(esc_tag("line1\rline2"), "line1\\rline2") == 0);
	assert(strcmp(esc_tag("key:value"), "key\\dvalue") == 0);
	assert(strcmp(esc_tag("C:\\path\\file"), "C\\d\\\\path\\\\file") == 0);
	assert(strcmp(esc_tag("A\nB\rC:D\\E"), "A\\nB\\rC\\dD\\\\E") == 0);
	assert(strcmp(esc_tag("endwith\\"), "endwith\\\\") == 0);
	assert(strcmp(esc_tag(""), "") == 0);
	assert(strcmp(esc_tag("\n\r:\\\\"), "\\n\\r\\d\\\\\\\\") == 0);

	for (i = 2; i < argc; ++i) {
		printf("argv[%d]\n", i);
		printf("\t#%s#\n", argv[i]);
		printf("\t#%s#\n", esc_shell(argv[i], buffer));
	}

#ifdef _WIN32
	/* basic cases - no special characters, no quotes needed */
	assert(strcmp(esc_shell("simple", buffer), "simple") == 0);
	assert(strcmp(esc_shell("file.txt", buffer), "file.txt") == 0);
	assert(strcmp(esc_shell("file123", buffer), "file123") == 0);
	assert(strcmp(esc_shell("file_name-test.doc", buffer), "file_name-test.doc") == 0);
	assert(strcmp(esc_shell(",._+:@/-", buffer), ",._+:@/-") == 0);
	assert(strcmp(esc_shell("C:\\Users\\test", buffer), "C:\\Users\\test") == 0);

	/* space - requires quoting */
	assert(strcmp(esc_shell(" ", buffer), "\" \"") == 0);
	assert(strcmp(esc_shell("file name.txt", buffer), "\"file name.txt\"") == 0);
	assert(strcmp(esc_shell("my document.doc", buffer), "\"my document.doc\"") == 0);
	assert(strcmp(esc_shell("  multiple  spaces  ", buffer), "\"  multiple  spaces  \"") == 0);

	/* tab - requires quoting */
	assert(strcmp(esc_shell("\t", buffer), "\"\t\"") == 0);
	assert(strcmp(esc_shell("file\tname", buffer), "\"file\tname\"") == 0);

	/* newline - requires quoting */
	assert(strcmp(esc_shell("\n", buffer), "\"\n\"") == 0);
	assert(strcmp(esc_shell("line1\nline2", buffer), "\"line1\nline2\"") == 0);

	/* carriage return - requires quoting */
	assert(strcmp(esc_shell("\r", buffer), "\"\r\"") == 0);
	assert(strcmp(esc_shell("text\r\n", buffer), "\"text\r\n\"") == 0);

	/* double quote - requires quoting and escaping with backslash */
	assert(strcmp(esc_shell("\"", buffer), "\"\\\"\"") == 0);
	assert(strcmp(esc_shell("file\"name", buffer), "\"file\\\"name\"") == 0);
	assert(strcmp(esc_shell("\"quoted\"", buffer), "\"\\\"quoted\\\"\"") == 0);
	assert(strcmp(esc_shell("say \"hello\"", buffer), "\"say \\\"hello\\\"\"") == 0);

	/* ampersand - requires quoting */
	assert(strcmp(esc_shell("&", buffer), "\"&\"") == 0);
	assert(strcmp(esc_shell("file&name", buffer), "\"file&name\"") == 0);
	assert(strcmp(esc_shell("a&b&c", buffer), "\"a&b&c\"") == 0);
	assert(strcmp(esc_shell("file & name", buffer), "\"file & name\"") == 0);

	/* pipe - requires quoting */
	assert(strcmp(esc_shell("|", buffer), "\"|\"") == 0);
	assert(strcmp(esc_shell("file|name", buffer), "\"file|name\"") == 0);
	assert(strcmp(esc_shell("a | b", buffer), "\"a | b\"") == 0);

	/* parentheses - requires quoting */
	assert(strcmp(esc_shell("(", buffer), "\"(\"") == 0);
	assert(strcmp(esc_shell(")", buffer), "\")\"") == 0);
	assert(strcmp(esc_shell("(test)", buffer), "\"(test)\"") == 0);
	assert(strcmp(esc_shell("file (1)", buffer), "\"file (1)\"") == 0);
	assert(strcmp(esc_shell("file(copy)", buffer), "\"file(copy)\"") == 0);

	/* angle brackets - requires quoting */
	assert(strcmp(esc_shell("<", buffer), "\"<\"") == 0);
	assert(strcmp(esc_shell(">", buffer), "\">\"") == 0);
	assert(strcmp(esc_shell("a<b>c", buffer), "\"a<b>c\"") == 0);
	assert(strcmp(esc_shell("file > output", buffer), "\"file > output\"") == 0);

	/* caret - requires quoting */
	assert(strcmp(esc_shell("^", buffer), "\"^\"") == 0);
	assert(strcmp(esc_shell("test^test", buffer), "\"test^test\"") == 0);
	assert(strcmp(esc_shell("a ^ b", buffer), "\"a ^ b\"") == 0);

	/* multiple special chars - requires quoting */
	assert(strcmp(esc_shell("&|()<>^", buffer), "\"&|()<>^\"") == 0);
	assert(strcmp(esc_shell("test&|test", buffer), "\"test&|test\"") == 0);
	assert(strcmp(esc_shell("a & b | c", buffer), "\"a & b | c\"") == 0);

	/* percent sign - requires quoting */
	assert(strcmp(esc_shell("%", buffer), "\"%\"") == 0);
	assert(strcmp(esc_shell("%%", buffer), "\"%%\"") == 0);
	assert(strcmp(esc_shell("%PATH%", buffer), "\"%PATH%\"") == 0);
	assert(strcmp(esc_shell("test%var%test", buffer), "\"test%var%test\"") == 0);
	assert(strcmp(esc_shell("%PATH% file", buffer), "\"%PATH% file\"") == 0);

	/* exclamation mark - requires quoting */
	assert(strcmp(esc_shell("!", buffer), "\"!\"") == 0);
	assert(strcmp(esc_shell("!VAR!", buffer), "\"!VAR!\"") == 0);
	assert(strcmp(esc_shell("test!test", buffer), "\"test!test\"") == 0);
	assert(strcmp(esc_shell("hello !world!", buffer), "\"hello !world!\"") == 0);

	/* equals sign - requires quoting */
	assert(strcmp(esc_shell("=", buffer), "\"=\"") == 0);
	assert(strcmp(esc_shell("VAR=value", buffer), "\"VAR=value\"") == 0);
	assert(strcmp(esc_shell("a=b", buffer), "\"a=b\"") == 0);

	/* semicolon - requires quoting */
	assert(strcmp(esc_shell(";", buffer), "\";\"") == 0);
	assert(strcmp(esc_shell("cmd1;cmd2", buffer), "\"cmd1;cmd2\"") == 0);

	/* backslash - no quotes needed when alone or in path */
	assert(strcmp(esc_shell("\\", buffer), "\\") == 0);
	assert(strcmp(esc_shell("C:\\", buffer), "C:\\") == 0);
	assert(strcmp(esc_shell("C:\\Users", buffer), "C:\\Users") == 0);
	assert(strcmp(esc_shell("path\\to\\file", buffer), "path\\to\\file") == 0);
	assert(strcmp(esc_shell("C:\\folder\\", buffer), "C:\\folder\\") == 0);

	/* backslash with space - requires quoting, normal backslash inside */
	assert(strcmp(esc_shell("\\ ", buffer), "\"\\ \"") == 0);
	assert(strcmp(esc_shell("C:\\ ", buffer), "\"C:\\ \"") == 0);
	assert(strcmp(esc_shell("C:\\Program Files", buffer), "\"C:\\Program Files\"") == 0);

	/* trailing backslash with quotes - backslashes before closing quote must be doubled */
	assert(strcmp(esc_shell("C:\\folder\\ ", buffer), "\"C:\\folder\\ \"") == 0);
	assert(strcmp(esc_shell("path\\ ", buffer), "\"path\\ \"") == 0);
	assert(strcmp(esc_shell("C:\\My Documents\\", buffer), "\"C:\\My Documents\\\\\"") == 0);

	/* backslash before embedded quote - backslash before quote must be doubled */
	assert(strcmp(esc_shell("C:\\\"test\"", buffer), "\"C:\\\\\\\"test\\\"\"") == 0);
	assert(strcmp(esc_shell("path\\\"file\"", buffer), "\"path\\\\\\\"file\\\"\"") == 0);

	/* multiple trailing backslashes before end with quotes */
	assert(strcmp(esc_shell("test\\\\ ", buffer), "\"test\\\\ \"") == 0);
	assert(strcmp(esc_shell("path\\\\\\\\ ", buffer), "\"path\\\\\\\\ \"") == 0);

	/* backslash NOT before quote - normal backslash */
	assert(strcmp(esc_shell("test\\file ", buffer), "\"test\\file \"") == 0);
	assert(strcmp(esc_shell("a\\b c", buffer), "\"a\\b c\"") == 0);

	/* control characters - require quoting */
	assert(strcmp(esc_shell("\x01", buffer), "\"\x01\"") == 0);
	assert(strcmp(esc_shell("\x1F", buffer), "\"\x1F\"") == 0);
	assert(strcmp(esc_shell("\x7F", buffer), "\"\x7F\"") == 0); /* DEL character */
	assert(strcmp(esc_shell("test\x01test", buffer), "\"test\x01test\"") == 0);

	/* complex real-world examples */
	assert(strcmp(esc_shell("C:\\Program Files\\App", buffer), "\"C:\\Program Files\\App\"") == 0);
	assert(strcmp(esc_shell("C:\\Program Files (x86)\\", buffer), "\"C:\\Program Files (x86)\\\\\"") == 0);
	assert(strcmp(esc_shell("file (copy).txt", buffer), "\"file (copy).txt\"") == 0);
	assert(strcmp(esc_shell("setup-v1.0.exe", buffer), "setup-v1.0.exe") == 0);
	assert(strcmp(esc_shell("setup v1.0.exe", buffer), "\"setup v1.0.exe\"") == 0);

	/* mixed quotes and special chars */
	assert(strcmp(esc_shell("say \"hi\" & exit", buffer), "\"say \\\"hi\\\" & exit\"") == 0);
	assert(strcmp(esc_shell("test \"a|b\"", buffer), "\"test \\\"a|b\\\"\"") == 0);

	/* empty string */
	assert(strcmp(esc_shell("", buffer), "") == 0);

	/* all safe characters that don't need escaping */
	assert(strcmp(esc_shell("abcdefghijklmnopqrstuvwxyz", buffer), "abcdefghijklmnopqrstuvwxyz") == 0);
	assert(strcmp(esc_shell("ABCDEFGHIJKLMNOPQRSTUVWXYZ", buffer), "ABCDEFGHIJKLMNOPQRSTUVWXYZ") == 0);
	assert(strcmp(esc_shell("0123456789", buffer), "0123456789") == 0);
	assert(strcmp(esc_shell("._-+,@:", buffer), "._-+,@:") == 0);
#else
	/* basic cases - no special characters */
	assert(strcmp(esc_shell("simple", buffer), "simple") == 0);
	assert(strcmp(esc_shell("file.txt", buffer), "file.txt") == 0);
	assert(strcmp(esc_shell("file123", buffer), "file123") == 0);
	assert(strcmp(esc_shell("file_name-test.doc", buffer), "file_name-test.doc") == 0);
	assert(strcmp(esc_shell(",._+:@/-", buffer), ",._+:@/-") == 0);
	assert(strcmp(esc_shell("/usr/local/bin", buffer), "/usr/local/bin") == 0);

	/* empty string */
	assert(strcmp(esc_shell("", buffer), "") == 0);

	/* space - escape with backslash */
	assert(strcmp(esc_shell(" ", buffer), "\\ ") == 0);
	assert(strcmp(esc_shell("file name.txt", buffer), "file\\ name.txt") == 0);
	assert(strcmp(esc_shell("my document.doc", buffer), "my\\ document.doc") == 0);
	assert(strcmp(esc_shell("  spaces  ", buffer), "\\ \\ spaces\\ \\ ") == 0);
	assert(strcmp(esc_shell("a b c", buffer), "a\\ b\\ c") == 0);

	/* tab - escape with backslash */
	assert(strcmp(esc_shell("\t", buffer), "\\\t") == 0);
	assert(strcmp(esc_shell("file\tname", buffer), "file\\\tname") == 0);
	assert(strcmp(esc_shell("\t\t", buffer), "\\\t\\\t") == 0);

	/* newline - escape with backslash */
	assert(strcmp(esc_shell("\n", buffer), "\\\n") == 0);
	assert(strcmp(esc_shell("line1\nline2", buffer), "line1\\\nline2") == 0);
	assert(strcmp(esc_shell("\n\n", buffer), "\\\n\\\n") == 0);

	/* carriage return - escape with backslash */
	assert(strcmp(esc_shell("\r", buffer), "\\\r") == 0);
	assert(strcmp(esc_shell("text\r\n", buffer), "text\\\r\\\n") == 0);

	/* tilde (home directory expansion) */
	assert(strcmp(esc_shell("~", buffer), "\\~") == 0);
	assert(strcmp(esc_shell("~/file", buffer), "\\~/file") == 0);
	assert(strcmp(esc_shell("file~name", buffer), "file\\~name") == 0);
	assert(strcmp(esc_shell("~user", buffer), "\\~user") == 0);

	/* backtick (command substitution) */
	assert(strcmp(esc_shell("`", buffer), "\\`") == 0);
	assert(strcmp(esc_shell("`command`", buffer), "\\`command\\`") == 0);
	assert(strcmp(esc_shell("test`test", buffer), "test\\`test") == 0);
	assert(strcmp(esc_shell("``", buffer), "\\`\\`") == 0);

	/* hash (comment) */
	assert(strcmp(esc_shell("#", buffer), "\\#") == 0);
	assert(strcmp(esc_shell("#comment", buffer), "\\#comment") == 0);
	assert(strcmp(esc_shell("file#name", buffer), "file\\#name") == 0);
	assert(strcmp(esc_shell("test#123", buffer), "test\\#123") == 0);

	/* dollar sign (variable expansion) */
	assert(strcmp(esc_shell("$", buffer), "\\$") == 0);
	assert(strcmp(esc_shell("$$", buffer), "\\$\\$") == 0);
	assert(strcmp(esc_shell("$VAR", buffer), "\\$VAR") == 0);
	assert(strcmp(esc_shell("${VAR}", buffer), "\\$\\{VAR\\}") == 0);
	assert(strcmp(esc_shell("test$test", buffer), "test\\$test") == 0);
	assert(strcmp(esc_shell("$1", buffer), "\\$1") == 0);
	assert(strcmp(esc_shell("$PATH", buffer), "\\$PATH") == 0);

	/* ampersand (background job) */
	assert(strcmp(esc_shell("&", buffer), "\\&") == 0);
	assert(strcmp(esc_shell("&&", buffer), "\\&\\&") == 0);
	assert(strcmp(esc_shell("file&name", buffer), "file\\&name") == 0);
	assert(strcmp(esc_shell("a&b&c", buffer), "a\\&b\\&c") == 0);
	assert(strcmp(esc_shell("cmd1 & cmd2", buffer), "cmd1\\ \\&\\ cmd2") == 0);

	/* asterisk (wildcard) */
	assert(strcmp(esc_shell("*", buffer), "\\*") == 0);
	assert(strcmp(esc_shell("**", buffer), "\\*\\*") == 0);
	assert(strcmp(esc_shell("*.txt", buffer), "\\*.txt") == 0);
	assert(strcmp(esc_shell("file*name", buffer), "file\\*name") == 0);
	assert(strcmp(esc_shell("test*", buffer), "test\\*") == 0);

	/* parentheses (subshell) */
	assert(strcmp(esc_shell("(", buffer), "\\(") == 0);
	assert(strcmp(esc_shell(")", buffer), "\\)") == 0);
	assert(strcmp(esc_shell("()", buffer), "\\(\\)") == 0);
	assert(strcmp(esc_shell("(test)", buffer), "\\(test\\)") == 0);
	assert(strcmp(esc_shell("file(1)", buffer), "file\\(1\\)") == 0);
	assert(strcmp(esc_shell("(a)(b)", buffer), "\\(a\\)\\(b\\)") == 0);

	/* backslash (escape character) */
	assert(strcmp(esc_shell("\\", buffer), "\\\\") == 0);
	assert(strcmp(esc_shell("\\\\", buffer), "\\\\\\\\") == 0);
	assert(strcmp(esc_shell("path\\to\\file", buffer), "path\\\\to\\\\file") == 0);
	assert(strcmp(esc_shell("test\\test", buffer), "test\\\\test") == 0);
	assert(strcmp(esc_shell("a\\b\\c", buffer), "a\\\\b\\\\c") == 0);

	/* pipe (pipeline) */
	assert(strcmp(esc_shell("|", buffer), "\\|") == 0);
	assert(strcmp(esc_shell("||", buffer), "\\|\\|") == 0);
	assert(strcmp(esc_shell("file|name", buffer), "file\\|name") == 0);
	assert(strcmp(esc_shell("a|b|c", buffer), "a\\|b\\|c") == 0);
	assert(strcmp(esc_shell("cmd1 | cmd2", buffer), "cmd1\\ \\|\\ cmd2") == 0);

	/* square brackets (wildcard) */
	assert(strcmp(esc_shell("[", buffer), "\\[") == 0);
	assert(strcmp(esc_shell("]", buffer), "\\]") == 0);
	assert(strcmp(esc_shell("[]", buffer), "\\[\\]") == 0);
	assert(strcmp(esc_shell("[abc]", buffer), "\\[abc\\]") == 0);
	assert(strcmp(esc_shell("file[1]", buffer), "file\\[1\\]") == 0);
	assert(strcmp(esc_shell("[0-9]", buffer), "\\[0-9\\]") == 0);

	/* curly braces (brace expansion) */
	assert(strcmp(esc_shell("{", buffer), "\\{") == 0);
	assert(strcmp(esc_shell("}", buffer), "\\}") == 0);
	assert(strcmp(esc_shell("{}", buffer), "\\{\\}") == 0);
	assert(strcmp(esc_shell("{a,b,c}", buffer), "\\{a,b,c\\}") == 0);
	assert(strcmp(esc_shell("file{1,2}", buffer), "file\\{1,2\\}") == 0);
	assert(strcmp(esc_shell("{1..10}", buffer), "\\{1..10\\}") == 0);

	/* semicolon (command separator) */
	assert(strcmp(esc_shell(";", buffer), "\\;") == 0);
	assert(strcmp(esc_shell(";;", buffer), "\\;\\;") == 0);
	assert(strcmp(esc_shell("cmd1;cmd2", buffer), "cmd1\\;cmd2") == 0);
	assert(strcmp(esc_shell("test;test", buffer), "test\\;test") == 0);
	assert(strcmp(esc_shell("a; b", buffer), "a\\;\\ b") == 0);

	/* single quote */
	assert(strcmp(esc_shell("'", buffer), "\\'") == 0);
	assert(strcmp(esc_shell("''", buffer), "\\'\\'") == 0);
	assert(strcmp(esc_shell("'test'", buffer), "\\'test\\'") == 0);
	assert(strcmp(esc_shell("file'name", buffer), "file\\'name") == 0);
	assert(strcmp(esc_shell("it's", buffer), "it\\'s") == 0);

	/* double quote */
	assert(strcmp(esc_shell("\"", buffer), "\\\"") == 0);
	assert(strcmp(esc_shell("\"\"", buffer), "\\\"\\\"") == 0);
	assert(strcmp(esc_shell("\"test\"", buffer), "\\\"test\\\"") == 0);
	assert(strcmp(esc_shell("file\"name", buffer), "file\\\"name") == 0);
	assert(strcmp(esc_shell("say \"hi\"", buffer), "say\\ \\\"hi\\\"") == 0);

	/* angle brackets (redirection) */
	assert(strcmp(esc_shell("<", buffer), "\\<") == 0);
	assert(strcmp(esc_shell(">", buffer), "\\>") == 0);
	assert(strcmp(esc_shell("<<", buffer), "\\<\\<") == 0);
	assert(strcmp(esc_shell(">>", buffer), "\\>\\>") == 0);
	assert(strcmp(esc_shell("a<b>c", buffer), "a\\<b\\>c") == 0);
	assert(strcmp(esc_shell("file>output", buffer), "file\\>output") == 0);
	assert(strcmp(esc_shell("cmd < in > out", buffer), "cmd\\ \\<\\ in\\ \\>\\ out") == 0);

	/* question mark (wildcard) */
	assert(strcmp(esc_shell("?", buffer), "\\?") == 0);
	assert(strcmp(esc_shell("??", buffer), "\\?\\?") == 0);
	assert(strcmp(esc_shell("file?.txt", buffer), "file\\?.txt") == 0);
	assert(strcmp(esc_shell("test?test", buffer), "test\\?test") == 0);
	assert(strcmp(esc_shell("file??", buffer), "file\\?\\?") == 0);

	/* equals sign (assignment in some contexts) */
	assert(strcmp(esc_shell("=", buffer), "\\=") == 0);
	assert(strcmp(esc_shell("==", buffer), "\\=\\=") == 0);
	assert(strcmp(esc_shell("VAR=value", buffer), "VAR\\=value") == 0);
	assert(strcmp(esc_shell("a=b", buffer), "a\\=b") == 0);
	assert(strcmp(esc_shell("PATH=/usr/bin", buffer), "PATH\\=/usr/bin") == 0);

	/* exclamation mark (history expansion) */
	assert(strcmp(esc_shell("!", buffer), "\\!") == 0);
	assert(strcmp(esc_shell("!!", buffer), "\\!\\!") == 0);
	assert(strcmp(esc_shell("test!test", buffer), "test\\!test") == 0);
	assert(strcmp(esc_shell("!$", buffer), "\\!\\$") == 0);
	assert(strcmp(esc_shell("!123", buffer), "\\!123") == 0);

	/* control characters (0x01-0x1F) - escape with backslash */
	assert(strcmp(esc_shell("\x01", buffer), "\\\x01") == 0);
	assert(strcmp(esc_shell("\x02", buffer), "\\\x02") == 0);
	assert(strcmp(esc_shell("\x1F", buffer), "\\\x1F") == 0);
	assert(strcmp(esc_shell("test\x01test", buffer), "test\\\x01test") == 0);

	/* DEL character (0x7F) */
	assert(strcmp(esc_shell("\x7F", buffer), "\\\x7F") == 0);
	assert(strcmp(esc_shell("test\x7Ftest", buffer), "test\\\x7Ftest") == 0);

	/* multiple special characters combined */
	assert(strcmp(esc_shell("$VAR & $OTHER", buffer), "\\$VAR\\ \\&\\ \\$OTHER") == 0);
	assert(strcmp(esc_shell("*.txt | grep test", buffer), "\\*.txt\\ \\|\\ grep\\ test") == 0);
	assert(strcmp(esc_shell("file (1) [copy].txt", buffer), "file\\ \\(1\\)\\ \\[copy\\].txt") == 0);
	assert(strcmp(esc_shell("a & b | c", buffer), "a\\ \\&\\ b\\ \\|\\ c") == 0);
	assert(strcmp(esc_shell("cmd1; cmd2 && cmd3", buffer), "cmd1\\;\\ cmd2\\ \\&\\&\\ cmd3") == 0);

	/* complex real-world examples */
	assert(strcmp(esc_shell("/home/user/My Documents", buffer), "/home/user/My\\ Documents") == 0);
	assert(strcmp(esc_shell("/path/to/file (copy).txt", buffer), "/path/to/file\\ \\(copy\\).txt") == 0);
	assert(strcmp(esc_shell("~/project/file-v1.0.tar.gz", buffer), "\\~/project/file-v1.0.tar.gz") == 0);
	assert(strcmp(esc_shell("$(whoami)@$(hostname)", buffer), "\\$\\(whoami\\)@\\$\\(hostname\\)") == 0);
	assert(strcmp(esc_shell("test && echo 'done'", buffer), "test\\ \\&\\&\\ echo\\ \\'done\\'") == 0);
	assert(strcmp(esc_shell("file #1 [important].txt", buffer), "file\\ \\#1\\ \\[important\\].txt") == 0);
	assert(strcmp(esc_shell("/tmp/test (1).txt", buffer), "/tmp/test\\ \\(1\\).txt") == 0);
	assert(strcmp(esc_shell("var=$HOME/bin:$PATH", buffer), "var\\=\\$HOME/bin:\\$PATH") == 0);

	/* edge cases with multiple escapes */
	assert(strcmp(esc_shell("a\\ b", buffer), "a\\\\\\ b") == 0);
	assert(strcmp(esc_shell("'\"test\"'", buffer), "\\'\\\"test\\\"\\'") == 0);
	assert(strcmp(esc_shell("$(echo \"test\")", buffer), "\\$\\(echo\\ \\\"test\\\"\\)") == 0);

	/* all safe characters that don't need escaping */
	assert(strcmp(esc_shell("abcdefghijklmnopqrstuvwxyz", buffer), "abcdefghijklmnopqrstuvwxyz") == 0);
	assert(strcmp(esc_shell("ABCDEFGHIJKLMNOPQRSTUVWXYZ", buffer), "ABCDEFGHIJKLMNOPQRSTUVWXYZ") == 0);
	assert(strcmp(esc_shell("0123456789", buffer), "0123456789") == 0);
	assert(strcmp(esc_shell("._-+,@:", buffer), "._-+,@:") == 0);
	assert(strcmp(esc_shell("/path/to/file", buffer), "/path/to/file") == 0);
	assert(strcmp(esc_shell("simple_file-name.txt", buffer), "simple_file-name.txt") == 0);

	/* file extensions and versions */
	assert(strcmp(esc_shell("file.tar.gz", buffer), "file.tar.gz") == 0);
	assert(strcmp(esc_shell("app-v1.2.3.deb", buffer), "app-v1.2.3.deb") == 0);
	assert(strcmp(esc_shell("test_2024-01-01.log", buffer), "test_2024-01-01.log") == 0);
#endif

	random_seed(0);
	assert(random_u8() == 0xAF);
	assert(random_u64() == 0x6E789E6AA1B965F4ULL);
}

/**
 * Runs a fast self-test for functionality that could be miscompiled,
 * primarily to verify inline assembly clobber and ABI requirements.
 */
void selftest(void)
{
	log_tag("selftest:\n");
	log_flush();

	msg_progress("Self-test...\n");

	/* large file check */
	if (sizeof(off_t) < sizeof(uint64_t)) {
		/* LCOV_EXCL_START */
		log_fatal(EINTERNAL, "Missing support for large files\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* utilities and low-level helpers */
	if (util_selftest() != 0) {
		/* LCOV_EXCL_START */
		log_fatal(EINTERNAL, "Failed UTIL test\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* non-cryptographic hash functions (murmur3, spooky2, museair) */
	test_hash();

	/* hardware and software crc32c */
	test_crc32c();

	/* tommyds data structures (hash tables, lists, search, sort) */
	test_tommy();

	/* cauchy raid module self-test */
	raid_mode(RAID_MODE_CAUCHY_RAID);
	if (raid_selftest() != 0) {
		/* LCOV_EXCL_START */
		log_fatal(EINTERNAL, "Failed SELF Cauchy RAID test\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* cauchy aes module self-test */
	raid_mode(RAID_MODE_CAUCHY_AES);
	if (raid_selftest() != 0) {
		/* LCOV_EXCL_START */
		log_fatal(EINTERNAL, "Failed SELF Cauchy AES test\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* vandermonde raid module self-test */
	raid_mode(RAID_MODE_VANDERMONDE_RAID);
	if (raid_selftest() != 0) {
		/* LCOV_EXCL_START */
		log_fatal(EINTERNAL, "Failed SELF Vandermonde RAID test\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* array sorting helper */
	if (raid_test_sort() != 0) {
		/* LCOV_EXCL_START */
		log_fatal(EINTERNAL, "Failed SORT test\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* sorted array insertion helper */
	if (raid_test_insert() != 0) {
		/* LCOV_EXCL_START */
		log_fatal(EINTERNAL, "Failed INSERT test\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* permutation and combination generators */
	if (raid_test_combo() != 0) {
		/* LCOV_EXCL_START */
		log_fatal(EINTERNAL, "Failed COMBO test\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* gfni affine transformation matrices */
	if (raid_test_gfaffine() != 0) {
		/* LCOV_EXCL_START */
		log_fatal(EINTERNAL, "Failed GFNI AFFINE test\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	/* restore default mode */
	raid_mode(RAID_MODE_CAUCHY_RAID);
}

/**
 * Runs the extensive test suite, including all unit tests and selftest.
 */
void test(int argc, char* argv[])
{
	uint64_t t_start;
	uint64_t t_selftest_start;
	uint64_t t_selftest_end;

	/* special testing code */
	if (argc < 2 || strcmp(argv[1], "test") != 0)
		return;

	t_start = os_tick_ms();

	lock_init();
	crc32c_init();
	raid_init();

	msg_progress("Test...\n");

	test_misc(argc, argv);

	test_raid();

	/* split parity layout and size calculations */
	test_parity();

	/* buffered streaming i/o and crc verification */
	test_stream();

	/* wildcard and path pattern matching */
	test_wnmatch();

	/* filter pattern parsing */
	test_filter();

	/* pathname import and export */
	test_path();

	/* smartctl output parsing */
	test_parse_smartctl();

	/* smart attribute ignore rules */
	test_smart_ignore();

	t_selftest_start = os_tick_ms();
	selftest();
	t_selftest_end = os_tick_ms();

	printf("Test: %" PRIu64 " ms\n", (t_selftest_start - t_start));
	printf("Selftest: %" PRIu64 " ms\n", (t_selftest_end - t_selftest_start));
	printf("Everything OK\n");

	lock_done();

	exit(EXIT_SUCCESS);
}

