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
#include "raid/internal.h"
#include "raid/memory.h"
#include "state.h"

/*
 * Size of the blocks to test.
 */
#define TEST_SIZE (256*1024)

/**
 * Differential us of two timeval.
 */
static int64_t diffgettimeofday(struct timeval *start, struct timeval *stop)
{
	int64_t d;

	d = 1000000LL * (stop->tv_sec - start->tv_sec);
	d += stop->tv_usec - start->tv_usec;

	return d;
}

/**
 * Start time measurement.
 */
#define SPEED_START \
	count = 0; \
	gettimeofday(&start, 0); \
	do { \
		for (i = 0; i < delta; ++i)

/**
 * Stop time measurement.
 */
#define SPEED_STOP \
		count += delta; \
		gettimeofday(&stop, 0); \
	} while (diffgettimeofday(&start, &stop) < 1000000LL); \
	ds = size * (int64_t)count * nd; \
	dt = diffgettimeofday(&start, &stop);

void speed(void)
{
	struct timeval start;
	struct timeval stop;
	int64_t ds;
	int64_t dt;
	int i, j;
	unsigned char digest[HASH_SIZE];
	unsigned char seed[HASH_SIZE];
	int id[RAID_PARITY_MAX];
	int ip[RAID_PARITY_MAX];
	int count;
	int delta = 10;
	int size = TEST_SIZE;
	int nd = 8;
	int nv;
	void *v_alloc;
	void **v;

	nv = nd + RAID_PARITY_MAX + 1;

	v = malloc_nofail_vector_align(nd, nv, size, &v_alloc);

	/* initialize disks with fixed data */
	for (i = 0; i < nd; ++i)
		memset(v[i], i, size);

	/* zero v */
	memset(v[nd+RAID_PARITY_MAX], 0, size);
	raid_zero(v[nd+RAID_PARITY_MAX]);

	/* hash seed */
	for (i = 0; i < HASH_SIZE; ++i)
		seed[i] = i;

	/* basic disks and parity mapping */
	for (i = 0; i < RAID_PARITY_MAX; ++i) {
		id[i] = i;
		ip[i] = i;
	}

	printf(PACKAGE " v" VERSION " by Andrea Mazzoleni, " PACKAGE_URL "\n");

#ifdef __GNUC__
	printf("Compiler gcc " __VERSION__ "\n");
#endif

#ifdef CONFIG_X86
	{
		char vendor[CPU_VENDOR_MAX];
		unsigned family;
		unsigned model;

		raid_cpu_info(vendor, &family, &model);

		printf("CPU %s, family %u, model %u, flags%s%s%s%s%s%s%s%s\n", vendor, family, model,
			raid_cpu_has_mmx() ? " mmx" : "",
			raid_cpu_has_sse2() ? " sse2" : "",
			raid_cpu_has_ssse3() ? " ssse3" : "",
			raid_cpu_has_sse42() ? " sse42" : "",
			raid_cpu_has_avx() ? " avx" : "",
			raid_cpu_has_avx2() ? "avx2" : "",
			raid_cpu_has_slowmult() ? " slowmult" : "",
			raid_cpu_has_slowpshufb()  ? " slowpshufb" : ""
		);
	}
#else
	printf("CPU is not a x86/x64\n");
#endif
#if WORDS_BIGENDIAN
	printf("Memory is big-endian %d-bit\n", (int)sizeof(void *) * 8);
#else
	printf("Memory is little-endian %d-bit\n", (int)sizeof(void *) * 8);
#endif

#if HAVE_FUTIMENS
	printf("Support nanosecond timestamps with futimens()\n");
#elif HAVE_FUTIMES
	printf("Support nanosecond timestamps with futimes()\n");
#elif HAVE_FUTIMESAT
	printf("Support nanosecond timestamps with futimesat()\n");
#else
	printf("Does not support nanosecond timestamps\n");
#endif

	printf("\n");

	printf("Speed test using %u data buffers of %u bytes, for a total of %u KiB.\n", nd, size, nd * size / 1024);
	printf("Memory blocks have a displacement of %u bytes to improve cache performance.\n", RAID_MALLOC_DISPLACEMENT);
	printf("The reported value is the aggregate bandwidth of all data blocks in MiB/s,\n");
	printf("not counting parity blocks.\n");
	printf("\n");

	printf("Memory write speed using the C memset() function:\n");
	printf("%8s", "memset");
	fflush(stdout);

	SPEED_START {
		for (j = 0; j < nd; ++j)
			memset(v[j], j, size);
	} SPEED_STOP

	printf("%8"PRIu64, ds / dt);
	printf("\n");
	printf("\n");

	/* crc table */
	printf("CRC used to check the content file integrity:\n");

	printf("%8s", "table");
	fflush(stdout);

	SPEED_START {
		for (j = 0; j < nd; ++j)
			crc32c_gen(0, v[j], size);
	} SPEED_STOP

	printf("%8"PRIu64, ds / dt);
	printf("\n");

	printf("%8s", "intel");
	fflush(stdout);

#if HAVE_CRC32B
	if (raid_cpu_has_sse42()) {
		SPEED_START {
			for (j = 0; j < nd; ++j)
				crc32c_x86(0, v[j], size);
		} SPEED_STOP

		printf("%8"PRIu64, ds / dt);
	}
#endif
	printf("\n");
	printf("\n");

	/* hash table */
	printf("Hash used to check the data blocks integrity:\n");

	printf("%8s", "");
	printf("%8s", "best");
	printf("%8s", "murmur3");
	printf("%8s", "spooky2");
	printf("\n");

	printf("%8s", "hash");
#ifdef CONFIG_X86
	if (sizeof(void *) == 4 && !raid_cpu_has_slowmult())
		printf("%8s", "murmur3");
	else
		printf("%8s", "spooky2");
#else
	if (sizeof(void *) == 4)
		printf("%8s", "murmur3");
	else
		printf("%8s", "spooky2");
#endif
	fflush(stdout);

	SPEED_START {
		for (j = 0; j < nd; ++j)
			memhash(HASH_MURMUR3, seed, digest, v[j], size);
	} SPEED_STOP

	printf("%8"PRIu64, ds / dt);
	fflush(stdout);

	SPEED_START {
		for (j = 0; j < nd; ++j)
			memhash(HASH_SPOOKY2, seed, digest, v[j], size);
	} SPEED_STOP

	printf("%8"PRIu64, ds / dt);
	printf("\n");
	printf("\n");

	/* RAID table */
	printf("RAID functions used for computing the parity with 'sync':\n");
	printf("%8s", "");
	printf("%8s", "best");
	printf("%8s", "int8");
	printf("%8s", "int32");
	printf("%8s", "int64");
#ifdef CONFIG_X86
	printf("%8s", "sse2");
#ifdef CONFIG_X86_64
	printf("%8s", "sse2e");
#endif
	printf("%8s", "ssse3");
#ifdef CONFIG_X86_64
	printf("%8s", "ssse3e");
#endif
#endif
	printf("\n");

	/* PAR1 */
	printf("%8s", "par1");
	printf("%8s", raid_par1_tag());
	fflush(stdout);

	printf("%8s", "");

	SPEED_START {
		raid_par1_int32(nd, size, v);
	} SPEED_STOP

	printf("%8"PRIu64, ds / dt);
	fflush(stdout);

	SPEED_START {
		raid_par1_int64(nd, size, v);
	} SPEED_STOP

	printf("%8"PRIu64, ds / dt);
	fflush(stdout);

#ifdef CONFIG_X86
	if (raid_cpu_has_sse2()) {
		SPEED_START {
			raid_par1_sse2(nd, size, v);
		} SPEED_STOP

		printf("%8"PRIu64, ds / dt);
		fflush(stdout);
	}
#endif
	printf("\n");

	/* PAR2 */
	printf("%8s", "par2");
	printf("%8s", raid_par2_tag());
	fflush(stdout);

	printf("%8s", "");

	SPEED_START {
		raid_par2_int32(nd, size, v);
	} SPEED_STOP

	printf("%8"PRIu64, ds / dt);
	fflush(stdout);

	SPEED_START {
		raid_par2_int64(nd, size, v);
	} SPEED_STOP

	printf("%8"PRIu64, ds / dt);
	fflush(stdout);

#ifdef CONFIG_X86
	if (raid_cpu_has_sse2()) {
		SPEED_START {
			raid_par2_sse2(nd, size, v);
		} SPEED_STOP

		printf("%8"PRIu64, ds / dt);
		fflush(stdout);

#ifdef CONFIG_X86_64
		SPEED_START {
			raid_par2_sse2ext(nd, size, v);
		} SPEED_STOP

		printf("%8"PRIu64, ds / dt);
		fflush(stdout);
#endif
	}
#endif
	printf("\n");

	/* PARz */
	printf("%8s", "parz");
	printf("%8s", raid_parz_tag());
	fflush(stdout);

	printf("%8s", "");

	SPEED_START {
		raid_parz_int32(nd, size, v);
	} SPEED_STOP

	printf("%8"PRIu64, ds / dt);
	fflush(stdout);

	SPEED_START {
		raid_parz_int64(nd, size, v);
	} SPEED_STOP

	printf("%8"PRIu64, ds / dt);
	fflush(stdout);

#ifdef CONFIG_X86
	if (raid_cpu_has_sse2()) {
		SPEED_START {
			raid_parz_sse2(nd, size, v);
		} SPEED_STOP

		printf("%8"PRIu64, ds / dt);
		fflush(stdout);

#ifdef CONFIG_X86_64
		SPEED_START {
			raid_parz_sse2ext(nd, size, v);
		} SPEED_STOP

		printf("%8"PRIu64, ds / dt);
		fflush(stdout);
#endif
	}
#endif
	printf("\n");

	/* PAR3 */
	printf("%8s", "par3");
	printf("%8s", raid_par3_tag());
	fflush(stdout);

	SPEED_START {
		raid_par3_int8(nd, size, v);
	} SPEED_STOP

	printf("%8"PRIu64, ds / dt);
	fflush(stdout);

	printf("%8s", "");
	printf("%8s", "");

#ifdef CONFIG_X86
	if (raid_cpu_has_sse2()) {
		printf("%8s", "");

#ifdef CONFIG_X86_64
		printf("%8s", "");
#endif
	}
#endif

#ifdef CONFIG_X86
	if (raid_cpu_has_ssse3()) {
		SPEED_START {
			raid_par3_ssse3(nd, size, v);
		} SPEED_STOP

		printf("%8"PRIu64, ds / dt);
		fflush(stdout);

#ifdef CONFIG_X86_64
		SPEED_START {
			raid_par3_ssse3ext(nd, size, v);
		} SPEED_STOP

		printf("%8"PRIu64, ds / dt);
		fflush(stdout);
#endif
	}
#endif
	printf("\n");

	/* PAR4 */
	printf("%8s", "par4");
	printf("%8s", raid_par4_tag());
	fflush(stdout);

	SPEED_START {
		raid_par4_int8(nd, size, v);
	} SPEED_STOP

	printf("%8"PRIu64, ds / dt);
	fflush(stdout);

	printf("%8s", "");
	printf("%8s", "");

#ifdef CONFIG_X86
	if (raid_cpu_has_sse2()) {
		printf("%8s", "");

#ifdef CONFIG_X86_64
		printf("%8s", "");
#endif
	}
#endif

#ifdef CONFIG_X86
	if (raid_cpu_has_ssse3()) {
		SPEED_START {
			raid_par4_ssse3(nd, size, v);
		} SPEED_STOP

		printf("%8"PRIu64, ds / dt);
		fflush(stdout);

#ifdef CONFIG_X86_64
		SPEED_START {
			raid_par4_ssse3ext(nd, size, v);
		} SPEED_STOP

		printf("%8"PRIu64, ds / dt);
		fflush(stdout);
#endif
	}
#endif
	printf("\n");

	/* PAR5 */
	printf("%8s", "par5");
	printf("%8s", raid_par5_tag());
	fflush(stdout);

	SPEED_START {
		raid_par5_int8(nd, size, v);
	} SPEED_STOP

	printf("%8"PRIu64, ds / dt);
	fflush(stdout);

	printf("%8s", "");
	printf("%8s", "");

#ifdef CONFIG_X86
	if (raid_cpu_has_sse2()) {
		printf("%8s", "");

#ifdef CONFIG_X86_64
		printf("%8s", "");
#endif
	}
#endif

#ifdef CONFIG_X86
	if (raid_cpu_has_ssse3()) {
		SPEED_START {
			raid_par5_ssse3(nd, size, v);
		} SPEED_STOP

		printf("%8"PRIu64, ds / dt);
		fflush(stdout);

#ifdef CONFIG_X86_64
		SPEED_START {
			raid_par5_ssse3ext(nd, size, v);
		} SPEED_STOP

		printf("%8"PRIu64, ds / dt);
		fflush(stdout);
#endif
	}
#endif
	printf("\n");

	/* PAR6 */
	printf("%8s", "par6");
	printf("%8s", raid_par6_tag());
	fflush(stdout);

	SPEED_START {
		raid_par6_int8(nd, size, v);
	} SPEED_STOP

	printf("%8"PRIu64, ds / dt);
	fflush(stdout);

	printf("%8s", "");
	printf("%8s", "");

#ifdef CONFIG_X86
	if (raid_cpu_has_sse2()) {
		printf("%8s", "");

#ifdef CONFIG_X86_64
		printf("%8s", "");
#endif
	}
#endif

#ifdef CONFIG_X86
	if (raid_cpu_has_ssse3()) {
		SPEED_START {
			raid_par6_ssse3(nd, size, v);
		} SPEED_STOP

		printf("%8"PRIu64, ds / dt);
		fflush(stdout);

#ifdef CONFIG_X86_64
		SPEED_START {
			raid_par6_ssse3ext(nd, size, v);
		} SPEED_STOP

		printf("%8"PRIu64, ds / dt);
		fflush(stdout);
#endif
	}
#endif
	printf("\n");
	printf("\n");

	/* recover table */
	printf("RAID functions used for recovering with 'fix':\n");
	printf("%8s", "");
	printf("%8s", "best");
	printf("%8s", "int8");
#ifdef CONFIG_X86
	printf("%8s", "ssse3");
#endif
	printf("\n");

	printf("%8s", "rec1");
	printf("%8s", raid_rec1_tag());
	fflush(stdout);

	SPEED_START {
		for (j = 0; j < nd; ++j)
			/* +1 to avoid PAR1 optimized case */
			raid_rec1_int8(1, id, ip + 1, nd, size, v);
	} SPEED_STOP

	printf("%8"PRIu64, ds / dt);
	fflush(stdout);

#ifdef CONFIG_X86
	if (raid_cpu_has_ssse3()) {
		SPEED_START {
			for (j = 0; j < nd; ++j)
				/* +1 to avoid PAR1 optimized case */
				raid_rec1_ssse3(1, id, ip + 1, nd, size, v);
		} SPEED_STOP

		printf("%8"PRIu64, ds / dt);
	}
#endif
	printf("\n");

	printf("%8s", "rec2");
	printf("%8s", raid_rec2_tag());
	fflush(stdout);

	SPEED_START {
		for (j = 0; j < nd; ++j)
			/* +1 to avoid PAR2 optimized case */
			raid_rec2_int8(2, id, ip + 1, nd, size, v);
	} SPEED_STOP

	printf("%8"PRIu64, ds / dt);
	fflush(stdout);

#ifdef CONFIG_X86
	if (raid_cpu_has_ssse3()) {
		SPEED_START {
			for (j = 0; j < nd; ++j)
				/* +1 to avoid PAR2 optimized case */
				raid_rec2_ssse3(2, id, ip + 1, nd, size, v);
		} SPEED_STOP

		printf("%8"PRIu64, ds / dt);
	}
#endif
	printf("\n");

	printf("%8s", "rec3");
	printf("%8s", raid_recX_tag());
	fflush(stdout);

	SPEED_START {
		for (j = 0; j < nd; ++j)
			raid_recX_int8(3, id, ip, nd, size, v);
	} SPEED_STOP

	printf("%8"PRIu64, ds / dt);
	fflush(stdout);

#ifdef CONFIG_X86
	if (raid_cpu_has_ssse3()) {
		SPEED_START {
			for (j = 0; j < nd; ++j)
				raid_recX_ssse3(3, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8"PRIu64, ds / dt);
	}
#endif
	printf("\n");

	printf("%8s", "rec4");
	printf("%8s", raid_recX_tag());
	fflush(stdout);

	SPEED_START {
		for (j = 0; j < nd; ++j)
			raid_recX_int8(4, id, ip, nd, size, v);
	} SPEED_STOP

	printf("%8"PRIu64, ds / dt);
	fflush(stdout);

#ifdef CONFIG_X86
	if (raid_cpu_has_ssse3()) {
		SPEED_START {
			for (j = 0; j < nd; ++j)
				raid_recX_ssse3(4, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8"PRIu64, ds / dt);
	}
#endif
	printf("\n");

	printf("%8s", "rec5");
	printf("%8s", raid_recX_tag());
	fflush(stdout);

	SPEED_START {
		for (j = 0; j < nd; ++j)
			raid_recX_int8(5, id, ip, nd, size, v);
	} SPEED_STOP

	printf("%8"PRIu64, ds / dt);
	fflush(stdout);

#ifdef CONFIG_X86
	if (raid_cpu_has_ssse3()) {
		SPEED_START {
			for (j = 0; j < nd; ++j)
				raid_recX_ssse3(5, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8"PRIu64, ds / dt);
	}
#endif
	printf("\n");

	printf("%8s", "rec6");
	printf("%8s", raid_recX_tag());
	fflush(stdout);

	SPEED_START {
		for (j = 0; j < nd; ++j)
			raid_recX_int8(6, id, ip, nd, size, v);
	} SPEED_STOP

	printf("%8"PRIu64, ds / dt);
	fflush(stdout);

#ifdef CONFIG_X86
	if (raid_cpu_has_ssse3()) {
		SPEED_START {
			for (j = 0; j < nd; ++j)
				raid_recX_ssse3(6, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8"PRIu64, ds / dt);
	}
#endif
	printf("\n");
	printf("\n");

	printf("If the 'best' expectations are wrong, please report it in the SnapRAID forum\n\n");

	free(v_alloc);
	free(v);
}

