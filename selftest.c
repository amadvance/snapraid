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
#include "cpu.h"
#include "elem.h"
#include "state.h"
#include "combo.h"

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

/**
 * Binomial coefficient of n over r.
 */
static unsigned bc(unsigned n, unsigned r)
{
	if (r == 0 || n == r)
		return 1;
	else
		return bc(n - 1, r - 1) + bc(n - 1, r);
}

static void combotest(void)
{
	unsigned r;
	unsigned count;
	int p[LEV_MAX];

	/* all parities */
	for(r=1;r<=LEV_MAX;++r) {
		/* count combination (r of LEV_MAX) parities */
		count = 0;
		combination_first(r, LEV_MAX, p);
		do {
			++count;
		} while (combination_next(r, LEV_MAX, p));

		if (count != bc(LEV_MAX, r)) {
			fprintf(stderr, "Failed COMBO test\n");
			exit(EXIT_FAILURE);
		}
	}
}

static void recovtest(unsigned mode, unsigned ndata, unsigned block_size)
{
	void* buffer_alloc;
	unsigned char** buffer;
	unsigned char** data;
	unsigned char** parity;
	unsigned char** test;
	unsigned char* data_save[LEV_MAX];
	unsigned char* parity_save[LEV_MAX];
	unsigned char* zero;
	unsigned char* waste;
	unsigned buffermax;
	int d[LEV_MAX];
	int p[LEV_MAX];
	unsigned i;
	unsigned j;
	unsigned r;
	void (*map[LEV_MAX][4])(unsigned level, const int* d, const int* c, unsigned char** vbuf, unsigned data, unsigned char* zero, unsigned size);
	unsigned mac[LEV_MAX];
	unsigned nrec;

	raid_set(mode);
	if (mode == RAID_MODE_CAUCHY)
		nrec = RAID_PARITY_CAUCHY_MAX;
	else
		nrec = RAID_PARITY_VANDERMONDE_MAX;

	buffermax = ndata + nrec * 2 + 2;

	buffer = malloc_nofail_vector_align(ndata, buffermax, block_size, &buffer_alloc);
	mtest_vector(buffer, buffermax, block_size);

	data = buffer;
	parity = buffer + ndata;
	test = buffer + ndata + nrec;

	for(i=0;i<nrec;++i)
		parity_save[i] = parity[i];

	zero = buffer[buffermax-2];
	memset(zero, 0, block_size);

	waste = buffer[buffermax-1];

	/* fill data disk with random */
	for(i=0;i<ndata;++i) {
		for(j=0;j<block_size;++j)
			data[i][j] = rand();
	}

	/* setup recov functions */
	for(i=0;i<nrec;++i) {
		mac[i] = 0;
		if (i == 0) {
			map[i][mac[i]++] = raid_rec1_int8;
#if defined(__i386__) || defined(__x86_64__)
			if (cpu_has_ssse3()) {
				map[i][mac[i]++] = raid_rec1_ssse3;
			}
#endif
		} else if (i == 1) {
			map[i][mac[i]++] = raid_rec2_int8;
#if defined(__i386__) || defined(__x86_64__)
			if (cpu_has_ssse3()) {
				map[i][mac[i]++] = raid_rec2_ssse3;
			}
#endif
		} else {
			map[i][mac[i]++] = raid_recX_int8;
#if defined(__i386__) || defined(__x86_64__)
			if (cpu_has_ssse3()) {
				map[i][mac[i]++] = raid_recX_ssse3;
			}
#endif
		}
	}

	/* compute the parity */
	raid_par(nrec, buffer, ndata, block_size);

	/* set all the parity to the waste buffer */
	for(i=0;i<nrec;++i)
		parity[i] = waste;

	/* all parity levels */
	for(r=1;r<=nrec;++r) {
		/* all combinations (r of ndata) disks */
		combination_first(r, ndata, d);
		do {
			/* all combinations (r of nrec) parities */
			combination_first(r, nrec, p);
			do {
				/* for each recover function */
				for(j=0;j<mac[r-1];++j) {
					/* set */
					for(i=0;i<r;++i) {
						/* remove the missing data */
						data_save[i] = data[d[i]];
						data[d[i]] = test[i];
						/* set the parity to use */
						parity[p[i]] = parity_save[p[i]];
					}

					/* recover */
					map[r-1][0](r, d, p, buffer, ndata, zero, block_size);

					/* check */
					for(i=0;i<r;++i) {
						if (memcmp(test[i], data_save[i], block_size) != 0) {
							fprintf(stderr, "Failed RECOV test\n");
							exit(EXIT_FAILURE);
						}
					}

					/* restore */
					for(i=0;i<r;++i) {
						/* restore the data */
						data[d[i]] = data_save[i];
						/* restore the parity */
						parity[p[i]] = waste;
					}
				}
			} while (combination_next(r, nrec, p));
		} while (combination_next(r, ndata, d));
	}

	free(buffer_alloc);
	free(buffer);
}

static void gentest(unsigned mode, unsigned ndata, unsigned block_size)
{
	void* buffer_alloc;
	unsigned char** buffer;
	unsigned buffermax;
	unsigned i, j;
	void (*map[64])(unsigned char** buffer, unsigned ndata, unsigned size);
	unsigned mac;
	unsigned npar;

	raid_set(mode);
	if (mode == RAID_MODE_CAUCHY)
		npar = RAID_PARITY_CAUCHY_MAX;
	else
		npar = RAID_PARITY_VANDERMONDE_MAX;

	buffermax = ndata + npar * 2;

	buffer = malloc_nofail_vector_align(ndata, buffermax, block_size, &buffer_alloc);
	mtest_vector(buffer, buffermax, block_size);

	/* fill with random */
	for(i=0;i<ndata;++i) {
		for(j=0;j<block_size;++j)
			buffer[i][j] = rand();
	}

	/* compute the parity */
	raid_par(npar, buffer, ndata, block_size);

	/* copy in back buffers */
	for(i=0;i<npar;++i)
		memcpy(buffer[ndata + npar + i], buffer[ndata + i], block_size);

	/* load all the available functions */
	mac = 0;

	map[mac++] = raid_par1_int32;
	map[mac++] = raid_par1_int64;
	map[mac++] = raid_par2_int32;
	map[mac++] = raid_par2_int64;

#if defined(__i386__) || defined(__x86_64__)
	if (cpu_has_sse2()) {
		map[mac++] = raid_par1_sse2;
		map[mac++] = raid_par2_sse2;
#if defined(__x86_64__)
		map[mac++] = raid_par2_sse2ext;
#endif
	}

	if (mode == RAID_MODE_CAUCHY) {
		map[mac++] = raid_par3_int8;
		map[mac++] = raid_par4_int8;
		map[mac++] = raid_par5_int8;
		map[mac++] = raid_par6_int8;

		if (cpu_has_ssse3()) {
			map[mac++] = raid_par3_ssse3;
			map[mac++] = raid_par4_ssse3;
			map[mac++] = raid_par5_ssse3;
			map[mac++] = raid_par6_ssse3;
#if defined(__x86_64__)
			map[mac++] = raid_par3_ssse3ext;
			map[mac++] = raid_par4_ssse3ext;
			map[mac++] = raid_par5_ssse3ext;
			map[mac++] = raid_par6_ssse3ext;
#endif
		}
#endif
	} else {
#if defined(__i386__) || defined(__x86_64__)
		map[mac++] = raid_parz_int32;
		map[mac++] = raid_parz_int64;
		if (cpu_has_sse2()) {
			map[mac++] = raid_parz_sse2;
#if defined(__x86_64__)
			map[mac++] = raid_parz_sse2ext;
#endif
		}
#endif
	}

	/* check all the functions */
	for(j=0;j<mac;++j) {
		/* compute parity */
		map[j](buffer, ndata, block_size);

		/* check it */
		for(i=0;i<npar;++i) {
			if (memcmp(buffer[ndata + npar + i], buffer[ndata + i], block_size) != 0) {
				fprintf(stderr, "Failed GEN test\n");
				exit(EXIT_FAILURE);
			}
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

void selftest()
{
	fprintf(stdlog, "selftest:\n");
	fflush(stdlog);

	printf("Self test...\n");

	/* large file check */
	if (sizeof(off_t) < sizeof(uint64_t)) {
		fprintf(stderr, "Missing support for large files\n");
		exit(EXIT_FAILURE);
	}

	hashtest();
	crc32ctest();
	combotest();
	gentest(RAID_MODE_VANDERMONDE, 32, 256);
	recovtest(RAID_MODE_VANDERMONDE, 12, 256);
	gentest(RAID_MODE_CAUCHY, 32, 256);
	recovtest(RAID_MODE_CAUCHY, 12, 256);
	gentest(RAID_MODE_CAUCHY, 1, 256);
}

