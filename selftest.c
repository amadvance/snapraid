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
#include "raid.h"

struct hash_test_vector {
	const char* data;
	int len;
	unsigned char digest[HASH_SIZE];
};

/**
 * Test vectors for MurmorHash3_x86_128
 */
static struct hash_test_vector TEST_MURMUR3[] = {
#include "murmur3test.c"
{ 0, 0, { 0 } }
};

/**
 * Test vectors for SpookyHash_128
 */
static struct hash_test_vector TEST_SPOOKY2[] = {
#include "spooky2test.c"
{ 0, 0, { 0 } }
};

static void raid5test(unsigned diskmax, unsigned block_size)
{
	void* buffer_alloc;
	unsigned char* buffer_aligned;
	unsigned char** buffer;
	unsigned char* buffer_save;
	unsigned buffermax;
	unsigned i, j;

	buffermax = diskmax + 1 + 1;
	buffer_aligned = malloc_nofail_align(buffermax * block_size, &buffer_alloc);
	buffer = malloc_nofail(buffermax * sizeof(void*));
	for(i=0;i<buffermax;++i) {
		buffer[i] = buffer_aligned + i * block_size;
	}
	buffer_save = buffer[buffermax-1];

	/* fill with random */
	for(i=0;i<diskmax;++i) {
		for(j=0;j<block_size;++j)
			buffer[i][j] = rand();
	}

	raid_gen(1, buffer, diskmax, block_size);

	for(i=0;i<diskmax;++i) {
		/* save the correct one */
		memcpy(buffer_save, buffer[i], block_size);

		/* destroy it */
		memset(buffer[i], 0x55, block_size);

		/* recover */
		raid5_recov_data(buffer, diskmax, block_size, i);

		/* check */
		if (memcmp(buffer_save, buffer[i], block_size) != 0) {
			fprintf(stderr, "Failed RAID5 test\n");
			exit(EXIT_FAILURE);
		}
	}

	free(buffer_alloc);
	free(buffer);
}

static void raid6test(unsigned diskmax, unsigned block_size)
{
	void* buffer_alloc;
	unsigned char* buffer_aligned;
	unsigned char** buffer;
	unsigned char* buffer_save0;
	unsigned char* buffer_save1;
	unsigned char* buffer_zero;
	unsigned buffermax;
	unsigned i, j;

	buffermax = diskmax + 2 + 2 + 1;
	buffer_aligned = malloc_nofail_align(buffermax * block_size, &buffer_alloc);
	buffer = malloc_nofail(buffermax * sizeof(void*));
	for(i=0;i<buffermax;++i) {
		buffer[i] = buffer_aligned + i * block_size;
	}
	buffer_save0 = buffer[buffermax-3];
	buffer_save1 = buffer[buffermax-2];
	buffer_zero = buffer[buffermax-1];
	memset(buffer_zero, 0, block_size);

	/* fill with random */
	for(i=0;i<diskmax;++i) {
		for(j=0;j<block_size;++j)
			buffer[i][j] = rand();
	}

	raid_gen(2, buffer, diskmax, block_size);

	/* check 2data */
	for(i=0;i<diskmax;++i) {
		for(j=i+1;j<diskmax;++j) {
			/* save the correct ones */
			memcpy(buffer_save0, buffer[i], block_size);
			memcpy(buffer_save1, buffer[j], block_size);

			/* destroy it */
			memset(buffer[i], 0x55, block_size);
			memset(buffer[j], 0x55, block_size);

			/* recover */
			raid6_recov_2data(buffer, diskmax, block_size, i, j, buffer_zero);

			/* check */
			if (memcmp(buffer_save0, buffer[i], block_size) != 0
				|| memcmp(buffer_save1, buffer[j], block_size) != 0
			) {
				fprintf(stderr, "Failed RAID6 test\n");
				exit(EXIT_FAILURE);
			}
		}
	}

	/* check datap */
	for(i=0;i<diskmax;++i) {
		/* the other is the parity */
		j = diskmax;

		/* save the correct ones */
		memcpy(buffer_save0, buffer[i], block_size);
		memcpy(buffer_save1, buffer[j], block_size);

		/* destroy it */
		memset(buffer[i], 0x55, block_size);
		memset(buffer[j], 0x55, block_size);

		/* recover */
		raid6_recov_datap(buffer, diskmax, block_size, i, buffer_zero);

		/* check */
		if (memcmp(buffer_save0, buffer[i], block_size) != 0
			|| memcmp(buffer_save1, buffer[j], block_size) != 0
		) {
			fprintf(stderr, "Failed RAID6 test\n");
			exit(EXIT_FAILURE);
		}
	}

	free(buffer_alloc);
	free(buffer);
}

#define HASH_TEST_MAX 512 /* tests are never longer than 512 bytes */

static void hashtest(void)
{
	unsigned i;
	unsigned char* seed_aligned;
	void* seed_alloc;
	unsigned char* buffer_aligned;
	void* buffer_alloc;

	seed_aligned = malloc_nofail_align(HASH_SIZE, &seed_alloc);
	buffer_aligned = malloc_nofail_align(HASH_TEST_MAX, &buffer_alloc);

	seed_aligned[0] = 0x5d;
	seed_aligned[1] = 0x79;
	seed_aligned[2] = 0x66;
	seed_aligned[3] = 0xa7;
	seed_aligned[4] = 0x73;
	seed_aligned[5] = 0x27;
	seed_aligned[6] = 0x02;
	seed_aligned[7] = 0x2f;
	seed_aligned[8] = 0x6a;
	seed_aligned[9] = 0xa1;
	seed_aligned[10] = 0x9e;
	seed_aligned[11] = 0xc1;
	seed_aligned[12] = 0x14;
	seed_aligned[13] = 0x8c;
	seed_aligned[14] = 0x9e;
	seed_aligned[15] = 0x43;

	for(i=0;TEST_MURMUR3[i].data;++i) {
		unsigned char digest[HASH_SIZE];
		memcpy(buffer_aligned, TEST_MURMUR3[i].data, TEST_MURMUR3[i].len);
		memhash(HASH_MURMUR3, seed_aligned, digest, buffer_aligned, TEST_MURMUR3[i].len);
		if (memcmp(digest, TEST_MURMUR3[i].digest, HASH_SIZE) != 0) {
			fprintf(stderr, "Failed Murmur3 test vector\n");
			exit(EXIT_FAILURE);
		}
	}

	for(i=0;TEST_SPOOKY2[i].data;++i) {
		unsigned char digest[HASH_SIZE];
		memcpy(buffer_aligned, TEST_SPOOKY2[i].data, TEST_SPOOKY2[i].len);
		memhash(HASH_SPOOKY2, seed_aligned, digest, buffer_aligned, TEST_SPOOKY2[i].len);
		if (memcmp(digest, TEST_SPOOKY2[i].digest, HASH_SIZE) != 0) {
			fprintf(stderr, "Failed Spooky2 test vector %u\n", i);
			exit(EXIT_FAILURE);
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

static void crc32ctest(void)
{
	unsigned i;

	for(i=0;TEST_CRC32C[i].data;++i) {
		uint32_t digest;
		digest = CRC_IV ^ crc32c(CRC_IV, (const unsigned char*)TEST_CRC32C[i].data, TEST_CRC32C[i].len);
		if (digest != TEST_CRC32C[i].digest) {
			fprintf(stderr, "Failed CRC32C test vector %u\n", i);
			exit(EXIT_FAILURE);
		}
	}
}

void selftest(int gui)
{
	unsigned i;

	if (gui) {
		fprintf(stdlog, "selftest:\n");
		fflush(stdlog);
	}
	
	printf("Self test...\n");

	/* large file check */
	if (sizeof(off_t) < sizeof(uint64_t)) {
		fprintf(stderr, "Missing support for large files\n");
		exit(EXIT_FAILURE);
	}

	hashtest();
	crc32ctest();

	for(i=1;i<=33;++i) {
		raid5test(i, 2048);
		raid6test(i, 2048);
	}
}

