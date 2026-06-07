// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2011 Andrea Mazzoleni

#include "portable.h"

#include "snapraid.h"
#include "util.h"
#include "raid/raid.h"
#include "raid/cpu.h"
#include "raid/internal.h"
#include "raid/memory.h"
#include "state.h"

/**
 * Differential us of two timeval.
 */
static int64_t diffgettimeofday(struct timeval* start, struct timeval* stop)
{
	int64_t d;

	d = 1000000LL * (stop->tv_sec - start->tv_sec);
	d += stop->tv_usec - start->tv_usec;

	return d;
}

/**
 * Start time measurement.
 */
/* INDENT-OFF */
#define SPEED_START \
	count = 0; \
	gettimeofday(&start, 0); \
	do { \
		for (i = 0; i < delta; ++i)
/* INDENT-ON */

/**
 * Stop time measurement.
 */
/* INDENT-OFF */
#define SPEED_STOP \
	count += delta; \
	gettimeofday(&stop, 0); \
	} while (diffgettimeofday(&start, &stop) < period * 1000LL); \
	ds = size * (int64_t)count * nd; \
	dt = diffgettimeofday(&start, &stop);
/* INDENT-ON */

/**
 * Global variable used to propagate side effects.
 *
 * This is required to avoid optimizing compilers
 * to remove code without side effects.
 */
unsigned side_effect;

void speed_mem(int nd, void** v, int size, int delta, int period)
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

void speed_crc(int nd, void** v, int size, int delta, int period)
{
	struct timeval start;
	struct timeval stop;
	int64_t ds;
	int64_t dt;
	int i, j;
	int count;

	/* crc table */
	printf("CRC used to check the content file integrity:\n");

	printf("%8s", "table");
	fflush(stdout);

	SPEED_START {
		for (j = 0; j < nd; ++j)
			side_effect += crc32c_gen(0, v[j], size);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	printf("\n");

#if CONFIG_X86
	if (raid_cpu_has_crc32()) {
		printf("%8s", "intel");
		fflush(stdout);

		SPEED_START {
			for (j = 0; j < nd; ++j)
				side_effect += crc32c_x86(0, v[j], size);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		printf("\n");
	}
#endif

#if CONFIG_ARM_CRC
	printf("%8s", "arm");
	fflush(stdout);

	SPEED_START {
		for (j = 0; j < nd; ++j)
			side_effect += crc32c_arm64(0, v[j], size);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	printf("\n");
#endif

	printf("\n");
}

void speed_hash(int nd, void** v, int size, int delta, int period)
{
	struct timeval start;
	struct timeval stop;
	int64_t ds;
	int64_t dt;
	int i, j;
	int count;
	unsigned char digest[HASH_MAX];
	unsigned char seed[HASH_MAX];

	/* hash seed */
	for (i = 0; i < HASH_MAX; ++i)
		seed[i] = i;

	/* hash table */
	printf("Hash used to check the data blocks integrity:\n");

	printf("%8s", "");
	printf("%8s", "best");
	printf("%8s", "murmur3");
	printf("%8s", "spooky2");
	printf("%8s", "metro");
	printf("%8s", "museair");
	printf("\n");

	printf("%8s", "hash");
	printf("%8s", memhashname(membesthash()));
	fflush(stdout);

	SPEED_START {
		for (j = 0; j < nd; ++j)
			memhash(HASH_MURMUR3, seed, digest, v[j], size);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);

	SPEED_START {
		for (j = 0; j < nd; ++j)
			memhash(HASH_SPOOKY2, seed, digest, v[j], size);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);

	SPEED_START {
		for (j = 0; j < nd; ++j)
			memhash(HASH_METRO, seed, digest, v[j], size);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);

	SPEED_START {
		for (j = 0; j < nd; ++j)
			memhash(HASH_MUSEAIR, seed, digest, v[j], size);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	printf("\n");
	printf("\n");
}

void speed_gen(int nd, void** v, int size, int delta, int period, const char* msg)
{
	struct timeval start;
	struct timeval stop;
	int64_t ds;
	int64_t dt;
	int i;
	int count;

	/* RAID table */
	printf("RAID functions used for computing the parity with 'sync'%s:\n", msg);
	printf("%8s", "");
	printf("%8s", "best");
	printf("%8s", "int8");
	printf("%8s", "int32");
	printf("%8s", "int64");
#ifdef CONFIG_NEON
	printf("%8s", "neon");
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
	if (raid_cpu_has_avx512bw())
		printf("%8s", "avx512");
	if (raid_cpu_has_avx2gfni())
		printf("%8s", "gfni");
	if (raid_cpu_has_avx512gfni())
		printf("%8s", "gfni512");
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

#ifdef CONFIG_NEON
	SPEED_START {
		raid_gen2_neon(nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif

#ifdef CONFIG_X86
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
	if (raid_cpu_has_ssse3()) {
		printf("%8s", "");
#ifdef CONFIG_X86_64
		printf("%8s", "");
#endif
	}
	if (raid_cpu_has_avx2()) {
		SPEED_START {
			raid_gen2_avx2(nd, size, v);
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
			raid_gen2_avx512bw(nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx2gfni()) {
		SPEED_START {
			raid_gen2_avx2gfni(nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx512gfni()) {
		SPEED_START {
			raid_gen2_avx512gfni(nd, size, v);
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

#ifdef CONFIG_NEON
	SPEED_START {
		raid_genz_neon(nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif

#ifdef CONFIG_X86
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
			raid_genz_avx2ext(nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
#endif
	}
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
	SPEED_START {
		raid_gen3_neon(nd, size, v);
	} SPEED_STOP

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
	if (raid_cpu_has_avx2()) {
		printf("%8s", "");
#ifdef CONFIG_X86_64
		SPEED_START {
			raid_gen3_avx2ext(nd, size, v);
		} SPEED_STOP

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
		SPEED_START {
			raid_gen3_avx2gfni(nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx512gfni()) {
		SPEED_START {
			raid_gen3_avx512gfni(nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
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
	SPEED_START {
		raid_gen4_neon(nd, size, v);
	} SPEED_STOP

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
	if (raid_cpu_has_avx2()) {
		printf("%8s", "");
#ifdef CONFIG_X86_64
		SPEED_START {
			raid_gen4_avx2ext(nd, size, v);
		} SPEED_STOP

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
		SPEED_START {
			raid_gen4_avx2gfni(nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx512gfni()) {
		SPEED_START {
			raid_gen4_avx512gfni(nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
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
	SPEED_START {
		raid_gen5_neon(nd, size, v);
	} SPEED_STOP

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
	if (raid_cpu_has_avx2()) {
		printf("%8s", "");
#ifdef CONFIG_X86_64
		SPEED_START {
			raid_gen5_avx2ext(nd, size, v);
		} SPEED_STOP

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
		SPEED_START {
			raid_gen5_avx2gfni(nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx512gfni()) {
		SPEED_START {
			raid_gen5_avx512gfni(nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#endif
#endif
	printf("\n");

	/* GEN6 */
	printf("%8s", "gen6");
	printf("%8s", raid_gen_tag(RAID_ALGO_CAUCHY_PAR5));
	fflush(stdout);

	SPEED_START {
		raid_gen6_int8(nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);

	printf("%8s", "");
	printf("%8s", "");

#ifdef CONFIG_NEON
	SPEED_START {
		raid_gen6_neon(nd, size, v);
	} SPEED_STOP

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
	if (raid_cpu_has_avx2()) {
		printf("%8s", "");
#ifdef CONFIG_X86_64
		SPEED_START {
			raid_gen6_avx2ext(nd, size, v);
		} SPEED_STOP

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
		SPEED_START {
			raid_gen6_avx2gfni(nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx512gfni()) {
		SPEED_START {
			raid_gen6_avx512gfni(nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#endif
#endif
	printf("\n");
	printf("\n");
}

void speed_rec(int nd, void** v, int size, int delta, int period)
{
	struct timeval start;
	struct timeval stop;
	int64_t ds;
	int64_t dt;
	int i;
	int count;
	int id[RAID_PARITY_MAX];
	int ip[RAID_PARITY_MAX];

	/* basic disks and parity mapping */
	for (i = 0; i < RAID_PARITY_MAX; ++i) {
		id[i] = i;
		ip[i] = i;
	}

	/* recover table */
	printf("RAID functions used for recovering with 'fix':\n");
	printf("%8s", "");
	printf("%8s", "best");
	printf("%8s", "int8");
#ifdef CONFIG_NEON
	printf("%8s", "neon");
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

	printf("%8s", "rec1");
	printf("%8s", raid_rec_tag(RAID_ALGO_CAUCHY_PAR1));
	fflush(stdout);

	SPEED_START {
		/* ensure to use same hardware in the delta step */
		raid_gen_force(1, sizeof(void*) == 8 ? raid_gen1_int64 : raid_gen1_int32);
		/* +1 to avoid GEN1 optimized case */
		raid_rec1_int8(1, id, ip + 1, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);

#ifdef CONFIG_NEON
	SPEED_START {
		/* ensure to use same hardware in the delta step */
		raid_gen_force(1, raid_gen1_neon);
		/* +1 to avoid GEN1 optimized case */
		raid_rec1_neon(1, id, ip + 1, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif

#ifdef CONFIG_X86
	if (raid_cpu_has_ssse3()) {
		SPEED_START {
			/* ensure to use same hardware in the delta step */
			raid_gen_force(1, raid_gen1_sse2);
			/* +1 to avoid GEN1 optimized case */
			raid_rec1_ssse3(1, id, ip + 1, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx2()) {
		SPEED_START {
			/* ensure to use same hardware in the delta step */
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
			/* ensure to use same hardware in the delta step */
			raid_gen_force(1, raid_gen1_avx512bw);
			/* +1 to avoid GEN1 optimized case */
			raid_rec1_avx512bw(1, id, ip + 1, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx2gfni()) {
		SPEED_START {
			/* ensure to use same hardware in the delta step */
			raid_gen_force(1, raid_gen1_avx2);
			/* +1 to avoid GEN1 optimized case */
			raid_rec1_avx2gfni(1, id, ip + 1, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx512gfni()) {
		SPEED_START {
			/* ensure to use same hardware in the delta step */
			raid_gen_force(1, raid_gen1_avx512bw); /* there is no raid_gen1_avx512gfni */
			/* +1 to avoid GEN1 optimized case */
			raid_rec1_avx512gfni(1, id, ip + 1, nd, size, v);
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
		/* ensure to use same hardware in the delta step */
		raid_gen_force(2, sizeof(void*) == 8 ? raid_gen2_int64 : raid_gen2_int32);
		/* +1 to avoid GEN2 optimized case */
		raid_rec2_int8(2, id, ip + 1, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);

#ifdef CONFIG_NEON
	SPEED_START {
		/* ensure to use same hardware in the delta step */
		raid_gen_force(2, raid_gen2_neon);
		/* +1 to avoid GEN2 optimized case */
		raid_rec2_neon(2, id, ip + 1, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif

#ifdef CONFIG_X86
	if (raid_cpu_has_ssse3()) {
		SPEED_START {
			/* ensure to use same hardware in the delta step */
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
	if (raid_cpu_has_avx2()) {
		SPEED_START {
			/* ensure to use same hardware in the delta step */
			raid_gen_force(2, raid_gen2_avx2);
			/* +1 to avoid GEN2 optimized case */
			raid_rec2_avx2(2, id, ip + 1, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#ifdef CONFIG_X86_64
	if (raid_cpu_has_avx512bw()) {
		SPEED_START {
			/* ensure to use same hardware in the delta step */
			raid_gen_force(2, raid_gen2_avx512bw);
			/* +1 to avoid GEN2 optimized case */
			raid_rec2_avx512bw(2, id, ip + 1, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx2gfni()) {
		SPEED_START {
			/* ensure to use same hardware in the delta step */
			raid_gen_force(2, raid_gen2_avx2gfni);
			/* +1 to avoid GEN2 optimized case */
			raid_rec2_avx2gfni(2, id, ip + 1, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx512gfni()) {
		SPEED_START {
			/* ensure to use same hardware in the delta step */
			raid_gen_force(2, raid_gen2_avx512gfni);
			/* +1 to avoid GEN2 optimized case */
			raid_rec2_avx512gfni(2, id, ip + 1, nd, size, v);
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
		/* ensure to use same hardware in the delta step */
		raid_gen_force(3, raid_gen3_int8);
		raid_recX_int8(3, id, ip, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);

#ifdef CONFIG_NEON
	SPEED_START {
		/* ensure to use same hardware in the delta step */
		raid_gen_force(3, raid_gen3_neon);
		raid_recX_neon(3, id, ip, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif

#ifdef CONFIG_X86
	if (raid_cpu_has_ssse3()) {
		SPEED_START {
			/* ensure to use same hardware in the delta step */
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
	if (raid_cpu_has_avx2()) {
		SPEED_START {
			/* ensure to use same hardware in the delta step */
#ifdef CONFIG_X86_64
			raid_gen_force(3, raid_gen3_avx2ext);
#else
			raid_gen_force(3, raid_gen3_ssse3);
#endif
			raid_recX_avx2(3, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#ifdef CONFIG_X86_64
	if (raid_cpu_has_avx512bw()) {
		SPEED_START {
			/* ensure to use same hardware in the delta step */
			raid_gen_force(3, raid_gen3_avx512bw);
			/* +1 to avoid GEN1 optimized case */
			raid_recX_avx512bw(3, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx2gfni()) {
		SPEED_START {
			/* ensure to use same hardware in the delta step */
			raid_gen_force(3, raid_gen3_avx2gfni);
			raid_recX_avx2gfni(3, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx512gfni()) {
		SPEED_START {
			/* ensure to use same hardware in the delta step */
			raid_gen_force(3, raid_gen3_avx512gfni);
			raid_recX_avx512gfni(3, id, ip, nd, size, v);
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
		/* ensure to use same hardware in the delta step */
		raid_gen_force(4, raid_gen4_int8);
		raid_recX_int8(4, id, ip, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);

#ifdef CONFIG_NEON
	SPEED_START {
		/* ensure to use same hardware in the delta step */
		raid_gen_force(4, raid_gen4_neon);
		raid_recX_neon(4, id, ip, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif

#ifdef CONFIG_X86
	if (raid_cpu_has_ssse3()) {
		SPEED_START {
			/* ensure to use same hardware in the delta step */
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
	if (raid_cpu_has_avx2()) {
		SPEED_START {
			/* ensure to use same hardware in the delta step */
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
#ifdef CONFIG_X86_64
	if (raid_cpu_has_avx512bw()) {
		SPEED_START {
			/* ensure to use same hardware in the delta step */
			raid_gen_force(4, raid_gen4_avx512bw);
			/* +1 to avoid GEN1 optimized case */
			raid_recX_avx512bw(4, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx2gfni()) {
		SPEED_START {
			/* ensure to use same hardware in the delta step */
			raid_gen_force(4, raid_gen4_avx2gfni);
			raid_recX_avx2gfni(4, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx512gfni()) {
		SPEED_START {
			/* ensure to use same hardware in the delta step */
			raid_gen_force(4, raid_gen4_avx512gfni);
			raid_recX_avx512gfni(4, id, ip, nd, size, v);
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
		/* ensure to use same hardware in the delta step */
		raid_gen_force(5, raid_gen5_int8);
		raid_recX_int8(5, id, ip, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);

#ifdef CONFIG_NEON
	SPEED_START {
		/* ensure to use same hardware in the delta step */
		raid_gen_force(5, raid_gen5_neon);
		raid_recX_neon(5, id, ip, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif

#ifdef CONFIG_X86
	if (raid_cpu_has_ssse3()) {
		SPEED_START {
			/* ensure to use same hardware in the delta step */
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
	if (raid_cpu_has_avx2()) {
		SPEED_START {
			/* ensure to use same hardware in the delta step */
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
#ifdef CONFIG_X86_64
	if (raid_cpu_has_avx512bw()) {
		SPEED_START {
			/* ensure to use same hardware in the delta step */
			raid_gen_force(5, raid_gen5_avx512bw);
			/* +1 to avoid GEN1 optimized case */
			raid_recX_avx512bw(5, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx2gfni()) {
		SPEED_START {
			/* ensure to use same hardware in the delta step */
			raid_gen_force(5, raid_gen5_avx2gfni);
			raid_recX_avx2gfni(5, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx512gfni()) {
		SPEED_START {
			/* ensure to use same hardware in the delta step */
			raid_gen_force(5, raid_gen5_avx512gfni);
			raid_recX_avx512gfni(5, id, ip, nd, size, v);
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
		/* ensure to use same hardware in the delta step */
		raid_gen_force(6, raid_gen6_int8);
		raid_recX_int8(6, id, ip, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);

#ifdef CONFIG_NEON
	SPEED_START {
		/* ensure to use same hardware in the delta step */
		raid_gen_force(6, raid_gen6_neon);
		raid_recX_neon(6, id, ip, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif

#ifdef CONFIG_X86
	if (raid_cpu_has_ssse3()) {
		SPEED_START {
			/* ensure to use same hardware in the delta step */
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
	if (raid_cpu_has_avx2()) {
		SPEED_START {
			/* ensure to use same hardware in the delta step */
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
#ifdef CONFIG_X86_64
	if (raid_cpu_has_avx512bw()) {
		SPEED_START {
			/* ensure to use same hardware in the delta step */
			raid_gen_force(6, raid_gen6_avx512bw);
			/* +1 to avoid GEN1 optimized case */
			raid_recX_avx512bw(6, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx2gfni()) {
		SPEED_START {
			/* ensure to use same hardware in the delta step */
			raid_gen_force(6, raid_gen6_avx2gfni);
			raid_recX_avx2gfni(6, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx512gfni()) {
		SPEED_START {
			/* ensure to use same hardware in the delta step */
			raid_gen_force(6, raid_gen6_avx512gfni);
			raid_recX_avx512gfni(6, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#endif
#endif
	printf("\n");
	printf("\n");
}

void speed_affinity(void)
{
#if HAVE_LINUX_DEVICE
	int num_cpus = sysconf(_SC_NPROCESSORS_CONF);
	if (num_cpus <= 0)
		return;

	printf("Machine has %d cores\n", num_cpus);

	int cpu = os_get_optimal_cpu();
	if (cpu <= 0)
		return;

	cpu_set_t mask;
	CPU_ZERO(&mask);
	CPU_SET(cpu, &mask);

	if (sched_setaffinity(0, sizeof(cpu_set_t), &mask) == 0) {
		printf("Running on core %d\n", cpu);
	}
#endif
}

void speed(int period, int nd, int size)
{
	int i;
	int delta;
	int nv;
	void* v_alloc;
	void** v;
	void* v_direct_alloc;
	void** v_direct;

	if (nd < 0)
		nd = 8; /* default */
	if (nd < 6)
		nd = 6; /* minimum */
	if (size < 0)
		size = 256 * KIBI;
	else
		size *= KIBI;
	if (period < 1)
		period = 1000;

	delta = period >= 1000 ? 10 : 1;

	nv = nd + RAID_PARITY_MAX + 1;

	v = malloc_nofail_vector_align(nv, size, &v_alloc);
	v_direct = raid_malloc_vector_align(nv, size, RAID_MALLOC_ALIGN, 0, 0, &v_direct_alloc);

	/* initialize disks with fixed data */
	for (i = 0; i < nd; ++i) {
		memset(v[i], i, size);
		memset(v_direct[i], i, size);
	}

	/* zero buffer */
	memset(v[nd + RAID_PARITY_MAX], 0, size);
	memset(v_direct[nd + RAID_PARITY_MAX], 0, size);
	raid_zero(v[nd + RAID_PARITY_MAX]);

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

		printf("CPU %s, family %u, model %u (%xh), flags%s%s%s%s%s%s%s%s%s%s\n", vendor, family, model, model,
			raid_cpu_has_sse2() ? " sse2" : "",
			raid_cpu_has_ssse3() ? " ssse3" : "",
			raid_cpu_has_crc32() ? " crc32" : "",
			raid_cpu_has_avx2() ? " avx2" : "",
			raid_cpu_has_avx2gfni() ? " avx2gfni" : "",
			raid_cpu_has_avx512bw() ? " avx512bw" : "",
			raid_cpu_has_avx512gfni() ? " avx512gfni" : "",
			raid_cpu_has_slowmult() ? " slowmult" : "",
			raid_cpu_has_slow_extendedreg() ? " slowext" : "",
			raid_cpu_has_slow_avx512() ? " slowavx512" : ""
		);
	}
#elif defined(__aarch64__) || defined(_M_ARM64)
	printf("CPU 64-bit ARM (AArch64)\n");
#elif defined(__arm__) || defined(_M_ARM)
	printf("CPU 32-bit ARM\n");
#elif defined(__powerpc64__)
	printf("CPU 64-bit PowerPC\n");
#elif defined(__powerpc__)
	printf("CPU 32-bit PowerPC\n");
#elif defined(__riscv)
	printf("CPU RISC-V\n");
#elif defined(__s390x__)
	printf("CPU 64-bit IBM Z / s390x\n");
#else
	printf("CPU of unknown architecture\n");
#endif
	speed_affinity();
#if WORDS_BIGENDIAN
	printf("Memory is big-endian %d-bit\n", (int)sizeof(void*) * 8);
#else
	printf("Memory is little-endian %d-bit\n", (int)sizeof(void*) * 8);
#endif
#if defined(__SIZEOF_INT128__)
	printf("128-bit integers are supported\n");
#else
	printf("128-bit integers are not supported\n");
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

	printf("Speed test using %u data buffers of %u bytes, for a total of %u KiB.\n", nd, size, nd * size / KIBI);
	printf("Memory blocks have a displacement of %u bytes to improve cache performance.\n", raid_optimal_displacement(nv));
	printf("The reported values are the aggregate bandwidth of all data blocks in MB/s,\n");
	printf("not counting parity blocks.\n");
	printf("\n");

	speed_mem(nd, v, size, delta, period);
	speed_crc(nd, v, size, delta, period);
	speed_hash(nd, v, size, delta, period);
	speed_gen(nd, v, size, delta, period, "");
	speed_gen(nd, v_direct, size, delta, period, " (without displacement)");
	speed_rec(nd, v, size, delta, period);

	printf("If the 'best' expectations are wrong, please report it in the SnapRAID forum\n\n");

	free(v_alloc);
	free(v);
	free(v_direct_alloc);
	free(v_direct);
}

