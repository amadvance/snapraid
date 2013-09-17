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
	unsigned char** buffer;
	unsigned char* buffer_test;
	unsigned buffermax;
	unsigned i, j;

	buffermax = diskmax + 1 + 1;

	buffer = malloc_nofail_vector_align(buffermax, block_size, &buffer_alloc);

	buffer_test = buffer[buffermax-1];

	/* fill with random */
	for(i=0;i<diskmax;++i) {
		for(j=0;j<block_size;++j)
			buffer[i][j] = rand();
	}

	/* compute the parity */
	raid_gen(1, buffer, diskmax, block_size);

	for(i=0;i<diskmax;++i) {
		unsigned char* a;

		/* save */
		a = buffer[i];

		/* damage */
		buffer[i] = buffer_test;

		/* recover */
		raid5_recov_data(buffer, diskmax, block_size, i);

		/* restore */
		buffer[i] = a;

		/* check */
		if (memcmp(buffer_test, buffer[i], block_size) != 0) {
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
	unsigned char** buffer;
	unsigned char* buffer_test0;
	unsigned char* buffer_test1;
	unsigned char* buffer_zero;
	unsigned char* buffer_bad;
	unsigned buffermax;
	unsigned i, j;

	buffermax = diskmax + 2 + 2 + 1 + 1;

	buffer = malloc_nofail_vector_align(buffermax, block_size, &buffer_alloc);

	buffer_test0 = buffer[buffermax-4];
	buffer_test1 = buffer[buffermax-3];
	buffer_zero = buffer[buffermax-2];
	buffer_bad = buffer[buffermax-1];
	memset(buffer_zero, 0, block_size);
	memset(buffer_bad, 0xA5, block_size);

	/* fill with random */
	for(i=0;i<diskmax;++i) {
		for(j=0;j<block_size;++j)
			buffer[i][j] = rand();
	}

	/* compute the parity */
	raid_gen(2, buffer, diskmax, block_size);

	/* check 2data */
	for(i=0;i<diskmax;++i) {
		for(j=i+1;j<diskmax;++j) {
			unsigned char* a;
			unsigned char* b;

			/* save */
			a = buffer[i];
			b = buffer[j];

			/* damage */
			buffer[i] = buffer_test0;
			buffer[j] = buffer_test1;

			/* recover */
			raid6_recov_2data(buffer, diskmax, block_size, i, j, buffer_zero);

			/* restore */
			buffer[i] = a;
			buffer[j] = b;

			/* check */
			if (memcmp(buffer_test0, buffer[i], block_size) != 0
				|| memcmp(buffer_test1, buffer[j], block_size) != 0
			) {
				fprintf(stderr, "Failed RAID6 test\n");
				exit(EXIT_FAILURE);
			}
		}
	}

	/* check datap */
	for(i=0;i<diskmax;++i) {
		unsigned char* a;
		unsigned char* p;

		/* other is p */
		j = diskmax;

		/* save */
		a = buffer[i];
		p = buffer[j];

		/* damage */
		buffer[i] = buffer_test0;
		buffer[j] = buffer_bad;

		/* recover */
		raid6_recov_datap(buffer, diskmax, block_size, i, buffer_zero);

		/* restore */
		buffer[i] = a;
		buffer[j] = p;

		/* check */
		if (memcmp(buffer_test0, buffer[i], block_size) != 0) {
			fprintf(stderr, "Failed RAID6 test\n");
			exit(EXIT_FAILURE);
		}
	}

	free(buffer_alloc);
	free(buffer);
}

static void raidTPtest(unsigned diskmax, unsigned block_size)
{
	void* buffer_alloc;
	unsigned char** buffer;
	unsigned char* buffer_test0;
	unsigned char* buffer_test1;
	unsigned char* buffer_test2;
	unsigned char* buffer_zero;
	unsigned char* buffer_bad;
	unsigned buffermax;
	unsigned i, j, k;

	buffermax = diskmax + 3 + 3 + 1 + 1;

	buffer = malloc_nofail_vector_align(buffermax, block_size, &buffer_alloc);

	buffer_test0 = buffer[buffermax-5];
	buffer_test1 = buffer[buffermax-4];
	buffer_test2 = buffer[buffermax-3];
	buffer_zero = buffer[buffermax-2];
	buffer_bad = buffer[buffermax-1];
	memset(buffer_zero, 0, block_size);
	memset(buffer_bad, 0xA5, block_size);

	/* fill with random */
	for(i=0;i<diskmax;++i) {
		for(j=0;j<block_size;++j)
			buffer[i][j] = rand();
	}

	/* compute the parity */
	raid_gen(3, buffer, diskmax, block_size);

	/* check 3data */
	for(i=0;i<diskmax;++i) {
		for(j=i+1;j<diskmax;++j) {
			for(k=j+1;k<diskmax;++k) {
				unsigned char* a;
				unsigned char* b;
				unsigned char* c;

				/* save */
				a = buffer[i];
				b = buffer[j];
				c = buffer[k];

				/* damage */
				buffer[i] = buffer_test0;
				buffer[j] = buffer_test1;
				buffer[k] = buffer_test2;

				/* recover */
				raidTP_recov_3data(buffer, diskmax, block_size, i, j, k, buffer_zero);

				/* restore */
				buffer[i] = a;
				buffer[j] = b;
				buffer[k] = c;

				/* check */
				if (memcmp(buffer_test0, buffer[i], block_size) != 0
					|| memcmp(buffer_test1, buffer[j], block_size) != 0
					|| memcmp(buffer_test2, buffer[k], block_size) != 0
				) {
					fprintf(stderr, "Failed RAIDTP test\n");
					exit(EXIT_FAILURE);
				}
			}
		}
	}

	/* check 2dataq */
	for(i=0;i<diskmax;++i) {
		for(j=i+1;j<diskmax;++j) {
			unsigned char* a;
			unsigned char* b;
			unsigned char* q;

			/* other is q */
			k = diskmax + 1;

			/* save */
			a = buffer[i];
			b = buffer[j];
			q = buffer[k];

			/* damage */
			buffer[i] = buffer_test0;
			buffer[j] = buffer_test1;
			buffer[k] = buffer_bad;

			/* recover */
			raidTP_recov_2dataq(buffer, diskmax, block_size, i, j, buffer_zero);

			/* restore */
			buffer[i] = a;
			buffer[j] = b;
			buffer[k] = q;

			/* check */
			if (memcmp(buffer_test0, buffer[i], block_size) != 0
				|| memcmp(buffer_test1, buffer[j], block_size) != 0
			) {
				fprintf(stderr, "Failed RAIDTP test\n");
				exit(EXIT_FAILURE);
			}
		}
	}

	/* check 2datap */
	for(i=0;i<diskmax;++i) {
		for(j=i+1;j<diskmax;++j) {
			unsigned char* a;
			unsigned char* b;
			unsigned char* p;

			/* other is p */
			k = diskmax;

			/* save */
			a = buffer[i];
			b = buffer[j];
			p = buffer[k];

			/* damage */
			buffer[i] = buffer_test0;
			buffer[j] = buffer_test1;
			buffer[k] = buffer_bad;

			/* recover */
			raidTP_recov_2datap(buffer, diskmax, block_size, i, j, buffer_zero);

			/* restore */
			buffer[i] = a;
			buffer[j] = b;
			buffer[k] = p;

			/* check */
			if (memcmp(buffer_test0, buffer[i], block_size) != 0
				|| memcmp(buffer_test1, buffer[j], block_size) != 0
			) {
				fprintf(stderr, "Failed RAIDTP test\n");
				exit(EXIT_FAILURE);
			}
		}
	}

	/* check datapq */
	for(i=0;i<diskmax;++i) {
		unsigned char* a;
		unsigned char* p;
		unsigned char* q;

		/* others are p,q */
		j = diskmax;
		k = diskmax + 1;

		/* save */
		a = buffer[i];
		p = buffer[j];
		q = buffer[k];

		/* damage */
		buffer[i] = buffer_test0;
		buffer[j] = buffer_bad;
		buffer[k] = buffer_bad;

		/* recover */
		raidTP_recov_datapq(buffer, diskmax, block_size, i, buffer_zero);

		/* restore */
		buffer[i] = a;
		buffer[j] = p;
		buffer[k] = q;

		/* check */
		if (memcmp(buffer_test0, buffer[i], block_size) != 0) {
			fprintf(stderr, "Failed RAIDTP test\n");
			exit(EXIT_FAILURE);
		}
	}

	free(buffer_alloc);
	free(buffer);
}

static void raidQPtest(unsigned diskmax, unsigned block_size)
{
	void* buffer_alloc;
	unsigned char** buffer;
	unsigned char* buffer_test0;
	unsigned char* buffer_test1;
	unsigned char* buffer_test2;
	unsigned char* buffer_test3;
	unsigned char* buffer_zero;
	unsigned char* buffer_bad;
	unsigned buffermax;
	unsigned i, j, k, l;
	unsigned n;

	struct combo1 {
		int ignore[3];
		int use;
	} COMBO1[4] = {
		{ { 1, 2, 3 }, 0 },
		{ { 0, 2, 3 }, 1 },
		{ { 0, 1, 3 }, 2 },
		{ { 0, 1, 2 }, 3 }
	};

	struct combo2 {
		int ignore[2];
		int use[2];
	} COMBO2[6] = {
		{ { 0, 1 }, { 2, 3 } },
		{ { 0, 2 }, { 1, 3 } },
		{ { 0, 3 }, { 1, 2 } },
		{ { 1, 2 }, { 0, 3 } },
		{ { 1, 3 }, { 0, 2 } },
		{ { 2, 3 }, { 0, 1 } }
	};

	struct combo3 {
		int ignore;
		int use[3];
	} COMBO3[4] = {
		{ 0, { 1, 2, 3 } },
		{ 1, { 0, 2, 3 } },
		{ 2, { 0, 1, 3 } },
		{ 3, { 0, 1, 2 } }
	};

	buffermax = diskmax + 4 + 4 + 1 + 1;

	buffer = malloc_nofail_vector_align(buffermax, block_size, &buffer_alloc);

	buffer_test0 = buffer[buffermax-6];
	buffer_test1 = buffer[buffermax-5];
	buffer_test2 = buffer[buffermax-4];
	buffer_test3 = buffer[buffermax-3];
	buffer_zero = buffer[buffermax-2];
	buffer_bad = buffer[buffermax-1];
	memset(buffer_zero, 0, block_size);
	memset(buffer_bad, 0xA5, block_size);

	/* fill with random */
	for(i=0;i<diskmax;++i) {
		for(j=0;j<block_size;++j)
			buffer[i][j] = rand();
	}

	/* compute the parity */
	raid_gen(4, buffer, diskmax, block_size);

	/* check 4data */
	for(i=0;i<diskmax;++i) {
		for(j=i+1;j<diskmax;++j) {
			for(k=j+1;k<diskmax;++k) {
				for(l=k+1;l<diskmax;++l) {
					unsigned char* a;
					unsigned char* b;
					unsigned char* c;
					unsigned char* d;

					/* save */
					a = buffer[i];
					b = buffer[j];
					c = buffer[k];
					d = buffer[l];

					/* damage */
					buffer[i] = buffer_test0;
					buffer[j] = buffer_test1;
					buffer[k] = buffer_test2;
					buffer[l] = buffer_test3;

					/* recover */
					raid_recov_4data(buffer, diskmax, block_size, i, j, k, l, 0, 1, 2, 3, buffer_zero);

					/* check */
					if (memcmp(buffer_test0, a, block_size) != 0
						|| memcmp(buffer_test1, b, block_size) != 0
						|| memcmp(buffer_test2, c, block_size) != 0
						|| memcmp(buffer_test3, d, block_size) != 0
					) {
						fprintf(stderr, "Failed RAIDQP test\n");
						exit(EXIT_FAILURE);
					}

					/* restore */
					buffer[i] = a;
					buffer[j] = b;
					buffer[k] = c;
					buffer[l] = d;
				}
			}
		}
	}

	/* check 3data */
	for(i=0;i<diskmax;++i) {
		for(j=i+1;j<diskmax;++j) {
			for(k=j+1;k<diskmax;++k) {
				for(n=0;n<4;++n) {
					unsigned char* a;
					unsigned char* b;
					unsigned char* c;
					unsigned char* p;

					/* save */
					a = buffer[i];
					b = buffer[j];
					c = buffer[k];

					/* damage */
					buffer[i] = buffer_test0;
					buffer[j] = buffer_test1;
					buffer[k] = buffer_test2;

					/* ignore parity */
					l = diskmax + COMBO3[n].ignore;
					p = buffer[l];
					buffer[l] = buffer_bad;

					/* recover ignoring p */
					raid_recov_3data(buffer, diskmax, block_size, i, j, k, COMBO3[n].use[0], COMBO3[n].use[1], COMBO3[n].use[2], buffer_zero);

					/* check */
					if (memcmp(buffer_test0, a, block_size) != 0
						|| memcmp(buffer_test1, b, block_size) != 0
						|| memcmp(buffer_test2, c, block_size) != 0
					) {
						fprintf(stderr, "Failed RAIDQP test 1\n");
						exit(EXIT_FAILURE);
					}

					/* restore parity */
					buffer[l] = p;

					/* restore */
					buffer[i] = a;
					buffer[j] = b;
					buffer[k] = c;
				}
			}
		}
	}

	/* check 2data */
	for(i=0;i<diskmax;++i) {
		for(j=i+1;j<diskmax;++j) {
			for(n=0;n<6;++n) {
				unsigned char* a;
				unsigned char* b;
				unsigned char* p;
				unsigned char* q;

				/* save */
				a = buffer[i];
				b = buffer[j];

				/* damage */
				buffer[i] = buffer_test0;
				buffer[j] = buffer_test1;

				/* ignore parity */
				k = diskmax + COMBO2[n].ignore[0];
				l = diskmax + COMBO2[n].ignore[1];
				p = buffer[k];
				q = buffer[l];
				buffer[k] = buffer_bad;
				buffer[l] = buffer_bad;

				/* recover */
				raid_recov_2data(buffer, diskmax, block_size, i, j, COMBO2[n].use[0], COMBO2[n].use[1], buffer_zero);

				/* check */
				if (memcmp(buffer_test0, a, block_size) != 0
					|| memcmp(buffer_test1, b, block_size) != 0
				) {
					fprintf(stderr, "Failed RAIDQP test 1\n");
					exit(EXIT_FAILURE);
				}

				/* restore parity */
				buffer[k] = p;
				buffer[l] = q;

				/* restore */
				buffer[i] = a;
				buffer[j] = b;
			}
		}
	}

	/* check 1data */
	for(n=0;n<4;++n) {
		for(i=0;i<diskmax;++i) {
			unsigned char* a;
			unsigned char* p;
			unsigned char* q;
			unsigned char* r;

			/* save */
			a = buffer[i];

			/* damage */
			buffer[i] = buffer_test0;

			/* ignore parity */
			j = diskmax + COMBO1[n].ignore[0];
			k = diskmax + COMBO1[n].ignore[1];
			l = diskmax + COMBO1[n].ignore[2];
			p = buffer[j];
			q = buffer[k];
			r = buffer[l];
			buffer[j] = buffer_bad;
			buffer[k] = buffer_bad;
			buffer[l] = buffer_bad;

			/* recover */
			raid_recov_1data(buffer, diskmax, block_size, i, COMBO1[n].use, buffer_zero);

			/* check */
			if (memcmp(buffer_test0, a, block_size) != 0) {
				fprintf(stderr, "Failed RAIDQP test\n");
				exit(EXIT_FAILURE);
			}

			/* restore parity */
			buffer[j] = p;
			buffer[k] = q;
			buffer[l] = r;

			/* restore */
			buffer[i] = a;
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
			fprintf(stderr, "Failed Murmur3 test\n");
			exit(EXIT_FAILURE);
		}
	}

	for(i=0;TEST_SPOOKY2[i].data;++i) {
		unsigned char digest[HASH_SIZE];
		memcpy(buffer_aligned, TEST_SPOOKY2[i].data, TEST_SPOOKY2[i].len);
		memhash(HASH_SPOOKY2, seed_aligned, digest, buffer_aligned, TEST_SPOOKY2[i].len);
		if (memcmp(digest, TEST_SPOOKY2[i].digest, HASH_SIZE) != 0) {
			fprintf(stderr, "Failed Spooky2 test\n");
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
		digest = crc32c(0, (const unsigned char*)TEST_CRC32C[i].data, TEST_CRC32C[i].len);
		if (digest != TEST_CRC32C[i].digest) {
			fprintf(stderr, "Failed CRC32C test\n");
			exit(EXIT_FAILURE);
		}
	}
}

void selftest(int gui)
{
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
	raid5test(33, 1024);
	raid6test(33, 1024);
	raidTPtest(33, 1024);
	raidQPtest(21, 1024);
}

