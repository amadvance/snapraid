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

struct test_vector {
	const char* data;
	int len;
	unsigned char digest[HASH_SIZE];
};

/**
 * Test vectors for MurmorHash3_x86_128
 */
struct test_vector TEST_MURMUR3[] = {
#include "murmur3test.c"
{ 0, 0, { 0 } }
};

/**
 * Test vectors for SpookyHash_128
 */
struct test_vector TEST_SPOOKY2[] = {
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

static void hashtest(void)
{
	unsigned i;
	unsigned char seed[HASH_SIZE];

	seed[0] = 0x5d;
	seed[1] = 0x79;
	seed[2] = 0x66;
	seed[3] = 0xa7;
	seed[4] = 0x73;
	seed[5] = 0x27;
	seed[6] = 0x02;
	seed[7] = 0x2f;
	seed[8] = 0x6a;
	seed[9] = 0xa1;
	seed[10] = 0x9e;
	seed[11] = 0xc1;
	seed[12] = 0x14;
	seed[13] = 0x8c;
	seed[14] = 0x9e;
	seed[15] = 0x43;

	for(i=0;TEST_MURMUR3[i].data;++i) {
		unsigned char digest[HASH_SIZE];
		memhash(HASH_MURMUR3, seed, digest, TEST_MURMUR3[i].data, TEST_MURMUR3[i].len);
		if (memcmp(digest, TEST_MURMUR3[i].digest, HASH_SIZE) != 0) {
			fprintf(stderr, "Failed Murmur3 test vector\n");
			exit(EXIT_FAILURE);
		}
	}

	for(i=0;TEST_SPOOKY2[i].data;++i) {
		unsigned char digest[HASH_SIZE];
		memhash(HASH_SPOOKY2, seed, digest, TEST_SPOOKY2[i].data, TEST_SPOOKY2[i].len);
		if (memcmp(digest, TEST_SPOOKY2[i].digest, HASH_SIZE) != 0) {
			fprintf(stderr, "Failed Spooky2 test vector %u\n", i);
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

	for(i=1;i<=33;++i) {
		raid5test(i, 2048);
		raid6test(i, 2048);
	}
}

