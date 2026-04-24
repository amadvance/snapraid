// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2013 Andrea Mazzoleni

/* Speed test for the RAID library */

#include "internal.h"
#include "memory.h"
#include "cpu.h"

#include <sys/time.h>
#include <stdio.h>
#include <inttypes.h>

/*
 * Size of the blocks to test.
 */
#define TEST_SIZE (256 * 1024)

/*
 * Number of data blocks to test.
 */
#define TEST_COUNT (8)

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
 * Test period.
 */
#ifdef COVERAGE
#define TEST_PERIOD 100000LL
#define TEST_DELTA 1
#else
#define TEST_PERIOD 1000000LL
#define TEST_DELTA 10
#endif

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
	} while (diffgettimeofday(&start, &stop) < TEST_PERIOD); \
	ds = size * (int64_t)count * nd; \
	dt = diffgettimeofday(&start, &stop);

void speed(void)
{
	struct timeval start;
	struct timeval stop;
	int64_t ds;
	int64_t dt;
	int i, j;
	int id[RAID_PARITY_MAX];
	int ip[RAID_PARITY_MAX];
	int count;
	int delta = TEST_DELTA;
	int size = TEST_SIZE;
	int nd = TEST_COUNT;
	int nv;
	void *v_alloc;
	void **v;

	nv = nd + RAID_PARITY_MAX + 1;

	v = raid_malloc_vector(nd, nv, size, &v_alloc);

	/* initialize disks with fixed data */
	for (i = 0; i < nd; ++i)
		memset(v[i], i, size);

	/* zero buffer */
	memset(v[nd + RAID_PARITY_MAX], 0, size);
	raid_zero(v[nd + RAID_PARITY_MAX]);

	/* basic disks and parity mapping */
	for (i = 0; i < RAID_PARITY_MAX; ++i) {
		id[i] = i;
		ip[i] = i;
	}

	printf("Speed test using %u data buffers of %u bytes, for a total of %u KiB.\n", nd, size, nd * size / 1024);
	printf("Memory blocks have a displacement of %u bytes to improve cache performance.\n", RAID_MALLOC_DISPLACEMENT);
	printf("The reported values are the aggregate bandwidth of all data blocks in MiB/s,\n");
	printf("not counting parity blocks.\n");
	printf("\n");

	printf("Memory write speed using the C memset() function:\n");
	printf("%8s", "memset");
	fflush(stdout);

	SPEED_START {
		for (j = 0; j < nd; ++j)
			memset(v[j], j, size);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	printf("\n");
	printf("\n");

	/* RAID table */
	printf("RAID functions used for computing the parity:\n");
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
	printf("%8s", "avx2");
#ifdef CONFIG_X86_64
	printf("%8s", "avx2e");
	printf("%8s", "avx512");
#endif
#endif
	printf("\n");

	/* GEN1 */
	printf("%8s", "gen1");
	printf("%8s", raid_gen_tag(RAID_ALGO_CAUCHY_PAR1));
	fflush(stdout);

	printf("%8s", "");

	SPEED_START {
		raid_gen1_int32(nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);

	SPEED_START {
		raid_gen1_int64(nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);

#ifdef CONFIG_X86
#ifdef CONFIG_SSE2
	if (raid_cpu_has_sse2()) {
		SPEED_START {
			raid_gen1_sse2(nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#endif

#ifdef CONFIG_X86_64
	printf("%8s", "");
#endif
	printf("%8s", "");
#ifdef CONFIG_X86_64
	printf("%8s", "");
#endif
#ifdef CONFIG_AVX2
	if (raid_cpu_has_avx2()) {
		SPEED_START {
			raid_gen1_avx2(nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#endif
#ifdef CONFIG_AVX512BW
	if (raid_cpu_has_avx512bw()) {
		printf("%8s", "");

		SPEED_START {
			raid_gen1_avx512bw(nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#endif
#endif
	printf("\n");

	/* GEN2 */
	printf("%8s", "gen2");
	printf("%8s", raid_gen_tag(RAID_ALGO_CAUCHY_PAR2));
	fflush(stdout);

	printf("%8s", "");

	SPEED_START {
		raid_gen2_int32(nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);

	SPEED_START {
		raid_gen2_int64(nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);

#ifdef CONFIG_X86
#ifdef CONFIG_SSE2
	if (raid_cpu_has_sse2()) {
		SPEED_START {
			raid_gen2_sse2(nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);

#ifdef CONFIG_X86_64
		SPEED_START {
			raid_gen2_sse2ext(nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
#endif
	}
#endif

	printf("%8s", "");
#ifdef CONFIG_X86_64
	printf("%8s", "");
#endif
#ifdef CONFIG_AVX2
	if (raid_cpu_has_avx2()) {
		SPEED_START {
			raid_gen2_avx2(nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#endif
#ifdef CONFIG_AVX512BW
	if (raid_cpu_has_avx512bw()) {
		printf("%8s", "");

		SPEED_START {
			raid_gen2_avx512bw(nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#endif
#endif
	printf("\n");

	/* GENz */
	printf("%8s", "genz");
	printf("%8s", raid_gen_tag(RAID_ALGO_VANDERMONDE_PAR3));
	fflush(stdout);

	printf("%8s", "");

	SPEED_START {
		raid_genz_int32(nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);

	SPEED_START {
		raid_genz_int64(nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);

#ifdef CONFIG_X86
#ifdef CONFIG_SSE2
	if (raid_cpu_has_sse2()) {
		SPEED_START {
			raid_genz_sse2(nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);

#ifdef CONFIG_X86_64
		SPEED_START {
			raid_genz_sse2ext(nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
#endif
	}
#endif

	printf("%8s", "");
#ifdef CONFIG_X86_64
	printf("%8s", "");
#endif
	printf("%8s", "");

#ifdef CONFIG_X86_64
#ifdef CONFIG_AVX2
	if (raid_cpu_has_avx2()) {
		SPEED_START {
			raid_genz_avx2ext(nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#endif
#endif
#endif
	printf("\n");

	/* GEN3 */
	printf("%8s", "gen3");
	printf("%8s", raid_gen_tag(RAID_ALGO_CAUCHY_PAR3));
	fflush(stdout);

	SPEED_START {
		raid_gen3_int8(nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
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
#ifdef CONFIG_SSSE3
	if (raid_cpu_has_ssse3()) {
		SPEED_START {
			raid_gen3_ssse3(nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);

#ifdef CONFIG_X86_64
		SPEED_START {
			raid_gen3_ssse3ext(nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
#endif
	}
#endif

	printf("%8s", "");

#ifdef CONFIG_X86_64
#ifdef CONFIG_AVX2
	if (raid_cpu_has_avx2()) {
		SPEED_START {
			raid_gen3_avx2ext(nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#endif
#ifdef CONFIG_AVX512BW
	if (raid_cpu_has_avx512bw()) {
		SPEED_START {
			raid_gen3_avx512bw(nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#endif
#endif
#endif
	printf("\n");

	/* GEN4 */
	printf("%8s", "gen4");
	printf("%8s", raid_gen_tag(RAID_ALGO_CAUCHY_PAR4));
	fflush(stdout);

	SPEED_START {
		raid_gen4_int8(nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);

	printf("%8s", "");
	printf("%8s", "");

#ifdef CONFIG_X86
#ifdef CONFIG_SSE2
	if (raid_cpu_has_sse2()) {
		printf("%8s", "");

#ifdef CONFIG_X86_64
		printf("%8s", "");
#endif
	}
#endif
#endif

#ifdef CONFIG_X86
#ifdef CONFIG_SSSE3
	if (raid_cpu_has_ssse3()) {
		SPEED_START {
			raid_gen4_ssse3(nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);

#ifdef CONFIG_X86_64
		SPEED_START {
			raid_gen4_ssse3ext(nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
#endif
	}
#endif

	printf("%8s", "");

#ifdef CONFIG_X86_64
#ifdef CONFIG_AVX2
	if (raid_cpu_has_avx2()) {
		SPEED_START {
			raid_gen4_avx2ext(nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#endif
#ifdef CONFIG_AVX512BW
	if (raid_cpu_has_avx512bw()) {
		SPEED_START {
			raid_gen4_avx512bw(nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#endif
#endif
#endif
	printf("\n");

	/* GEN5 */
	printf("%8s", "gen5");
	printf("%8s", raid_gen_tag(RAID_ALGO_CAUCHY_PAR5));
	fflush(stdout);

	SPEED_START {
		raid_gen5_int8(nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);

	printf("%8s", "");
	printf("%8s", "");

#ifdef CONFIG_X86
#ifdef CONFIG_SSE2
	if (raid_cpu_has_sse2()) {
		printf("%8s", "");

#ifdef CONFIG_X86_64
		printf("%8s", "");
#endif
	}
#endif
#endif

#ifdef CONFIG_X86
#ifdef CONFIG_SSSE3
	if (raid_cpu_has_ssse3()) {
		SPEED_START {
			raid_gen5_ssse3(nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);

#ifdef CONFIG_X86_64
		SPEED_START {
			raid_gen5_ssse3ext(nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
#endif
	}
#endif

	printf("%8s", "");

#ifdef CONFIG_X86_64
#ifdef CONFIG_AVX2
	if (raid_cpu_has_avx2()) {
		SPEED_START {
			raid_gen5_avx2ext(nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#endif
#ifdef CONFIG_AVX512BW
	if (raid_cpu_has_avx512bw()) {
		SPEED_START {
			raid_gen5_avx512bw(nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#endif
#endif
#endif
	printf("\n");

	/* GEN6 */
	printf("%8s", "gen6");
	printf("%8s", raid_gen_tag(RAID_ALGO_CAUCHY_PAR6));
	fflush(stdout);

	SPEED_START {
		raid_gen6_int8(nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);

	printf("%8s", "");
	printf("%8s", "");

#ifdef CONFIG_X86
#ifdef CONFIG_SSE2
	if (raid_cpu_has_sse2()) {
		printf("%8s", "");

#ifdef CONFIG_X86_64
		printf("%8s", "");
#endif
	}
#endif
#endif

#ifdef CONFIG_X86
#ifdef CONFIG_SSSE3
	if (raid_cpu_has_ssse3()) {
		SPEED_START {
			raid_gen6_ssse3(nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);

#ifdef CONFIG_X86_64
		SPEED_START {
			raid_gen6_ssse3ext(nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
#endif
	}
#endif

	printf("%8s", "");

#ifdef CONFIG_X86_64
#ifdef CONFIG_AVX2
	if (raid_cpu_has_avx2()) {
		SPEED_START {
			raid_gen6_avx2ext(nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#endif
#ifdef CONFIG_AVX512BW
	if (raid_cpu_has_avx512bw()) {
		SPEED_START {
			raid_gen6_avx512bw(nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#endif
#endif
#endif
	printf("\n");
	printf("\n");

	/* recover table */
	printf("RAID functions used for recovering:\n");
	printf("%8s", "");
	printf("%8s", "best");
	printf("%8s", "int8");
#ifdef CONFIG_X86
	printf("%8s", "ssse3");
	printf("%8s", "avx2");
#ifdef CONFIG_X86_64
	printf("%8s", "avx512");
#endif
#endif
	printf("\n");

	printf("%8s", "rec1");
	printf("%8s", raid_rec_tag(RAID_ALGO_CAUCHY_PAR1));
	fflush(stdout);

	SPEED_START {
		raid_gen_force(1, sizeof(void *) == 8 ? raid_gen1_int64 : raid_gen1_int32);
		/* +1 to avoid GEN1 optimized case */
		raid_rec1_int8(1, id, ip + 1, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);

#ifdef CONFIG_X86
#ifdef CONFIG_SSSE3
	if (raid_cpu_has_ssse3()) {
		SPEED_START {
			raid_gen_force(1, raid_gen1_sse2);
			/* +1 to avoid GEN1 optimized case */
			raid_rec1_ssse3(1, id, ip + 1, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#endif
#ifdef CONFIG_AVX2
	if (raid_cpu_has_avx2()) {
		SPEED_START {
			raid_gen_force(1, raid_gen1_avx2);
			/* +1 to avoid GEN1 optimized case */
			raid_rec1_avx2(1, id, ip + 1, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#endif
#endif
#ifdef CONFIG_X86_64
#ifdef CONFIG_AVX512BW
	if (raid_cpu_has_avx512bw()) {
		SPEED_START {
			raid_gen_force(1, raid_gen1_avx512bw);
			/* +1 to avoid GEN1 optimized case */
			raid_rec1_avx512bw(1, id, ip + 1, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#endif
#endif
	printf("\n");

	printf("%8s", "rec2");
	printf("%8s", raid_rec_tag(RAID_ALGO_CAUCHY_PAR2));
	fflush(stdout);

	SPEED_START {
		raid_gen_force(2, sizeof(void *) == 8 ? raid_gen2_int64 : raid_gen2_int32);
		/* +1 to avoid GEN2 optimized case */
		raid_rec2_int8(2, id, ip + 1, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);

#ifdef CONFIG_X86
#ifdef CONFIG_SSSE3
	if (raid_cpu_has_ssse3()) {
		SPEED_START {
#ifdef CONFIG_X86_64
			raid_gen_force(2, raid_gen2_sse2ext);
#else
			raid_gen_force(2, raid_gen2_sse2);
#endif
			/* +1 to avoid GEN2 optimized case */
			raid_rec2_ssse3(2, id, ip + 1, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#endif
#ifdef CONFIG_AVX2
	if (raid_cpu_has_avx2()) {
		SPEED_START {
			raid_gen_force(2, raid_gen2_avx2);
			/* +1 to avoid GEN2 optimized case */
			raid_rec2_avx2(2, id, ip + 1, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#endif
#endif
#ifdef CONFIG_X86_64
#ifdef CONFIG_AVX512BW
	if (raid_cpu_has_avx512bw()) {
		SPEED_START {
			raid_gen_force(2, raid_gen2_avx512bw);
			/* +1 to avoid GEN2 optimized case */
			raid_rec2_avx512bw(2, id, ip + 1, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#endif
#endif
	printf("\n");

	printf("%8s", "rec3");
	printf("%8s", raid_rec_tag(RAID_ALGO_CAUCHY_PAR3));
	fflush(stdout);

	SPEED_START {
		raid_gen_force(3, raid_gen3_int8);
		raid_recX_int8(3, id, ip, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);

#ifdef CONFIG_X86
#ifdef CONFIG_SSSE3
	if (raid_cpu_has_ssse3()) {
		SPEED_START {
#ifdef CONFIG_X86_64
			raid_gen_force(3, raid_gen3_ssse3ext);
#else
			raid_gen_force(3, raid_gen3_ssse3);
#endif
			raid_recX_ssse3(3, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#endif
#ifdef CONFIG_AVX2
	if (raid_cpu_has_avx2()) {
		SPEED_START {
			raid_gen_force(3, raid_gen3_avx2ext);
			raid_recX_avx2(3, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#endif
#endif
#ifdef CONFIG_X86_64
#ifdef CONFIG_AVX2
	if (raid_cpu_has_avx512bw()) {
		SPEED_START {
			raid_gen_force(3, raid_gen3_avx512bw);
			raid_recX_avx512bw(3, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#endif
#endif
	printf("\n");

	printf("%8s", "rec4");
	printf("%8s", raid_rec_tag(RAID_ALGO_CAUCHY_PAR4));
	fflush(stdout);

	SPEED_START {
		raid_gen_force(4, raid_gen4_int8);
		raid_recX_int8(4, id, ip, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);

#ifdef CONFIG_X86
#ifdef CONFIG_SSSE3
	if (raid_cpu_has_ssse3()) {
		SPEED_START {
#ifdef CONFIG_X86_64
			raid_gen_force(4, raid_gen4_ssse3ext);
#else
			raid_gen_force(4, raid_gen4_ssse3);
#endif
			raid_recX_ssse3(4, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#endif
#ifdef CONFIG_AVX2
	if (raid_cpu_has_avx2()) {
		SPEED_START {
#ifdef CONFIG_X86_64
			raid_gen_force(4, raid_gen4_avx2ext);
#else
			raid_gen_force(4, raid_gen4_ssse3);
#endif
			raid_recX_avx2(4, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#endif
#endif
#ifdef CONFIG_X86_64
#ifdef CONFIG_AVX2
	if (raid_cpu_has_avx512bw()) {
		SPEED_START {
			raid_gen_force(4, raid_gen4_avx512bw);
			raid_recX_avx512bw(4, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#endif
#endif
	printf("\n");

	printf("%8s", "rec5");
	printf("%8s", raid_rec_tag(RAID_ALGO_CAUCHY_PAR5));
	fflush(stdout);

	SPEED_START {
		raid_gen_force(5, raid_gen5_int8);
		raid_recX_int8(5, id, ip, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);

#ifdef CONFIG_X86
#ifdef CONFIG_SSSE3
	if (raid_cpu_has_ssse3()) {
		SPEED_START {
#ifdef CONFIG_X86_64
			raid_gen_force(5, raid_gen5_ssse3ext);
#else
			raid_gen_force(5, raid_gen5_ssse3);
#endif
			raid_recX_ssse3(5, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#endif
#ifdef CONFIG_AVX2
	if (raid_cpu_has_avx2()) {
		SPEED_START {
#ifdef CONFIG_X86_64
			raid_gen_force(5, raid_gen5_avx2ext);
#else
			raid_gen_force(5, raid_gen5_ssse3);
#endif
			raid_recX_avx2(5, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#endif
#endif
#ifdef CONFIG_X86_64
#ifdef CONFIG_AVX2
	if (raid_cpu_has_avx512bw()) {
		SPEED_START {
			raid_gen_force(5, raid_gen5_avx512bw);
			raid_recX_avx512bw(5, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#endif
#endif
	printf("\n");

	printf("%8s", "rec6");
	printf("%8s", raid_rec_tag(RAID_ALGO_CAUCHY_PAR6));
	fflush(stdout);

	SPEED_START {
		raid_gen_force(6, raid_gen6_int8);
		raid_recX_int8(6, id, ip, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);

#ifdef CONFIG_X86
#ifdef CONFIG_SSSE3
	if (raid_cpu_has_ssse3()) {
		SPEED_START {
#ifdef CONFIG_X86_64
			raid_gen_force(6, raid_gen6_ssse3ext);
#else
			raid_gen_force(6, raid_gen6_ssse3);
#endif
			raid_recX_ssse3(6, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#endif
#ifdef CONFIG_AVX2
	if (raid_cpu_has_avx2()) {
		SPEED_START {
#ifdef CONFIG_X86_64
			raid_gen_force(6, raid_gen6_avx2ext);
#else
			raid_gen_force(6, raid_gen6_ssse3);
#endif
			raid_recX_avx2(6, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#endif
#endif
#ifdef CONFIG_X86_64
#ifdef CONFIG_AVX2
	if (raid_cpu_has_avx512bw()) {
		SPEED_START {
			raid_gen_force(6, raid_gen6_avx512bw);
			raid_recX_avx512bw(6, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#endif
#endif
	printf("\n");
	printf("\n");

	free(v_alloc);
	free(v);
}

int main(void)
{
	printf("Speed test for the RAID Cauchy library\n\n");

	raid_init();

#ifdef CONFIG_X86
	if (raid_cpu_has_sse2())
		printf("Including x86 SSE2 functions\n");
	if (raid_cpu_has_ssse3())
		printf("Including x86 SSSE3 functions\n");
	if (raid_cpu_has_avx2())
		printf("Including x86 AVX2 functions\n");
	if (raid_cpu_has_avx512bw())
		printf("Including x86 AVX512bw functions\n");
	if (raid_cpu_has_avx512gfni())
		printf("Including x86 AVX512gfni functions\n");
#endif
#ifdef CONFIG_X86_64
	printf("Including x64 extended SSE register set\n");
#endif

	printf("\nPlease wait about 30 seconds...\n\n");

	speed();

	return 0;
}

