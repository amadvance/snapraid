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
	unsigned char digest[HASH_SIZE];
};

/**
 * Test vectors for MD5 from RFC1321.
 *
 * MD5("") = d41d8cd98f00b204e9800998ecf8427e
 * MD5("a") = 0cc175b9c0f1b6a831c399e269772661
 * MD5("abc") = 900150983cd24fb0d6963f7d28e17f72
 * MD5("message digest") = f96b697d7cb7938d525a2f31aaf161d0
 * MD5("abcdefghijklmnopqrstuvwxyz") = c3fcd3d76192e4007dfb496cca67e13b
 * MD5("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789") = d174ab98d277d9f5a5611c2c9f419d9f
 * MD5("12345678901234567890123456789012345678901234567890123456789012345678901234567890") = 57edf4a22be3c955ac49da2e2107b67a
 */
struct test_vector TEST_MD5[] = {
{ "", { 0xd4, 0x1d, 0x8c, 0xd9, 0x8f, 0x00, 0xb2, 0x04, 0xe9, 0x80, 0x09, 0x98, 0xec, 0xf8, 0x42, 0x7e } },
{ "a", { 0x0c, 0xc1, 0x75, 0xb9, 0xc0, 0xf1, 0xb6, 0xa8, 0x31, 0xc3, 0x99, 0xe2, 0x69, 0x77, 0x26, 0x61 } },
{ "abc", { 0x90, 0x01, 0x50, 0x98, 0x3c, 0xd2, 0x4f, 0xb0, 0xd6, 0x96, 0x3f, 0x7d, 0x28, 0xe1, 0x7f, 0x72 } },
{ "message digest", { 0xf9, 0x6b, 0x69, 0x7d, 0x7c, 0xb7, 0x93, 0x8d, 0x52, 0x5a, 0x2f, 0x31, 0xaa, 0xf1, 0x61, 0xd0 } },
{ "abcdefghijklmnopqrstuvwxyz", { 0xc3, 0xfc, 0xd3, 0xd7, 0x61, 0x92, 0xe4, 0x00, 0x7d, 0xfb, 0x49, 0x6c, 0xca, 0x67, 0xe1, 0x3b } },
{ "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789", { 0xd1, 0x74, 0xab, 0x98, 0xd2, 0x77, 0xd9, 0xf5, 0xa5, 0x61, 0x1c, 0x2c, 0x9f, 0x41, 0x9d, 0x9f } },
{ "12345678901234567890123456789012345678901234567890123456789012345678901234567890", { 0x57, 0xed, 0xf4, 0xa2, 0x2b, 0xe3, 0xc9, 0x55, 0xac, 0x49, 0xda, 0x2e, 0x21, 0x07, 0xb6, 0x7a, } },
{ 0, { 0 } }
};

/**
 * Test vectors for MurmorHash3_x86_128
 */
struct test_vector TEST_MURMUR3[] = {
{ "", { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
{ "a", { 0x3c, 0x93, 0x94, 0xa7, 0x1b, 0xb0, 0x56, 0x55, 0x1b, 0xb0, 0x56, 0x55, 0x1b, 0xb0, 0x56, 0x55 } },
{ "abc", { 0xd1, 0xc6, 0xcd, 0x75, 0xa5, 0x06, 0xb0, 0xa2, 0xa5, 0x06, 0xb0, 0xa2, 0xa5, 0x06, 0xb0, 0xa2 } },
{ "message digest", { 0xea, 0x7b, 0x82, 0xc1, 0x89, 0x77, 0xdb, 0x26, 0xb0, 0x30, 0xfe, 0xbc, 0xef, 0x42, 0x4b, 0xda } },
{ "abcdefghijklmnopqrstuvwxyz", { 0x13, 0x06, 0x34, 0x3e, 0x66, 0x2f, 0x6f, 0x66, 0x6e, 0x56, 0xf6, 0x17, 0x2c, 0x3d, 0xe3, 0x44 } },
{ "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789", { 0xe7, 0xd6, 0x62, 0x29, 0x32, 0xf7, 0xdd, 0x48, 0x29, 0x38, 0xed, 0x07, 0x32, 0x0c, 0x72, 0x12 } },
{ "1234567890123456789012345678901234567890123456789012345678901234", { 0x8e, 0x2e, 0xd6, 0x0c, 0xbb, 0x4d, 0x10, 0x7e, 0x7a, 0x78, 0x72, 0x2e, 0xab, 0x9a, 0x6c, 0x0d } },
{ "12345678901234567890123456789012345678901234567890123456789012345", { 0x45, 0xe6, 0x26, 0xa0, 0xf5, 0xdb, 0xf8, 0x22, 0x3b, 0x5e, 0xc5, 0x41, 0x10, 0xc5, 0x06, 0x36 } },
{ "123456789012345678901234567890123456789012345678901234567890123456", { 0xb5, 0x5e, 0xdd, 0xa2, 0xa6, 0x65, 0xfd, 0x53, 0xec, 0xb8, 0xc9, 0x93, 0x97, 0x97, 0x65, 0x22 } },
{ "1234567890123456789012345678901234567890123456789012345678901234567", { 0x19, 0x40, 0x6f, 0xb8, 0xc4, 0x30, 0x59, 0x00, 0x56, 0x10, 0xe7, 0xdd, 0x9a, 0x48, 0x83, 0xa5 } },
{ "12345678901234567890123456789012345678901234567890123456789012345678", { 0x77, 0xa8, 0xb1, 0xe6, 0xb2, 0xbc, 0x2c, 0x4f, 0x91, 0x0c, 0x32, 0xb3, 0x1d, 0x28, 0xf2, 0xd4 } },
{ "123456789012345678901234567890123456789012345678901234567890123456789", { 0xe0, 0x5a, 0x5f, 0xe7, 0x0c, 0x2a, 0xa9, 0x63, 0x9a, 0x8e, 0x41, 0xbe, 0x73, 0xa3, 0x25, 0xe4 } },
{ "1234567890123456789012345678901234567890123456789012345678901234567890", { 0x8e, 0x8f, 0x93, 0xcf, 0xdf, 0x1f, 0xe5, 0xfe, 0x09, 0x35, 0xe3, 0x53, 0x1e, 0x5b, 0x75, 0x37 } },
{ "12345678901234567890123456789012345678901234567890123456789012345678901", { 0x46, 0xd5, 0xd2, 0xa3, 0x6d, 0xc9, 0x20, 0x0d, 0xd6, 0x6a, 0x90, 0x9e, 0xda, 0xfb, 0x1c, 0x26 } },
{ "123456789012345678901234567890123456789012345678901234567890123456789012", { 0x4a, 0x26, 0xe4, 0xae, 0x00, 0x32, 0x08, 0xfd, 0x0b, 0xee, 0x6c, 0x0f, 0xb2, 0x64, 0xa4, 0x25 } },
{ "1234567890123456789012345678901234567890123456789012345678901234567890123", { 0x2a, 0x97, 0x3f, 0xe8, 0xb4, 0xf9, 0x4e, 0xf9, 0x45, 0xe2, 0xe4, 0x90, 0x69, 0x14, 0xa6, 0x24 } },
{ "12345678901234567890123456789012345678901234567890123456789012345678901234", { 0x4b, 0xd7, 0x48, 0x1a, 0x25, 0x8a, 0x2a, 0xab, 0x50, 0xc3, 0xc6, 0xd0, 0x24, 0x62, 0x78, 0xba } },
{ "123456789012345678901234567890123456789012345678901234567890123456789012345", { 0x88, 0xe4, 0x8f, 0x6f, 0xee, 0x22, 0xe5, 0x89, 0x78, 0x24, 0x99, 0x07, 0x73, 0x7e, 0x5b, 0xfd } },
{ "1234567890123456789012345678901234567890123456789012345678901234567890123456", { 0xdd, 0x18, 0x02, 0x02, 0xa9, 0xcd, 0x6e, 0xcb, 0xfe, 0x91, 0x8e, 0x82, 0x4e, 0xa8, 0x33, 0xfe } },
{ "12345678901234567890123456789012345678901234567890123456789012345678901234567", { 0x80, 0xf2, 0xb8, 0x67, 0x5a, 0x02, 0x78, 0x0c, 0xcb, 0x31, 0xd0, 0xf3, 0xb6, 0x67, 0x52, 0xca } },
{ "123456789012345678901234567890123456789012345678901234567890123456789012345678", { 0x66, 0x85, 0x66, 0x0f, 0xc7, 0x56, 0xfb, 0xb7, 0x9b, 0xa2, 0x28, 0x37, 0x54, 0x89, 0xbb, 0x48 } },
{ "1234567890123456789012345678901234567890123456789012345678901234567890123456789", { 0x5b, 0x59, 0x21, 0x42, 0xae, 0x72, 0xbe, 0xd4, 0x4a, 0xa7, 0xf2, 0x12, 0x35, 0x48, 0xc6, 0xd8 } },
{ "12345678901234567890123456789012345678901234567890123456789012345678901234567890", { 0xf2, 0x6c, 0x2a, 0x8f, 0x58, 0x23, 0x4c, 0x8e, 0x2c, 0x74, 0x41, 0x66, 0x0e, 0xc2, 0xd3, 0x91 } },
{ 0, { 0 } }
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

	for(i=0;TEST_MD5[i].data;++i) {
		unsigned char digest[HASH_SIZE];
		memhash(HASH_MD5, digest, TEST_MD5[i].data, strlen(TEST_MD5[i].data));
		if (memcmp(digest, TEST_MD5[i].digest, HASH_SIZE) != 0) {
			fprintf(stderr, "Failed MD5 test vector\n");
			exit(EXIT_FAILURE);
		}
	}

	for(i=0;TEST_MURMUR3[i].data;++i) {
		unsigned char digest[HASH_SIZE];
		memhash(HASH_MURMUR3, digest, TEST_MURMUR3[i].data, strlen(TEST_MURMUR3[i].data));
		if (memcmp(digest, TEST_MURMUR3[i].digest, HASH_SIZE) != 0) {
			fprintf(stderr, "Failed Murmur3 test vector\n");
			exit(EXIT_FAILURE);
		}
	}
}

void selftest(int gui)
{
	unsigned i;

	if (gui) {
		fprintf(stderr, "selftest:\n");
		fflush(stderr);
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

