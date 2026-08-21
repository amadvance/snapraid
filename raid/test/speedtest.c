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
	} while (diffgettimeofday(&start, &stop) < period); \
	ds = size * (int64_t)count * nd; \
	dt = diffgettimeofday(&start, &stop);

void speed_mem(int nd, void **v, int size, int delta, int period)
{
	struct timeval start;
	struct timeval stop;
	int64_t ds;
	int64_t dt;
	int i, j;
	int count;

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
}

void speed_gen(int nd, void **v, int size, int delta, int period, const char *msg)
{
	struct timeval start;
	struct timeval stop;
	int64_t ds;
	int64_t dt;
	int i;
	int count;
	int mode = raid_mode(RAID_MODE_GET);

	/* RAID table */
	printf("%s functions used for computing the parity:\n", msg);
	printf("%8s", "");
	printf("%8s", "best");
	printf("%8s", "int8");
	printf("%8s", "int32");
	printf("%8s", "int64");
#ifdef CONFIG_NEON
	printf("%8s", "neon");
#endif
#ifdef CONFIG_NEON32
	printf("%8s", "neon32");
#endif
#ifdef CONFIG_X86
	if (raid_cpu_has_sse2()) {
		printf("%8s", "sse2");
#ifdef CONFIG_X86_64
		printf("%8s", "sse2e");
#endif
	}
	if (raid_cpu_has_ssse3()) {
		printf("%8s", "ssse3");
#ifdef CONFIG_X86_64
		printf("%8s", "ssse3e");
#endif
	}
	if (raid_cpu_has_avx2()) {
		printf("%8s", "avx2");
#ifdef CONFIG_X86_64
		printf("%8s", "avx2e");
#endif
	}
#ifdef CONFIG_X86_64
	if (raid_cpu_has_avx512bw()) {
		printf("%8s", "avx512");
	}
	if (raid_cpu_has_avx2gfni()) {
		printf("%8s", "gfni");
	}
	if (raid_cpu_has_avx512gfni()) {
		printf("%8s", "gfni512");
	}
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

#ifdef CONFIG_NEON
	SPEED_START {
		raid_gen1_neon(nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif
#ifdef CONFIG_NEON32
	SPEED_START {
		raid_gen1_neon32(nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif

#ifdef CONFIG_X86
	if (raid_cpu_has_sse2()) {
		SPEED_START {
			raid_gen1_sse2(nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
#ifdef CONFIG_X86_64
		printf("%8s", "");
#endif
	}
	if (raid_cpu_has_ssse3()) {
		printf("%8s", "");
#ifdef CONFIG_X86_64
		printf("%8s", "");
#endif
	}
	if (raid_cpu_has_avx2()) {
		SPEED_START {
			raid_gen1_avx2(nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
#ifdef CONFIG_X86_64
		printf("%8s", "");
#endif
	}
#ifdef CONFIG_X86_64
	if (raid_cpu_has_avx512bw()) {
		SPEED_START {
			raid_gen1_avx512bw(nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx2gfni())
		printf("%8s", "");
	if (raid_cpu_has_avx512gfni())
		printf("%8s", "");
#endif
#endif
	printf("\n");

	/* GEN2 */
	printf("%8s", "gen2");
	printf("%8s", raid_gen_tag(RAID_ALGO_CAUCHY_PAR2));
	fflush(stdout);

	printf("%8s", "");

	if (mode == RAID_MODE_CAUCHY_AES) {
		SPEED_START {
			raid_gen2_int32_aes(nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);

		SPEED_START {
			raid_gen2_int64_aes(nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	} else {
		SPEED_START {
			raid_gen2_int32_raid(nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);

		SPEED_START {
			raid_gen2_int64_raid(nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}

#ifdef CONFIG_NEON
	if (mode == RAID_MODE_CAUCHY_AES) {
		SPEED_START {
			raid_gen2_neon_aes(nd, size, v);
		} SPEED_STOP
	} else {
		SPEED_START {
			raid_gen2_neon_raid(nd, size, v);
		} SPEED_STOP
	}

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif
#ifdef CONFIG_NEON32
	if (mode == RAID_MODE_CAUCHY_AES) {
		SPEED_START {
			raid_gen2_neon32_aes(nd, size, v);
		} SPEED_STOP
	} else {
		SPEED_START {
			raid_gen2_neon32_raid(nd, size, v);
		} SPEED_STOP
	}

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif

#ifdef CONFIG_X86
	if (raid_cpu_has_sse2()) {
		if (mode == RAID_MODE_CAUCHY_AES) {
			SPEED_START {
				raid_gen2_sse2_aes(nd, size, v);
			} SPEED_STOP
		} else {
			SPEED_START {
				raid_gen2_sse2_raid(nd, size, v);
			} SPEED_STOP
		}

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);

#ifdef CONFIG_X86_64
		if (mode == RAID_MODE_CAUCHY_AES) {
			SPEED_START {
				raid_gen2_sse2ext_aes(nd, size, v);
			} SPEED_STOP
		} else {
			SPEED_START {
				raid_gen2_sse2ext_raid(nd, size, v);
			} SPEED_STOP
		}

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
#endif
	}
	if (raid_cpu_has_ssse3()) {
		printf("%8s", "");
#ifdef CONFIG_X86_64
		printf("%8s", "");
#endif
	}
	if (raid_cpu_has_avx2()) {
		if (mode == RAID_MODE_CAUCHY_AES) {
			SPEED_START {
				raid_gen2_avx2_aes(nd, size, v);
			} SPEED_STOP
		} else {
			SPEED_START {
				raid_gen2_avx2_raid(nd, size, v);
			} SPEED_STOP
		}

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);

#ifdef CONFIG_X86_64
		if (mode == RAID_MODE_CAUCHY_AES) {
			SPEED_START {
				raid_gen2_avx2ext_aes(nd, size, v);
			} SPEED_STOP
		} else {
			SPEED_START {
				raid_gen2_avx2ext_raid(nd, size, v);
			} SPEED_STOP
		}

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
#endif
	}
#ifdef CONFIG_X86_64
	if (raid_cpu_has_avx512bw()) {
		SPEED_START {
			raid_gen2_avx512bw(nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx2gfni()) {
		if (mode == RAID_MODE_CAUCHY_AES) {
			SPEED_START {
				raid_gen2_avx2gfni_aes(nd, size, v);
			} SPEED_STOP

			printf("%8" PRIu64, ds / dt);
		} else {
			SPEED_START {
				raid_gen2_avx2gfni_raid(nd, size, v);
			} SPEED_STOP

			printf("%8" PRIu64, ds / dt);
		}
		fflush(stdout);
	}
	if (raid_cpu_has_avx512gfni()) {
		if (mode == RAID_MODE_CAUCHY_AES) {
			SPEED_START {
				raid_gen2_avx512gfni_aes(nd, size, v);
			} SPEED_STOP

			printf("%8" PRIu64, ds / dt);
		} else {
			SPEED_START {
				raid_gen2_avx512gfni_raid(nd, size, v);
			} SPEED_STOP

			printf("%8" PRIu64, ds / dt);
		}
		fflush(stdout);
	}
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

#ifdef CONFIG_NEON
	if (mode == RAID_MODE_CAUCHY_AES) {
		SPEED_START {
			raid_gen3_neon_aes(nd, size, v);
		} SPEED_STOP
	} else {
		SPEED_START {
			raid_gen3_neon_raid(nd, size, v);
		} SPEED_STOP
	}

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif
#ifdef CONFIG_NEON32
	if (mode == RAID_MODE_CAUCHY_AES) {
		SPEED_START {
			raid_gen3_neon32_aes(nd, size, v);
		} SPEED_STOP
	} else {
		SPEED_START {
			raid_gen3_neon32_raid(nd, size, v);
		} SPEED_STOP
	}

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif

#ifdef CONFIG_X86
	if (raid_cpu_has_sse2()) {
		printf("%8s", "");
#ifdef CONFIG_X86_64
		printf("%8s", "");
#endif
	}
	if (raid_cpu_has_ssse3()) {
		if (mode == RAID_MODE_CAUCHY_AES) {
			SPEED_START {
				raid_gen3_ssse3_aes(nd, size, v);
			} SPEED_STOP
		} else {
			SPEED_START {
				raid_gen3_ssse3_raid(nd, size, v);
			} SPEED_STOP
		}

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
#ifdef CONFIG_X86_64
		if (mode == RAID_MODE_CAUCHY_AES) {
			SPEED_START {
				raid_gen3_ssse3ext_aes(nd, size, v);
			} SPEED_STOP
		} else {
			SPEED_START {
				raid_gen3_ssse3ext_raid(nd, size, v);
			} SPEED_STOP
		}

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
#endif
	}
	if (raid_cpu_has_avx2()) {
		printf("%8s", "");
#ifdef CONFIG_X86_64
		if (mode == RAID_MODE_CAUCHY_AES) {
			SPEED_START {
				raid_gen3_avx2ext_aes(nd, size, v);
			} SPEED_STOP
		} else {
			SPEED_START {
				raid_gen3_avx2ext_raid(nd, size, v);
			} SPEED_STOP
		}

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
#endif
	}
#ifdef CONFIG_X86_64
	if (raid_cpu_has_avx512bw()) {
		SPEED_START {
			raid_gen3_avx512bw(nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx2gfni()) {
		if (mode == RAID_MODE_CAUCHY_AES) {
			SPEED_START {
				raid_gen3_avx2gfni_aes(nd, size, v);
			} SPEED_STOP

			printf("%8" PRIu64, ds / dt);
		} else {
			SPEED_START {
				raid_gen3_avx2gfni_raid(nd, size, v);
			} SPEED_STOP

			printf("%8" PRIu64, ds / dt);
		}
		fflush(stdout);
	}
	if (raid_cpu_has_avx512gfni()) {
		if (mode == RAID_MODE_CAUCHY_AES) {
			SPEED_START {
				raid_gen3_avx512gfni_aes(nd, size, v);
			} SPEED_STOP

			printf("%8" PRIu64, ds / dt);
		} else {
			SPEED_START {
				raid_gen3_avx512gfni_raid(nd, size, v);
			} SPEED_STOP

			printf("%8" PRIu64, ds / dt);
		}
		fflush(stdout);
	}
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

#ifdef CONFIG_NEON
	if (mode == RAID_MODE_CAUCHY_AES) {
		SPEED_START {
			raid_gen4_neon_aes(nd, size, v);
		} SPEED_STOP
	} else {
		SPEED_START {
			raid_gen4_neon_raid(nd, size, v);
		} SPEED_STOP
	}

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif
#ifdef CONFIG_NEON32
	if (mode == RAID_MODE_CAUCHY_AES) {
		SPEED_START {
			raid_gen4_neon32_aes(nd, size, v);
		} SPEED_STOP
	} else {
		SPEED_START {
			raid_gen4_neon32_raid(nd, size, v);
		} SPEED_STOP
	}

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif

#ifdef CONFIG_X86
	if (raid_cpu_has_sse2()) {
		printf("%8s", "");
#ifdef CONFIG_X86_64
		printf("%8s", "");
#endif
	}
	if (raid_cpu_has_ssse3()) {
		if (mode == RAID_MODE_CAUCHY_AES) {
			SPEED_START {
				raid_gen4_ssse3_aes(nd, size, v);
			} SPEED_STOP
		} else {
			SPEED_START {
				raid_gen4_ssse3_raid(nd, size, v);
			} SPEED_STOP
		}

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
#ifdef CONFIG_X86_64
		if (mode == RAID_MODE_CAUCHY_AES) {
			SPEED_START {
				raid_gen4_ssse3ext_aes(nd, size, v);
			} SPEED_STOP
		} else {
			SPEED_START {
				raid_gen4_ssse3ext_raid(nd, size, v);
			} SPEED_STOP
		}

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
#endif
	}
	if (raid_cpu_has_avx2()) {
		printf("%8s", "");
#ifdef CONFIG_X86_64
		if (mode == RAID_MODE_CAUCHY_AES) {
			SPEED_START {
				raid_gen4_avx2ext_aes(nd, size, v);
			} SPEED_STOP
		} else {
			SPEED_START {
				raid_gen4_avx2ext_raid(nd, size, v);
			} SPEED_STOP
		}

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
#endif
	}
#ifdef CONFIG_X86_64
	if (raid_cpu_has_avx512bw()) {
		SPEED_START {
			raid_gen4_avx512bw(nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx2gfni()) {
		if (mode == RAID_MODE_CAUCHY_AES) {
			SPEED_START {
				raid_gen4_avx2gfni_aes(nd, size, v);
			} SPEED_STOP

			printf("%8" PRIu64, ds / dt);
		} else {
			SPEED_START {
				raid_gen4_avx2gfni_raid(nd, size, v);
			} SPEED_STOP

			printf("%8" PRIu64, ds / dt);
		}
		fflush(stdout);
	}
	if (raid_cpu_has_avx512gfni()) {
		if (mode == RAID_MODE_CAUCHY_AES) {
			SPEED_START {
				raid_gen4_avx512gfni_aes(nd, size, v);
			} SPEED_STOP

			printf("%8" PRIu64, ds / dt);
		} else {
			SPEED_START {
				raid_gen4_avx512gfni_raid(nd, size, v);
			} SPEED_STOP

			printf("%8" PRIu64, ds / dt);
		}
		fflush(stdout);
	}
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

#ifdef CONFIG_NEON
	if (mode == RAID_MODE_CAUCHY_AES) {
		SPEED_START {
			raid_gen5_neon_aes(nd, size, v);
		} SPEED_STOP
	} else {
		SPEED_START {
			raid_gen5_neon_raid(nd, size, v);
		} SPEED_STOP
	}

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif
#ifdef CONFIG_NEON32
	if (mode == RAID_MODE_CAUCHY_AES) {
		SPEED_START {
			raid_gen5_neon32_aes(nd, size, v);
		} SPEED_STOP
	} else {
		SPEED_START {
			raid_gen5_neon32_raid(nd, size, v);
		} SPEED_STOP
	}

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif

#ifdef CONFIG_X86
	if (raid_cpu_has_sse2()) {
		printf("%8s", "");
#ifdef CONFIG_X86_64
		printf("%8s", "");
#endif
	}
	if (raid_cpu_has_ssse3()) {
		if (mode == RAID_MODE_CAUCHY_RAID) {
			SPEED_START {
				raid_gen5_ssse3_raid(nd, size, v);
			} SPEED_STOP

			printf("%8" PRIu64, ds / dt);
		} else {
			printf("%8s", "");
		}
		fflush(stdout);
#ifdef CONFIG_X86_64
		if (mode == RAID_MODE_CAUCHY_AES) {
			SPEED_START {
				raid_gen5_ssse3ext_aes(nd, size, v);
			} SPEED_STOP
		} else {
			SPEED_START {
				raid_gen5_ssse3ext_raid(nd, size, v);
			} SPEED_STOP
		}

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
#endif
	}
	if (raid_cpu_has_avx2()) {
		printf("%8s", "");
#ifdef CONFIG_X86_64
		if (mode == RAID_MODE_CAUCHY_AES) {
			SPEED_START {
				raid_gen5_avx2ext_aes(nd, size, v);
			} SPEED_STOP
		} else {
			SPEED_START {
				raid_gen5_avx2ext_raid(nd, size, v);
			} SPEED_STOP
		}

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
#endif
	}
#ifdef CONFIG_X86_64
	if (raid_cpu_has_avx512bw()) {
		SPEED_START {
			raid_gen5_avx512bw(nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx2gfni()) {
		if (mode == RAID_MODE_CAUCHY_AES) {
			SPEED_START {
				raid_gen5_avx2gfni_aes(nd, size, v);
			} SPEED_STOP

			printf("%8" PRIu64, ds / dt);
		} else {
			SPEED_START {
				raid_gen5_avx2gfni_raid(nd, size, v);
			} SPEED_STOP

			printf("%8" PRIu64, ds / dt);
		}
		fflush(stdout);
	}
	if (raid_cpu_has_avx512gfni()) {
		if (mode == RAID_MODE_CAUCHY_AES) {
			SPEED_START {
				raid_gen5_avx512gfni_aes(nd, size, v);
			} SPEED_STOP

			printf("%8" PRIu64, ds / dt);
		} else {
			SPEED_START {
				raid_gen5_avx512gfni_raid(nd, size, v);
			} SPEED_STOP

			printf("%8" PRIu64, ds / dt);
		}
		fflush(stdout);
	}
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

#ifdef CONFIG_NEON
	if (mode == RAID_MODE_CAUCHY_AES) {
		SPEED_START {
			raid_gen6_neon_aes(nd, size, v);
		} SPEED_STOP
	} else {
		SPEED_START {
			raid_gen6_neon_raid(nd, size, v);
		} SPEED_STOP
	}

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif
#ifdef CONFIG_NEON32
	if (mode == RAID_MODE_CAUCHY_AES) {
		SPEED_START {
			raid_gen6_neon32_aes(nd, size, v);
		} SPEED_STOP
	} else {
		SPEED_START {
			raid_gen6_neon32_raid(nd, size, v);
		} SPEED_STOP
	}

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif

#ifdef CONFIG_X86
	if (raid_cpu_has_sse2()) {
		printf("%8s", "");
#ifdef CONFIG_X86_64
		printf("%8s", "");
#endif
	}
	if (raid_cpu_has_ssse3()) {
		if (mode == RAID_MODE_CAUCHY_RAID) {
			SPEED_START {
				raid_gen6_ssse3_raid(nd, size, v);
			} SPEED_STOP

			printf("%8" PRIu64, ds / dt);
		} else {
			printf("%8s", "");
		}
		fflush(stdout);
#ifdef CONFIG_X86_64
		if (mode == RAID_MODE_CAUCHY_AES) {
			SPEED_START {
				raid_gen6_ssse3ext_aes(nd, size, v);
			} SPEED_STOP
		} else {
			SPEED_START {
				raid_gen6_ssse3ext_raid(nd, size, v);
			} SPEED_STOP
		}

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
#endif
	}
	if (raid_cpu_has_avx2()) {
		printf("%8s", "");
#ifdef CONFIG_X86_64
		if (mode == RAID_MODE_CAUCHY_AES) {
			SPEED_START {
				raid_gen6_avx2ext_aes(nd, size, v);
			} SPEED_STOP
		} else {
			SPEED_START {
				raid_gen6_avx2ext_raid(nd, size, v);
			} SPEED_STOP
		}

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
#endif
	}
#ifdef CONFIG_X86_64
	if (raid_cpu_has_avx512bw()) {
		SPEED_START {
			raid_gen6_avx512bw(nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx2gfni()) {
		if (mode == RAID_MODE_CAUCHY_AES) {
			SPEED_START {
				raid_gen6_avx2gfni_aes(nd, size, v);
			} SPEED_STOP

			printf("%8" PRIu64, ds / dt);
		} else {
			SPEED_START {
				raid_gen6_avx2gfni_raid(nd, size, v);
			} SPEED_STOP

			printf("%8" PRIu64, ds / dt);
		}
		fflush(stdout);
	}
	if (raid_cpu_has_avx512gfni()) {
		if (mode == RAID_MODE_CAUCHY_AES) {
			SPEED_START {
				raid_gen6_avx512gfni_aes(nd, size, v);
			} SPEED_STOP

			printf("%8" PRIu64, ds / dt);
		} else {
			SPEED_START {
				raid_gen6_avx512gfni_raid(nd, size, v);
			} SPEED_STOP

			printf("%8" PRIu64, ds / dt);
		}
		fflush(stdout);
	}
#endif
#endif
	printf("\n");
	printf("\n");
}

void speed_genz(int nd, void **v, int size, int delta, int period)
{
	struct timeval start;
	struct timeval stop;
	int64_t ds;
	int64_t dt;
	int i;
	int count;

	/* Vandermonde table */
	printf("Vandermonde functions used for computing the parity:\n");
	printf("%8s", "");
	printf("%8s", "best");
	printf("%8s", "int8");
	printf("%8s", "int32");
	printf("%8s", "int64");
#ifdef CONFIG_NEON
	printf("%8s", "neon");
#endif
#ifdef CONFIG_NEON32
	printf("%8s", "neon32");
#endif
#ifdef CONFIG_X86
	if (raid_cpu_has_sse2()) {
		printf("%8s", "sse2");
#ifdef CONFIG_X86_64
		printf("%8s", "sse2e");
#endif
	}
	if (raid_cpu_has_ssse3()) {
		printf("%8s", "ssse3");
#ifdef CONFIG_X86_64
		printf("%8s", "ssse3e");
#endif
	}
	if (raid_cpu_has_avx2()) {
		printf("%8s", "avx2");
#ifdef CONFIG_X86_64
		printf("%8s", "avx2e");
#endif
	}
#endif
	printf("\n");

	/* GENz */
	printf("%8s", "genz");
	printf("%8s", raid_gen_tag(RAID_ALGO_VANDERMONDE_PAR3));
	fflush(stdout);

	printf("%8s", "");

	SPEED_START {
		raid_genz_int32_raid(nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);

	SPEED_START {
		raid_genz_int64_raid(nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);

#ifdef CONFIG_NEON
	SPEED_START {
		raid_genz_neon_raid(nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif
#ifdef CONFIG_NEON32
	SPEED_START {
		raid_genz_neon32_raid(nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif

#ifdef CONFIG_X86
	if (raid_cpu_has_sse2()) {
		SPEED_START {
			raid_genz_sse2_raid(nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);

#ifdef CONFIG_X86_64
		SPEED_START {
			raid_genz_sse2ext_raid(nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
#endif
	}
	if (raid_cpu_has_ssse3()) {
		printf("%8s", "");
#ifdef CONFIG_X86_64
		printf("%8s", "");
#endif
	}
	if (raid_cpu_has_avx2()) {
		printf("%8s", "");
#ifdef CONFIG_X86_64
		SPEED_START {
			raid_genz_avx2ext_raid(nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
#endif
	}
#endif

	printf("\n");
	printf("\n");
}

void speed_rec(int nd, void **v, int size, int delta, int period)
{
	struct timeval start;
	struct timeval stop;
	int64_t ds;
	int64_t dt;
	int i;
	int id[RAID_PARITY_MAX];
	int ip[RAID_PARITY_MAX];
	int count;

	/* basic disks and parity mapping */
	for (i = 0; i < RAID_PARITY_MAX; ++i) {
		id[i] = i;
		ip[i] = i;
	}

	/* recover table */
	printf("RAID polynomial functions used for recovering:\n");
	printf("%8s", "");
	printf("%8s", "best");
	printf("%8s", "int8");
#ifdef CONFIG_NEON
	printf("%8s", "neon");
#endif
#ifdef CONFIG_NEON32
	printf("%8s", "neon32");
#endif
#ifdef CONFIG_X86
	if (raid_cpu_has_ssse3())
		printf("%8s", "ssse3");
	if (raid_cpu_has_avx2())
		printf("%8s", "avx2");
#ifdef CONFIG_X86_64
	if (raid_cpu_has_avx512bw())
		printf("%8s", "avx512");
	if (raid_cpu_has_avx2gfni())
		printf("%8s", "gfni");
	if (raid_cpu_has_avx512gfni())
		printf("%8s", "gfni512");
#endif
#endif
	printf("\n");

	/* REC1OF1 */
	printf("%8s", "rec1of1");
	printf("%8s", raid_rec_tag(RAID_ALGO_CAUCHY_PAR1));
	fflush(stdout);

	SPEED_START {
		raid_gen_force(1, sizeof(void *) == 8 ? raid_gen1_int64 : raid_gen1_int32);
		raid_rec1_int8(1, id, ip, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);

#ifdef CONFIG_NEON
	SPEED_START {
		raid_gen_force(1, raid_gen1_neon);
		raid_rec1_neon(1, id, ip, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif
#ifdef CONFIG_NEON32
	SPEED_START {
		raid_gen_force(1, raid_gen1_neon32);
		raid_rec1_neon32(1, id, ip, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif

#ifdef CONFIG_X86
	if (raid_cpu_has_ssse3()) {
		SPEED_START {
			raid_gen_force(1, raid_gen1_sse2);
			raid_rec1_ssse3(1, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx2()) {
		SPEED_START {
			raid_gen_force(1, raid_gen1_avx2);
			raid_rec1_avx2(1, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#ifdef CONFIG_X86_64
	if (raid_cpu_has_avx512bw()) {
		SPEED_START {
			raid_gen_force(1, raid_gen1_avx512bw);
			raid_rec1_avx512bw(1, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx2gfni()) {
		SPEED_START {
			raid_gen_force(1, raid_gen1_avx2);
			raid_rec1_avx2gfni_raid(1, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx512gfni()) {
		SPEED_START {
			raid_gen_force(1, raid_gen1_avx512bw); /* there is no raid_gen1_avx512gfni */
			raid_rec1_avx512gfni_raid(1, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#endif
#endif
	printf("\n");

	/* REC1OF2 */
	printf("%8s", "rec1of2");
	printf("%8s", raid_rec_tag(RAID_ALGO_CAUCHY_PAR1));
	fflush(stdout);

	SPEED_START {
		raid_gen_force(1, sizeof(void *) == 8 ? raid_gen1_int64 : raid_gen1_int32);
		/* +1 to avoid GEN1 optimized case */
		raid_rec1_int8(1, id, ip + 1, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);

#ifdef CONFIG_NEON
	SPEED_START {
		raid_gen_force(1, raid_gen1_neon);
		/* +1 to avoid GEN1 optimized case */
		raid_rec1_neon(1, id, ip + 1, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif
#ifdef CONFIG_NEON32
	SPEED_START {
		raid_gen_force(1, raid_gen1_neon32);
		/* +1 to avoid GEN1 optimized case */
		raid_rec1_neon32(1, id, ip + 1, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif

#ifdef CONFIG_X86
	if (raid_cpu_has_ssse3()) {
		SPEED_START {
			raid_gen_force(1, raid_gen1_sse2);
			/* +1 to avoid GEN1 optimized case */
			raid_rec1_ssse3(1, id, ip + 1, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx2()) {
		SPEED_START {
			raid_gen_force(1, raid_gen1_avx2);
			/* +1 to avoid GEN1 optimized case */
			raid_rec1_avx2(1, id, ip + 1, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#ifdef CONFIG_X86_64
	if (raid_cpu_has_avx512bw()) {
		SPEED_START {
			raid_gen_force(1, raid_gen1_avx512bw);
			/* +1 to avoid GEN1 optimized case */
			raid_rec1_avx512bw(1, id, ip + 1, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx2gfni()) {
		SPEED_START {
			raid_gen_force(1, raid_gen1_avx2);
			/* +1 to avoid GEN1 optimized case */
			raid_rec1_avx2gfni_raid(1, id, ip + 1, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx512gfni()) {
		SPEED_START {
			raid_gen_force(1, raid_gen1_avx512bw); /* there is no raid_gen1_avx512gfni */
			/* +1 to avoid GEN1 optimized case */
			raid_rec1_avx512gfni_raid(1, id, ip + 1, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#endif
#endif
	printf("\n");

	/* REC2OF2 */
	printf("%8s", "rec2of2");
	printf("%8s", raid_rec_tag(RAID_ALGO_CAUCHY_PAR2));
	fflush(stdout);

	SPEED_START {
		raid_gen_force(2, sizeof(void *) == 8 ? raid_gen2_int64_raid : raid_gen2_int32_raid);
		raid_rec2_int8(2, id, ip, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);

#ifdef CONFIG_NEON
	SPEED_START {
		raid_gen_force(2, raid_gen2_neon_raid);
		raid_rec2_neon(2, id, ip, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif
#ifdef CONFIG_NEON32
	SPEED_START {
		raid_gen_force(2, raid_gen2_neon32_raid);
		raid_rec2_neon32(2, id, ip, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif

#ifdef CONFIG_X86
	if (raid_cpu_has_ssse3()) {
		SPEED_START {
#ifdef CONFIG_X86_64
			raid_gen_force(2, raid_gen2_sse2ext_raid);
#else
			raid_gen_force(2, raid_gen2_sse2_raid);
#endif
			raid_rec2_ssse3(2, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx2()) {
		SPEED_START {
#ifdef CONFIG_X86_64
			raid_gen_force(2, raid_gen2_avx2ext_raid);
#else
			raid_gen_force(2, raid_gen2_avx2_raid);
#endif
			raid_rec2_avx2(2, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#ifdef CONFIG_X86_64
	if (raid_cpu_has_avx512bw()) {
		SPEED_START {
			raid_gen_force(2, raid_gen2_avx512bw);
			raid_rec2_avx512bw(2, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx2gfni()) {
		SPEED_START {
			raid_gen_force(2, raid_gen2_avx2gfni_raid);
			raid_rec2_avx2gfni_raid(2, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx512gfni()) {
		SPEED_START {
			raid_gen_force(2, raid_gen2_avx512gfni_raid);
			raid_rec2_avx512gfni_raid(2, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#endif
#endif
	printf("\n");

	/* REC2OF3 */
	printf("%8s", "rec2of3");
	printf("%8s", raid_rec_tag(RAID_ALGO_CAUCHY_PAR2));
	fflush(stdout);

	SPEED_START {
		raid_gen_force(2, sizeof(void *) == 8 ? raid_gen2_int64_raid : raid_gen2_int32_raid);
		/* +1 to avoid GEN2 optimized case */
		raid_rec2_int8(2, id, ip + 1, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);

#ifdef CONFIG_NEON
	SPEED_START {
		raid_gen_force(2, raid_gen2_neon_raid);
		/* +1 to avoid GEN2 optimized case */
		raid_rec2_neon(2, id, ip + 1, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif
#ifdef CONFIG_NEON32
	SPEED_START {
		raid_gen_force(2, raid_gen2_neon32_raid);
		/* +1 to avoid GEN2 optimized case */
		raid_rec2_neon32(2, id, ip + 1, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif

#ifdef CONFIG_X86
	if (raid_cpu_has_ssse3()) {
		SPEED_START {
#ifdef CONFIG_X86_64
			raid_gen_force(2, raid_gen2_sse2ext_raid);
#else
			raid_gen_force(2, raid_gen2_sse2_raid);
#endif
			/* +1 to avoid GEN2 optimized case */
			raid_rec2_ssse3(2, id, ip + 1, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx2()) {
		SPEED_START {
#ifdef CONFIG_X86_64
			raid_gen_force(2, raid_gen2_avx2ext_raid);
#else
			raid_gen_force(2, raid_gen2_avx2_raid);
#endif
			/* +1 to avoid GEN2 optimized case */
			raid_rec2_avx2(2, id, ip + 1, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#ifdef CONFIG_X86_64
	if (raid_cpu_has_avx512bw()) {
		SPEED_START {
			raid_gen_force(2, raid_gen2_avx512bw);
			/* +1 to avoid GEN2 optimized case */
			raid_rec2_avx512bw(2, id, ip + 1, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx2gfni()) {
		SPEED_START {
			raid_gen_force(2, raid_gen2_avx2gfni_raid);
			/* +1 to avoid GEN2 optimized case */
			raid_rec2_avx2gfni_raid(2, id, ip + 1, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx512gfni()) {
		SPEED_START {
			raid_gen_force(2, raid_gen2_avx512gfni_raid);
			/* +1 to avoid GEN2 optimized case */
			raid_rec2_avx512gfni_raid(2, id, ip + 1, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#endif
#endif
	printf("\n");

	/* REC3OF3 */
	printf("%8s", "rec3of3");
	printf("%8s", raid_rec_tag(RAID_ALGO_CAUCHY_PAR3));
	fflush(stdout);

	SPEED_START {
		raid_gen_force(3, raid_gen3_int8);
		raid_recX_int8(3, id, ip, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);

#ifdef CONFIG_NEON
	SPEED_START {
		raid_gen_force(3, raid_gen3_neon_raid);
		raid_recX_neon(3, id, ip, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif
#ifdef CONFIG_NEON32
	SPEED_START {
		raid_gen_force(3, raid_gen3_neon32_raid);
		raid_recX_neon32(3, id, ip, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif

#ifdef CONFIG_X86
	if (raid_cpu_has_ssse3()) {
		SPEED_START {
#ifdef CONFIG_X86_64
			raid_gen_force(3, raid_gen3_ssse3ext_raid);
#else
			raid_gen_force(3, raid_gen3_ssse3_raid);
#endif
			raid_recX_ssse3(3, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx2()) {
		SPEED_START {
#ifdef CONFIG_X86_64
			raid_gen_force(3, raid_gen3_avx2ext_raid);
#else
			raid_gen_force(3, raid_gen3_ssse3_raid);
#endif
			raid_recX_avx2(3, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#ifdef CONFIG_X86_64
	if (raid_cpu_has_avx512bw()) {
		SPEED_START {
			raid_gen_force(3, raid_gen3_avx512bw);
			/* +1 to avoid GEN1 optimized case */
			raid_recX_avx512bw(3, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx2gfni()) {
		SPEED_START {
			raid_gen_force(3, raid_gen3_avx2gfni_raid);
			raid_recX_avx2gfni_raid(3, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx512gfni()) {
		SPEED_START {
			raid_gen_force(3, raid_gen3_avx512gfni_raid);
			raid_recX_avx512gfni_raid(3, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#endif
#endif
	printf("\n");

	/* REC4OF4 */
	printf("%8s", "rec4of4");
	printf("%8s", raid_rec_tag(RAID_ALGO_CAUCHY_PAR4));
	fflush(stdout);

	SPEED_START {
		raid_gen_force(4, raid_gen4_int8);
		raid_recX_int8(4, id, ip, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);

#ifdef CONFIG_NEON
	SPEED_START {
		raid_gen_force(4, raid_gen4_neon_raid);
		raid_recX_neon(4, id, ip, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif
#ifdef CONFIG_NEON32
	SPEED_START {
		raid_gen_force(4, raid_gen4_neon32_raid);
		raid_recX_neon32(4, id, ip, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif

#ifdef CONFIG_X86
	if (raid_cpu_has_ssse3()) {
		SPEED_START {
#ifdef CONFIG_X86_64
			raid_gen_force(4, raid_gen4_ssse3ext_raid);
#else
			raid_gen_force(4, raid_gen4_ssse3_raid);
#endif
			raid_recX_ssse3(4, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx2()) {
		SPEED_START {
#ifdef CONFIG_X86_64
			raid_gen_force(4, raid_gen4_avx2ext_raid);
#else
			raid_gen_force(4, raid_gen4_ssse3_raid);
#endif
			raid_recX_avx2(4, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#ifdef CONFIG_X86_64
	if (raid_cpu_has_avx512bw()) {
		SPEED_START {
			raid_gen_force(4, raid_gen4_avx512bw);
			/* +1 to avoid GEN1 optimized case */
			raid_recX_avx512bw(4, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx2gfni()) {
		SPEED_START {
			raid_gen_force(4, raid_gen4_avx2gfni_raid);
			raid_recX_avx2gfni_raid(4, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx512gfni()) {
		SPEED_START {
			raid_gen_force(4, raid_gen4_avx512gfni_raid);
			raid_recX_avx512gfni_raid(4, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#endif
#endif
	printf("\n");

	/* REC5OF5 */
	printf("%8s", "rec5of5");
	printf("%8s", raid_rec_tag(RAID_ALGO_CAUCHY_PAR5));
	fflush(stdout);

	SPEED_START {
		raid_gen_force(5, raid_gen5_int8);
		raid_recX_int8(5, id, ip, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);

#ifdef CONFIG_NEON
	SPEED_START {
		raid_gen_force(5, raid_gen5_neon_raid);
		raid_recX_neon(5, id, ip, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif
#ifdef CONFIG_NEON32
	SPEED_START {
		raid_gen_force(5, raid_gen5_neon32_raid);
		raid_recX_neon32(5, id, ip, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif

#ifdef CONFIG_X86
	if (raid_cpu_has_ssse3()) {
		SPEED_START {
#ifdef CONFIG_X86_64
			raid_gen_force(5, raid_gen5_ssse3ext_raid);
#else
			raid_gen_force(5, raid_gen5_ssse3_raid);
#endif
			raid_recX_ssse3(5, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx2()) {
		SPEED_START {
#ifdef CONFIG_X86_64
			raid_gen_force(5, raid_gen5_avx2ext_raid);
#else
			raid_gen_force(5, raid_gen5_ssse3_raid);
#endif
			raid_recX_avx2(5, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#ifdef CONFIG_X86_64
	if (raid_cpu_has_avx512bw()) {
		SPEED_START {
			raid_gen_force(5, raid_gen5_avx512bw);
			/* +1 to avoid GEN1 optimized case */
			raid_recX_avx512bw(5, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx2gfni()) {
		SPEED_START {
			raid_gen_force(5, raid_gen5_avx2gfni_raid);
			raid_recX_avx2gfni_raid(5, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx512gfni()) {
		SPEED_START {
			raid_gen_force(5, raid_gen5_avx512gfni_raid);
			raid_recX_avx512gfni_raid(5, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#endif
#endif
	printf("\n");

	/* REC6OF6 */
	printf("%8s", "rec6of6");
	printf("%8s", raid_rec_tag(RAID_ALGO_CAUCHY_PAR6));
	fflush(stdout);

	SPEED_START {
		raid_gen_force(6, raid_gen6_int8);
		raid_recX_int8(6, id, ip, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);

#ifdef CONFIG_NEON
	SPEED_START {
		raid_gen_force(6, raid_gen6_neon_raid);
		raid_recX_neon(6, id, ip, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif
#ifdef CONFIG_NEON32
	SPEED_START {
		raid_gen_force(6, raid_gen6_neon32_raid);
		raid_recX_neon32(6, id, ip, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif

#ifdef CONFIG_X86
	if (raid_cpu_has_ssse3()) {
		SPEED_START {
#ifdef CONFIG_X86_64
			raid_gen_force(6, raid_gen6_ssse3ext_raid);
#else
			raid_gen_force(6, raid_gen6_ssse3_raid);
#endif
			raid_recX_ssse3(6, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx2()) {
		SPEED_START {
#ifdef CONFIG_X86_64
			raid_gen_force(6, raid_gen6_avx2ext_raid);
#else
			raid_gen_force(6, raid_gen6_ssse3_raid);
#endif
			raid_recX_avx2(6, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#ifdef CONFIG_X86_64
	if (raid_cpu_has_avx512bw()) {
		SPEED_START {
			raid_gen_force(6, raid_gen6_avx512bw);
			/* +1 to avoid GEN1 optimized case */
			raid_recX_avx512bw(6, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx2gfni()) {
		SPEED_START {
			raid_gen_force(6, raid_gen6_avx2gfni_raid);
			raid_recX_avx2gfni_raid(6, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx512gfni()) {
		SPEED_START {
			raid_gen_force(6, raid_gen6_avx512gfni_raid);
			raid_recX_avx512gfni_raid(6, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#endif
#endif
	printf("\n");
	printf("\n");
	printf("(recNofM: N data disks recovered using parities up to the M-th parity,\n");
	printf(" e.g. rec1of1 uses P, rec1of2 uses Q, rec2of2 uses P+Q, rec2of3 uses Q+R)\n");
	printf("\n");
}

void speed(void)
{
	int nd = TEST_COUNT;
	int nv;
	void *v_alloc;
	void **v;
	int i;
	int size = TEST_SIZE;
	int delta = TEST_DELTA;
	int period = TEST_PERIOD;

	nv = nd + RAID_PARITY_MAX + 1;

	printf("Speed test using %u data buffers of %u bytes, for a total of %u KiB.\n", nd, size, nd * size / 1024);
	printf("Memory blocks have a displacement of %u bytes to improve cache performance.\n", raid_optimal_displacement(nv));
	printf("The reported values are the aggregate bandwidth of all data blocks in MiB/s,\n");
	printf("not counting parity blocks.\n");
	printf("\n");

	v = raid_malloc_vector(nv, size, &v_alloc);

	/* initialize disks with fixed data */
	for (i = 0; i < nd; ++i)
		memset(v[i], i, size);

	/* zero buffer */
	memset(v[nd + RAID_PARITY_MAX], 0, size);
	raid_zero(v[nd + RAID_PARITY_MAX]);

	speed_mem(nd, v, size, delta, period);

	raid_mode(RAID_MODE_CAUCHY_RAID);
	speed_gen(nd, v, size, delta, period, "RAID polynomial");

	raid_mode(RAID_MODE_CAUCHY_AES);
	speed_gen(nd, v, size, delta, period, "AES polynomial");

	raid_mode(RAID_MODE_VANDERMONDE_RAID);
	speed_genz(nd, v, size, delta, period);

	raid_mode(RAID_MODE_CAUCHY_RAID);
	speed_rec(nd, v, size, delta, period);

	free(v_alloc);
	free(v);
}

int main(void)
{
	printf("Speed test for the RAID Cauchy library\n\n");

	raid_init();

#ifdef CONFIG_NEON
	printf("Including ARM NEON\n");
#endif
#ifdef CONFIG_NEON32
	printf("Including ARM NEON\n");
#endif
#ifdef CONFIG_X86
	if (raid_cpu_has_sse2())
		printf("Including x86 SSE2\n");
	if (raid_cpu_has_ssse3())
		printf("Including x86 SSSE3\n");
	if (raid_cpu_has_avx2())
		printf("Including x86 AVX2\n");
	if (raid_cpu_has_avx512bw())
		printf("Including x86 AVX512BW\n");
	if (raid_cpu_has_avx2gfni())
		printf("Including x86 AVX2GFNI\n");
	if (raid_cpu_has_avx512gfni())
		printf("Including x86 AVX512GFNI\n");
#endif

	printf("\nPlease wait about 30 seconds...\n\n");

	speed();

	return 0;
}
