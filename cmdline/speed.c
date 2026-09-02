// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2011 Andrea Mazzoleni

#include "os/portable.h"

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
	int mode = raid_mode(RAID_MODE_GET);

	/* RAID table */
	printf("%s functions used for computing parity with 'sync':\n", msg);
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
		raid_gen1_int32(nd, size, v, 1);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);

	SPEED_START {
		raid_gen1_int64(nd, size, v, 1);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);

#ifdef CONFIG_NEON
	SPEED_START {
		raid_gen1_neon(nd, size, v, 1);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif
#ifdef CONFIG_NEON32
	SPEED_START {
		raid_gen1_neon32(nd, size, v, 1);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif

#ifdef CONFIG_X86
	if (raid_cpu_has_sse2()) {
		SPEED_START {
			raid_gen1_sse2(nd, size, v, 1);
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
			raid_gen1_avx2(nd, size, v, 1);
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
			raid_gen1_avx512bw(nd, size, v, 1);
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

	SPEED_START {
		raid_gen2_int8(nd, size, v, 1);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);

	if (mode == RAID_MODE_CAUCHY_AES) {
		SPEED_START {
			raid_gen2_int32_aes(nd, size, v, 1);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);

		SPEED_START {
			raid_gen2_int64_aes(nd, size, v, 1);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	} else {
		SPEED_START {
			raid_gen2_int32_raid(nd, size, v, 1);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);

		SPEED_START {
			raid_gen2_int64_raid(nd, size, v, 1);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}

#ifdef CONFIG_NEON
	if (mode == RAID_MODE_CAUCHY_AES) {
		SPEED_START {
			raid_gen2_neon_aes(nd, size, v, 1);
		} SPEED_STOP
	} else {
		SPEED_START {
			raid_gen2_neon_raid(nd, size, v, 1);
		} SPEED_STOP
	}

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif
#ifdef CONFIG_NEON32
	if (mode == RAID_MODE_CAUCHY_AES) {
		SPEED_START {
			raid_gen2_neon32_aes(nd, size, v, 1);
		} SPEED_STOP
	} else {
		SPEED_START {
			raid_gen2_neon32_raid(nd, size, v, 1);
		} SPEED_STOP
	}

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif

#ifdef CONFIG_X86
	if (raid_cpu_has_sse2()) {
		if (mode == RAID_MODE_CAUCHY_AES) {
			SPEED_START {
				raid_gen2_sse2_aes(nd, size, v, 1);
			} SPEED_STOP
		} else {
			SPEED_START {
				raid_gen2_sse2_raid(nd, size, v, 1);
			} SPEED_STOP
		}

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);

#ifdef CONFIG_X86_64
		if (mode == RAID_MODE_CAUCHY_AES) {
			SPEED_START {
				raid_gen2_sse2ext_aes(nd, size, v, 1);
			} SPEED_STOP
		} else {
			SPEED_START {
				raid_gen2_sse2ext_raid(nd, size, v, 1);
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
				raid_gen2_avx2_aes(nd, size, v, 1);
			} SPEED_STOP
		} else {
			SPEED_START {
				raid_gen2_avx2_raid(nd, size, v, 1);
			} SPEED_STOP
		}

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);

#ifdef CONFIG_X86_64
		if (mode == RAID_MODE_CAUCHY_AES) {
			SPEED_START {
				raid_gen2_avx2ext_aes(nd, size, v, 1);
			} SPEED_STOP
		} else {
			SPEED_START {
				raid_gen2_avx2ext_raid(nd, size, v, 1);
			} SPEED_STOP
		}

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
#endif
	}
#ifdef CONFIG_X86_64
	if (raid_cpu_has_avx512bw()) {
		SPEED_START {
			raid_gen2_avx512bw(nd, size, v, 1);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx2gfni()) {
		if (mode == RAID_MODE_CAUCHY_AES) {
			SPEED_START {
				raid_gen2_avx2gfni_aes(nd, size, v, 1);
			} SPEED_STOP

			printf("%8" PRIu64, ds / dt);
		} else {
			SPEED_START {
				raid_gen2_avx2gfni_raid(nd, size, v, 1);
			} SPEED_STOP

			printf("%8" PRIu64, ds / dt);
		}
		fflush(stdout);
	}
	if (raid_cpu_has_avx512gfni()) {
		if (mode == RAID_MODE_CAUCHY_AES) {
			SPEED_START {
				raid_gen2_avx512gfni_aes(nd, size, v, 1);
			} SPEED_STOP

			printf("%8" PRIu64, ds / dt);
		} else {
			SPEED_START {
				raid_gen2_avx512gfni_raid(nd, size, v, 1);
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
		raid_gen3_int8(nd, size, v, 1);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);

	printf("%8s", "");
	printf("%8s", "");

#ifdef CONFIG_NEON
	if (mode == RAID_MODE_CAUCHY_AES) {
		SPEED_START {
			raid_gen3_neon_aes(nd, size, v, 1);
		} SPEED_STOP
	} else {
		SPEED_START {
			raid_gen3_neon_raid(nd, size, v, 1);
		} SPEED_STOP
	}

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif
#ifdef CONFIG_NEON32
	if (mode == RAID_MODE_CAUCHY_AES) {
		SPEED_START {
			raid_gen3_neon32_aes(nd, size, v, 1);
		} SPEED_STOP
	} else {
		SPEED_START {
			raid_gen3_neon32_raid(nd, size, v, 1);
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
				raid_gen3_ssse3_aes(nd, size, v, 1);
			} SPEED_STOP
		} else {
			SPEED_START {
				raid_gen3_ssse3_raid(nd, size, v, 1);
			} SPEED_STOP
		}

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);

#ifdef CONFIG_X86_64
		if (mode == RAID_MODE_CAUCHY_AES) {
			SPEED_START {
				raid_gen3_ssse3ext_aes(nd, size, v, 1);
			} SPEED_STOP
		} else {
			SPEED_START {
				raid_gen3_ssse3ext_raid(nd, size, v, 1);
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
				raid_gen3_avx2ext_aes(nd, size, v, 1);
			} SPEED_STOP
		} else {
			SPEED_START {
				raid_gen3_avx2ext_raid(nd, size, v, 1);
			} SPEED_STOP
		}

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
#endif
	}
#ifdef CONFIG_X86_64
	if (raid_cpu_has_avx512bw()) {
		SPEED_START {
			raid_gen3_avx512bw(nd, size, v, 1);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx2gfni()) {
		if (mode == RAID_MODE_CAUCHY_AES) {
			SPEED_START {
				raid_gen3_avx2gfni_aes(nd, size, v, 1);
			} SPEED_STOP

			printf("%8" PRIu64, ds / dt);
		} else {
			SPEED_START {
				raid_gen3_avx2gfni_raid(nd, size, v, 1);
			} SPEED_STOP

			printf("%8" PRIu64, ds / dt);
		}
		fflush(stdout);
	}
	if (raid_cpu_has_avx512gfni()) {
		if (mode == RAID_MODE_CAUCHY_AES) {
			SPEED_START {
				raid_gen3_avx512gfni_aes(nd, size, v, 1);
			} SPEED_STOP

			printf("%8" PRIu64, ds / dt);
		} else {
			SPEED_START {
				raid_gen3_avx512gfni_raid(nd, size, v, 1);
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
		raid_gen4_int8(nd, size, v, 1);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);

	printf("%8s", "");
	printf("%8s", "");

#ifdef CONFIG_NEON
	if (mode == RAID_MODE_CAUCHY_AES) {
		SPEED_START {
			raid_gen4_neon_aes(nd, size, v, 1);
		} SPEED_STOP
	} else {
		SPEED_START {
			raid_gen4_neon_raid(nd, size, v, 1);
		} SPEED_STOP
	}

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif
#ifdef CONFIG_NEON32
	if (mode == RAID_MODE_CAUCHY_AES) {
		SPEED_START {
			raid_gen4_neon32_aes(nd, size, v, 1);
		} SPEED_STOP
	} else {
		SPEED_START {
			raid_gen4_neon32_raid(nd, size, v, 1);
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
				raid_gen4_ssse3_aes(nd, size, v, 1);
			} SPEED_STOP
		} else {
			SPEED_START {
				raid_gen4_ssse3_raid(nd, size, v, 1);
			} SPEED_STOP
		}

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);

#ifdef CONFIG_X86_64
		if (mode == RAID_MODE_CAUCHY_AES) {
			SPEED_START {
				raid_gen4_ssse3ext_aes(nd, size, v, 1);
			} SPEED_STOP
		} else {
			SPEED_START {
				raid_gen4_ssse3ext_raid(nd, size, v, 1);
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
				raid_gen4_avx2ext_aes(nd, size, v, 1);
			} SPEED_STOP
		} else {
			SPEED_START {
				raid_gen4_avx2ext_raid(nd, size, v, 1);
			} SPEED_STOP
		}

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
#endif
	}
#ifdef CONFIG_X86_64
	if (raid_cpu_has_avx512bw()) {
		SPEED_START {
			raid_gen4_avx512bw(nd, size, v, 1);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx2gfni()) {
		if (mode == RAID_MODE_CAUCHY_AES) {
			SPEED_START {
				raid_gen4_avx2gfni_aes(nd, size, v, 1);
			} SPEED_STOP

			printf("%8" PRIu64, ds / dt);
		} else {
			SPEED_START {
				raid_gen4_avx2gfni_raid(nd, size, v, 1);
			} SPEED_STOP

			printf("%8" PRIu64, ds / dt);
		}
		fflush(stdout);
	}
	if (raid_cpu_has_avx512gfni()) {
		if (mode == RAID_MODE_CAUCHY_AES) {
			SPEED_START {
				raid_gen4_avx512gfni_aes(nd, size, v, 1);
			} SPEED_STOP

			printf("%8" PRIu64, ds / dt);
		} else {
			SPEED_START {
				raid_gen4_avx512gfni_raid(nd, size, v, 1);
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
		raid_gen5_int8(nd, size, v, 1);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);

	printf("%8s", "");
	printf("%8s", "");

#ifdef CONFIG_NEON
	if (mode == RAID_MODE_CAUCHY_AES) {
		SPEED_START {
			raid_gen5_neon_aes(nd, size, v, 1);
		} SPEED_STOP
	} else {
		SPEED_START {
			raid_gen5_neon_raid(nd, size, v, 1);
		} SPEED_STOP
	}

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif
#ifdef CONFIG_NEON32
	if (mode == RAID_MODE_CAUCHY_AES) {
		SPEED_START {
			raid_gen5_neon32_aes(nd, size, v, 1);
		} SPEED_STOP
	} else {
		SPEED_START {
			raid_gen5_neon32_raid(nd, size, v, 1);
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
				raid_gen5_ssse3_raid(nd, size, v, 1);
			} SPEED_STOP

			printf("%8" PRIu64, ds / dt);
		} else {
			printf("%8s", "");
		}
		fflush(stdout);

#ifdef CONFIG_X86_64
		if (mode == RAID_MODE_CAUCHY_AES) {
			SPEED_START {
				raid_gen5_ssse3ext_aes(nd, size, v, 1);
			} SPEED_STOP
		} else {
			SPEED_START {
				raid_gen5_ssse3ext_raid(nd, size, v, 1);
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
				raid_gen5_avx2ext_aes(nd, size, v, 1);
			} SPEED_STOP
		} else {
			SPEED_START {
				raid_gen5_avx2ext_raid(nd, size, v, 1);
			} SPEED_STOP
		}

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
#endif
	}
#ifdef CONFIG_X86_64
	if (raid_cpu_has_avx512bw()) {
		SPEED_START {
			raid_gen5_avx512bw(nd, size, v, 1);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx2gfni()) {
		if (mode == RAID_MODE_CAUCHY_AES) {
			SPEED_START {
				raid_gen5_avx2gfni_aes(nd, size, v, 1);
			} SPEED_STOP

			printf("%8" PRIu64, ds / dt);
		} else {
			SPEED_START {
				raid_gen5_avx2gfni_raid(nd, size, v, 1);
			} SPEED_STOP

			printf("%8" PRIu64, ds / dt);
		}
		fflush(stdout);
	}
	if (raid_cpu_has_avx512gfni()) {
		if (mode == RAID_MODE_CAUCHY_AES) {
			SPEED_START {
				raid_gen5_avx512gfni_aes(nd, size, v, 1);
			} SPEED_STOP

			printf("%8" PRIu64, ds / dt);
		} else {
			SPEED_START {
				raid_gen5_avx512gfni_raid(nd, size, v, 1);
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
		raid_gen6_int8(nd, size, v, 1);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);

	printf("%8s", "");
	printf("%8s", "");

#ifdef CONFIG_NEON
	if (mode == RAID_MODE_CAUCHY_AES) {
		SPEED_START {
			raid_gen6_neon_aes(nd, size, v, 1);
		} SPEED_STOP
	} else {
		SPEED_START {
			raid_gen6_neon_raid(nd, size, v, 1);
		} SPEED_STOP
	}

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif
#ifdef CONFIG_NEON32
	if (mode == RAID_MODE_CAUCHY_AES) {
		SPEED_START {
			raid_gen6_neon32_aes(nd, size, v, 1);
		} SPEED_STOP
	} else {
		SPEED_START {
			raid_gen6_neon32_raid(nd, size, v, 1);
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
				raid_gen6_ssse3_raid(nd, size, v, 1);
			} SPEED_STOP

			printf("%8" PRIu64, ds / dt);
		} else {
			printf("%8s", "");
		}
		fflush(stdout);

#ifdef CONFIG_X86_64
		if (mode == RAID_MODE_CAUCHY_AES) {
			SPEED_START {
				raid_gen6_ssse3ext_aes(nd, size, v, 1);
			} SPEED_STOP
		} else {
			SPEED_START {
				raid_gen6_ssse3ext_raid(nd, size, v, 1);
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
				raid_gen6_avx2ext_aes(nd, size, v, 1);
			} SPEED_STOP
		} else {
			SPEED_START {
				raid_gen6_avx2ext_raid(nd, size, v, 1);
			} SPEED_STOP
		}

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
#endif
	}
#ifdef CONFIG_X86_64
	if (raid_cpu_has_avx512bw()) {
		SPEED_START {
			raid_gen6_avx512bw(nd, size, v, 1);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx2gfni()) {
		if (mode == RAID_MODE_CAUCHY_AES) {
			SPEED_START {
				raid_gen6_avx2gfni_aes(nd, size, v, 1);
			} SPEED_STOP

			printf("%8" PRIu64, ds / dt);
		} else {
			SPEED_START {
				raid_gen6_avx2gfni_raid(nd, size, v, 1);
			} SPEED_STOP

			printf("%8" PRIu64, ds / dt);
		}
		fflush(stdout);
	}
	if (raid_cpu_has_avx512gfni()) {
		if (mode == RAID_MODE_CAUCHY_AES) {
			SPEED_START {
				raid_gen6_avx512gfni_aes(nd, size, v, 1);
			} SPEED_STOP

			printf("%8" PRIu64, ds / dt);
		} else {
			SPEED_START {
				raid_gen6_avx512gfni_raid(nd, size, v, 1);
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

void speed_genz(int nd, void** v, int size, int delta, int period)
{
	struct timeval start;
	struct timeval stop;
	int64_t ds;
	int64_t dt;
	int i;
	int count;

	/* Vandermonde table */
	printf("Vandermonde functions used for computing parity with 'sync':\n");
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
		raid_genz_int32_raid(nd, size, v, 1);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);

	SPEED_START {
		raid_genz_int64_raid(nd, size, v, 1);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);

#ifdef CONFIG_NEON
	SPEED_START {
		raid_genz_neon_raid(nd, size, v, 1);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif
#ifdef CONFIG_NEON32
	SPEED_START {
		raid_genz_neon32_raid(nd, size, v, 1);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif

#ifdef CONFIG_X86
	if (raid_cpu_has_sse2()) {
		SPEED_START {
			raid_genz_sse2_raid(nd, size, v, 1);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);

#ifdef CONFIG_X86_64
		SPEED_START {
			raid_genz_sse2ext_raid(nd, size, v, 1);
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
			raid_genz_avx2ext_raid(nd, size, v, 1);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
#endif
	}
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
	printf("RAID polynomial functions used for recovering with 'fix':\n");
	printf("%8s", "");
	printf("%8s", "best");
	printf("%8s", "int");
#ifdef CONFIG_NEON
	printf("%8s", "neon");
#endif
#ifdef CONFIG_NEON32
	printf("%8s", "neon32");
#endif
#ifdef CONFIG_X86
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

	/* REC1OF1 */
	printf("%8s", "rec1of1");
	printf("%8s", raid_rec_tag(RAID_ALGO_CAUCHY_PAR1));
	fflush(stdout);

	SPEED_START {
		/* force the parity generator used by raid_rec1of1() */
		raid_gen_force(1, sizeof(void*) == 8 ? raid_gen1_int64 : raid_gen1_int32);
		raid_rec1_int8(1, id, ip, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);

#ifdef CONFIG_NEON
	SPEED_START {
		/* force the parity generator used by raid_rec1of1() */
		raid_gen_force(1, raid_gen1_neon);
		raid_rec1_neon(1, id, ip, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif
#ifdef CONFIG_NEON32
	SPEED_START {
		/* force the parity generator used by raid_rec1of1() */
		raid_gen_force(1, raid_gen1_neon32);
		raid_rec1_neon32(1, id, ip, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif

#ifdef CONFIG_X86
	if (raid_cpu_has_ssse3()) {
		SPEED_START {
			/* force the parity generator used by raid_rec1of1() */
			raid_gen_force(1, raid_gen1_sse2);
			raid_rec1_ssse3(1, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);

#ifdef CONFIG_X86_64
		SPEED_START {
			/* force the parity generator used by raid_rec1of1() */
			raid_gen_force(1, raid_gen1_sse2);
			raid_rec1_ssse3ext(1, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
#endif
	}
	if (raid_cpu_has_avx2()) {
		SPEED_START {
			/* force the parity generator used by raid_rec1of1() */
			raid_gen_force(1, raid_gen1_avx2);
			raid_rec1_avx2(1, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#ifdef CONFIG_X86_64
	if (raid_cpu_has_avx2()) {
		SPEED_START {
			/* force the parity generator used by raid_rec1of1() */
			raid_gen_force(1, raid_gen1_avx2);
			raid_rec1_avx2ext(1, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx512bw()) {
		SPEED_START {
			/* force the parity generator used by raid_rec1of1() */
			raid_gen_force(1, raid_gen1_avx512bw);
			raid_rec1_avx512bw(1, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx2gfni()) {
		SPEED_START {
			/* force the parity generator used by raid_rec1of1() */
			raid_gen_force(1, raid_gen1_avx2);
			raid_rec1_avx2gfni_raid(1, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx512gfni()) {
		SPEED_START {
			/* force the parity generator used by raid_rec1of1() */
			raid_gen_force(1, raid_gen1_avx512bw);
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
		/* +1 to avoid GEN1 optimized case */
		raid_rec1_int8(1, id, ip + 1, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);

#ifdef CONFIG_NEON
	SPEED_START {
		/* +1 to avoid GEN1 optimized case */
		raid_rec1_neon(1, id, ip + 1, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif
#ifdef CONFIG_NEON32
	SPEED_START {
		/* +1 to avoid GEN1 optimized case */
		raid_rec1_neon32(1, id, ip + 1, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif

#ifdef CONFIG_X86
	if (raid_cpu_has_ssse3()) {
		SPEED_START {
			/* +1 to avoid GEN1 optimized case */
			raid_rec1_ssse3(1, id, ip + 1, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);

#ifdef CONFIG_X86_64
		SPEED_START {
			/* +1 to avoid GEN1 optimized case */
			raid_rec1_ssse3ext(1, id, ip + 1, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
#endif
	}
	if (raid_cpu_has_avx2()) {
		SPEED_START {
			/* +1 to avoid GEN1 optimized case */
			raid_rec1_avx2(1, id, ip + 1, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#ifdef CONFIG_X86_64
	if (raid_cpu_has_avx2()) {
		SPEED_START {
			/* +1 to avoid GEN1 optimized case */
			raid_rec1_avx2ext(1, id, ip + 1, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx512bw()) {
		SPEED_START {
			/* +1 to avoid RAID5 optimized case */
			raid_rec1_avx512bw(1, id, ip + 1, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx2gfni()) {
		SPEED_START {
			/* +1 to avoid RAID5 optimized case */
			raid_rec1_avx2gfni_raid(1, id, ip + 1, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx512gfni()) {
		SPEED_START {
			/* +1 to avoid RAID5 optimized case */
			raid_rec1_avx512gfni_raid(1, id, ip + 1, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#endif
#endif
	printf("\n");

	/* REC1OF3 */
	printf("%8s", "rec1of3");
	printf("%8s", raid_rec_tag(RAID_ALGO_CAUCHY_PAR1));
	fflush(stdout);

	SPEED_START {
		/* +2 to select R instead of Q */
		raid_rec1_int8(1, id, ip + 2, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);

#ifdef CONFIG_NEON
	SPEED_START {
		/* +2 to select R instead of Q */
		raid_rec1_neon(1, id, ip + 2, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif
#ifdef CONFIG_NEON32
	SPEED_START {
		/* +2 to select R instead of Q */
		raid_rec1_neon32(1, id, ip + 2, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif

#ifdef CONFIG_X86
	if (raid_cpu_has_ssse3()) {
		SPEED_START {
			/* +2 to select R instead of Q */
			raid_rec1_ssse3(1, id, ip + 2, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);

#ifdef CONFIG_X86_64
		SPEED_START {
			/* +2 to select R instead of Q */
			raid_rec1_ssse3ext(1, id, ip + 2, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
#endif
	}
	if (raid_cpu_has_avx2()) {
		SPEED_START {
			/* +2 to select R instead of Q */
			raid_rec1_avx2(1, id, ip + 2, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#ifdef CONFIG_X86_64
	if (raid_cpu_has_avx2()) {
		SPEED_START {
			/* +2 to select R instead of Q */
			raid_rec1_avx2ext(1, id, ip + 2, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx512bw()) {
		SPEED_START {
			/* +2 to select R instead of Q */
			raid_rec1_avx512bw(1, id, ip + 2, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx2gfni()) {
		SPEED_START {
			/* +2 to select R instead of Q */
			raid_rec1_avx2gfni_raid(1, id, ip + 2, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx512gfni()) {
		SPEED_START {
			/* +2 to select R instead of Q */
			raid_rec1_avx512gfni_raid(1, id, ip + 2, nd, size, v);
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
		raid_rec2_int8(2, id, ip, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);

#ifdef CONFIG_NEON
	SPEED_START {
		raid_rec2_neon(2, id, ip, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif
#ifdef CONFIG_NEON32
	SPEED_START {
		raid_rec2_neon32(2, id, ip, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif

#ifdef CONFIG_X86
	if (raid_cpu_has_ssse3()) {
		SPEED_START {
			raid_rec2_ssse3(2, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);

#ifdef CONFIG_X86_64
		SPEED_START {
			raid_rec2_ssse3ext(2, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
#endif
	}
	if (raid_cpu_has_avx2()) {
		SPEED_START {
			raid_rec2_avx2(2, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#ifdef CONFIG_X86_64
	if (raid_cpu_has_avx2()) {
		SPEED_START {
			raid_rec2_avx2ext(2, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx512bw()) {
		SPEED_START {
			raid_rec2_avx512bw(2, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx2gfni()) {
		SPEED_START {
			raid_rec2_avx2gfni_raid(2, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx512gfni()) {
		SPEED_START {
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
		/* +1 to avoid GEN2 optimized case */
		raid_rec2_int8(2, id, ip + 1, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);

#ifdef CONFIG_NEON
	SPEED_START {
		/* +1 to avoid GEN2 optimized case */
		raid_rec2_neon(2, id, ip + 1, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif
#ifdef CONFIG_NEON32
	SPEED_START {
		/* +1 to avoid GEN2 optimized case */
		raid_rec2_neon32(2, id, ip + 1, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif

#ifdef CONFIG_X86
	if (raid_cpu_has_ssse3()) {
		SPEED_START {
			/* +1 to avoid GEN2 optimized case */
			raid_rec2_ssse3(2, id, ip + 1, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);

#ifdef CONFIG_X86_64
		SPEED_START {
			/* +1 to avoid GEN2 optimized case */
			raid_rec2_ssse3ext(2, id, ip + 1, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
#endif
	}
	if (raid_cpu_has_avx2()) {
		SPEED_START {
			/* +1 to avoid GEN2 optimized case */
			raid_rec2_avx2(2, id, ip + 1, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#ifdef CONFIG_X86_64
	if (raid_cpu_has_avx2()) {
		SPEED_START {
			/* +1 to avoid GEN2 optimized case */
			raid_rec2_avx2ext(2, id, ip + 1, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx512bw()) {
		SPEED_START {
			/* +1 to avoid GEN2 optimized case */
			raid_rec2_avx512bw(2, id, ip + 1, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx2gfni()) {
		SPEED_START {
			/* +1 to avoid GEN2 optimized case */
			raid_rec2_avx2gfni_raid(2, id, ip + 1, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx512gfni()) {
		SPEED_START {
			/* +1 to avoid GEN2 optimized case */
			raid_rec2_avx512gfni_raid(2, id, ip + 1, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#endif
#endif
	printf("\n");

	/* REC2OF4 */
	printf("%8s", "rec2of4");
	printf("%8s", raid_rec_tag(RAID_ALGO_CAUCHY_PAR2));
	fflush(stdout);

	SPEED_START {
		/* +2 to select R,S instead of Q,R */
		raid_rec2_int8(2, id, ip + 2, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);

#ifdef CONFIG_NEON
	SPEED_START {
		/* +2 to select R,S instead of Q,R */
		raid_rec2_neon(2, id, ip + 2, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif
#ifdef CONFIG_NEON32
	SPEED_START {
		/* +2 to select R,S instead of Q,R */
		raid_rec2_neon32(2, id, ip + 2, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif

#ifdef CONFIG_X86
	if (raid_cpu_has_ssse3()) {
		SPEED_START {
			/* +2 to select R,S instead of Q,R */
			raid_rec2_ssse3(2, id, ip + 2, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);

#ifdef CONFIG_X86_64
		SPEED_START {
			/* +2 to select R,S instead of Q,R */
			raid_rec2_ssse3ext(2, id, ip + 2, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
#endif
	}
	if (raid_cpu_has_avx2()) {
		SPEED_START {
			/* +2 to select R,S instead of Q,R */
			raid_rec2_avx2(2, id, ip + 2, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#ifdef CONFIG_X86_64
	if (raid_cpu_has_avx2()) {
		SPEED_START {
			/* +2 to select R,S instead of Q,R */
			raid_rec2_avx2ext(2, id, ip + 2, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx512bw()) {
		SPEED_START {
			/* +2 to select R,S instead of Q,R */
			raid_rec2_avx512bw(2, id, ip + 2, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx2gfni()) {
		SPEED_START {
			/* +2 to select R,S instead of Q,R */
			raid_rec2_avx2gfni_raid(2, id, ip + 2, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx512gfni()) {
		SPEED_START {
			/* +2 to select R,S instead of Q,R */
			raid_rec2_avx512gfni_raid(2, id, ip + 2, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#endif
#endif
	printf("\n");

	printf("%8s", "rec3of3");
	printf("%8s", raid_rec_tag(RAID_ALGO_CAUCHY_PAR3));
	fflush(stdout);

	SPEED_START {
		raid_rec3_int8(3, id, ip, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);

#ifdef CONFIG_NEON
	SPEED_START {
		raid_rec3_neon(3, id, ip, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif
#ifdef CONFIG_NEON32
	SPEED_START {
		raid_rec3_neon32(3, id, ip, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif

#ifdef CONFIG_X86
	if (raid_cpu_has_ssse3()) {
		SPEED_START {
			raid_rec3_ssse3(3, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);

#ifdef CONFIG_X86_64
		SPEED_START {
			raid_rec3_ssse3ext(3, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
#endif
	}
	if (raid_cpu_has_avx2()) {
		SPEED_START {
			raid_rec3_avx2(3, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#ifdef CONFIG_X86_64
	if (raid_cpu_has_avx2()) {
		SPEED_START {
			raid_rec3_avx2ext(3, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx512bw()) {
		SPEED_START {
			raid_rec3_avx512bw(3, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx2gfni()) {
		SPEED_START {
			raid_rec3_avx2gfni_raid(3, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx512gfni()) {
		SPEED_START {
			raid_rec3_avx512gfni_raid(3, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#endif
#endif
	printf("\n");

	printf("%8s", "rec4of4");
	printf("%8s", raid_rec_tag(RAID_ALGO_CAUCHY_PAR4));
	fflush(stdout);

	SPEED_START {
		raid_rec4_int8(4, id, ip, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);

#ifdef CONFIG_NEON
	SPEED_START {
		raid_rec4_neon(4, id, ip, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif
#ifdef CONFIG_NEON32
	SPEED_START {
		raid_rec4_neon32(4, id, ip, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif

#ifdef CONFIG_X86
	if (raid_cpu_has_ssse3()) {
		SPEED_START {
			raid_rec4_ssse3(4, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);

#ifdef CONFIG_X86_64
		SPEED_START {
			raid_rec4_ssse3ext(4, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
#endif
	}
	if (raid_cpu_has_avx2()) {
		SPEED_START {
			raid_rec4_avx2(4, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#ifdef CONFIG_X86_64
	if (raid_cpu_has_avx2()) {
		SPEED_START {
			raid_rec4_avx2ext(4, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx512bw()) {
		SPEED_START {
			raid_rec4_avx512bw(4, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx2gfni()) {
		SPEED_START {
			raid_rec4_avx2gfni_raid(4, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx512gfni()) {
		SPEED_START {
			raid_rec4_avx512gfni_raid(4, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#endif
#endif
	printf("\n");

	printf("%8s", "rec5of5");
	printf("%8s", raid_rec_tag(RAID_ALGO_CAUCHY_PAR5));
	fflush(stdout);

	SPEED_START {
		raid_rec5_int8(5, id, ip, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);

#ifdef CONFIG_NEON
	SPEED_START {
		raid_rec5_neon(5, id, ip, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif
#ifdef CONFIG_NEON32
	SPEED_START {
		raid_rec5_neon32(5, id, ip, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif

#ifdef CONFIG_X86
	if (raid_cpu_has_ssse3()) {
		SPEED_START {
			raid_rec5_ssse3(5, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);

#ifdef CONFIG_X86_64
		SPEED_START {
			raid_rec5_ssse3ext(5, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
#endif
	}
	if (raid_cpu_has_avx2()) {
		SPEED_START {
			raid_rec5_avx2(5, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#ifdef CONFIG_X86_64
	if (raid_cpu_has_avx2()) {
		SPEED_START {
			raid_rec5_avx2ext(5, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx512bw()) {
		SPEED_START {
			raid_rec5_avx512bw(5, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx2gfni()) {
		SPEED_START {
			raid_rec5_avx2gfni_raid(5, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx512gfni()) {
		SPEED_START {
			raid_rec5_avx512gfni_raid(5, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#endif
#endif
	printf("\n");

	printf("%8s", "rec6of6");
	printf("%8s", raid_rec_tag(RAID_ALGO_CAUCHY_PAR6));
	fflush(stdout);

	SPEED_START {
		raid_rec6_int8(6, id, ip, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);

#ifdef CONFIG_NEON
	SPEED_START {
		raid_rec6_neon(6, id, ip, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif
#ifdef CONFIG_NEON32
	SPEED_START {
		raid_rec6_neon32(6, id, ip, nd, size, v);
	} SPEED_STOP

	printf("%8" PRIu64, ds / dt);
	fflush(stdout);
#endif

#ifdef CONFIG_X86
	if (raid_cpu_has_ssse3()) {
		SPEED_START {
			raid_rec6_ssse3(6, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);

#ifdef CONFIG_X86_64
		SPEED_START {
			raid_rec6_ssse3ext(6, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
#endif
	}
	if (raid_cpu_has_avx2()) {
		SPEED_START {
			raid_rec6_avx2(6, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
#ifdef CONFIG_X86_64
	if (raid_cpu_has_avx2()) {
		SPEED_START {
			raid_rec6_avx2ext(6, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx512bw()) {
		SPEED_START {
			raid_rec6_avx512bw(6, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx2gfni()) {
		SPEED_START {
			raid_rec6_avx2gfni_raid(6, id, ip, nd, size, v);
		} SPEED_STOP

		printf("%8" PRIu64, ds / dt);
		fflush(stdout);
	}
	if (raid_cpu_has_avx512gfni()) {
		SPEED_START {
			raid_rec6_avx512gfni_raid(6, id, ip, nd, size, v);
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

void speed_affinity(void)
{
#if HAVE_LINUX_DEVICE
	int num_cpus = sysconf(_SC_NPROCESSORS_ONLN);
	if (num_cpus <= 0)
		num_cpus = sysconf(_SC_NPROCESSORS_CONF);
	if (num_cpus <= 0)
		return;

	printf("Machine has %d cores\n", num_cpus);

	int cpu = os_get_optimal_cpu();
	if (cpu < 0 || cpu >= CPU_SETSIZE)
		return;

	cpu_set_t mask;
	CPU_ZERO(&mask);
	CPU_SET(cpu, &mask);

	if (sched_setaffinity(0, sizeof(cpu_set_t), &mask) == 0) {
		printf("Running on cpu %d\n", cpu);
	}
#endif
}

#if defined(__aarch64__) && defined(__APPLE__)
static int check_cpu_feature(const char* name)
{
	int value = 0;
	size_t len = sizeof(value);

	if (sysctlbyname(name, &value, &len, 0, 0) == 0) {
		return value;
	}

	return 0;
}

static const char* decode_cpufamily(uint32_t family)
{
	switch (family) {
	case 0xe81e7ef6 : return "Monsoon/Mistral (A11)";
	case 0x07d34b9f : return "Vortex/Tempest (A12)";
	case 0x462504d2 : return "Lightning/Thunder (A13)";
	case 0x1b588bb3 : return "Firestorm/Icestorm (M1/A14)";
	case 0xda33d83d : return "Blizzard/Avalanche (M2/A15)";
	case 0x8765edea : return "Everest/Sawtooth (A16)";
	case 0xfa33415e : return "Ibiza (M3)";
	case 0x72015832 : return "Palma (M3 Pro)";
	case 0x17d5e360 : return "Lobos (M3 Max)";
	case 0x2876f5b5 : return "Coll (A17 Pro)";
	case 0x6f5129ac : return "Donan (M4)";
	case 0x1774f74e : return "Brava (M4 Pro)";
	case 0x524c529b : return "Hidra (M4 Max)";
	case 0x7843b821 : return "Tahiti (A18)";
	case 0x6f64249a : return "Tupai (A18 Pro)";
	case 0 : return "Unavailable";
	default : return "Unknown";
	}
}

void print_apple(const struct machineinfo_struct* info)
{
	uint32_t family = 0;
	size_t len = sizeof(uint32_t);
	sysctlbyname("hw.cpufamily", &family, &len, 0, 0);

	int has_aes = check_cpu_feature("hw.optional.arm.FEAT_AES");
	int has_dotprod = check_cpu_feature("hw.optional.arm.FEAT_DotProd");
	int has_sha3 = check_cpu_feature("hw.optional.arm.FEAT_SHA3");
	int has_sve = check_cpu_feature("hw.optional.arm.FEAT_SVE");
	int has_sme = check_cpu_feature("hw.optional.arm.FEAT_SME");

	const char* name = info->cpu_brand[0] ? info->cpu_brand : (info->system_model[0] ? info->system_model : "Apple Silicon");

	printf("CPU %s, family %s (0x%08x), flags%s%s%s%s%s\n",
		name, decode_cpufamily(family), family,
		has_aes ? " aes" : "",
		has_dotprod ? " dotprod" : "",
		has_sha3 ? " sha3" : "",
		has_sve ? " sve" : "",
		has_sme ? " sme" : ""
	);
}
#endif

#ifdef CONFIG_X86
void print_intel(const struct machineinfo_struct* info)
{
	printf("CPU %s, family %u (0x%x), model %u (0x%x), stepping %u, flags%s%s%s%s%s%s%s%s%s%s\n",
		info->cpu_vendor[0] ? info->cpu_vendor : "x86",
		info->cpu_family, info->cpu_family,
		info->cpu_model, info->cpu_model,
		info->cpu_stepping,
		raid_cpu_has_sse2() ? " sse2" : "",
		raid_cpu_has_ssse3() ? " ssse3" : "",
		raid_cpu_has_crc32() ? " crc32" : "",
		raid_cpu_has_avx2() ? " avx2" : "",
		raid_cpu_has_avx2gfni() ? " avx2gfni" : "",
		raid_cpu_has_avx512bw() ? " avx512bw" : "",
		raid_cpu_has_avx512gfni() ? " avx512gfni" : "",
		raid_cpu_has_slowmult() ? " slowmult" : "",
		raid_cpu_has_slow_extendedreg() ? " slowext" : "",
		raid_cpu_has_avx512bw() && raid_cpu_has_slow_avx512() ? " slowavx512" : ""
	);
}
#endif

void speed(int period, int nd, int size)
{
	int i;
	int delta;
	int nv;
	void* v_alloc;
	void** v;
	struct machineinfo_struct minfo;

	machineinfo(&minfo);

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

	nv = nd + RAID_PARITY_MAX;

	v = malloc_nofail_vector_align(nv, size, &v_alloc);

	/* initialize disks with fixed data */
	for (i = 0; i < nd; ++i)
		memset(v[i], i, size);

	printf(PACKAGE " v" VERSION " by Andrea Mazzoleni, " PACKAGE_URL "\n");

#ifdef __GNUC__
	printf("Compiler gcc " __VERSION__ "\n");
#endif

#ifdef CONFIG_X86
	print_intel(&minfo);
#elif defined(__aarch64__) && defined(__APPLE__)
	print_apple(&minfo);
#elif defined(__aarch64__)
#if defined(CONFIG_NEON)
	printf("CPU %s, flags neon\n", minfo.cpu_brand[0] ? minfo.cpu_brand : "64-bit ARM (AArch64)");
#else
	printf("CPU %s\n", minfo.cpu_brand[0] ? minfo.cpu_brand : "64-bit ARM (AArch64)");
#endif
#elif defined(__arm__)
#if defined(CONFIG_NEON32)
	printf("CPU %s, flags neon\n", minfo.cpu_brand[0] ? minfo.cpu_brand : "32-bit ARM");
#else
	printf("CPU %s\n", minfo.cpu_brand[0] ? minfo.cpu_brand : "32-bit ARM");
#endif
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

	if (minfo.cpu_brand[0] != 0) {
		const char* p = minfo.cpu_brand;
		int in_space = 0;

		printf("Model ");
		while (*p != 0) {
			if (*p == ' ') {
				if (!in_space) {
					putchar(' ');
					in_space = 1;
				}
			} else {
				putchar(*p);
				in_space = 0;
			}
			++p;
		}
		printf("\n");
	}

	if (minfo.cpu_clock[0] != 0) {
		printf("Clock %s\n", minfo.cpu_clock);
	}

	if (minfo.cache_l1_data > 0 || minfo.cache_l2 > 0) {
		printf("Cache L1 Data %" PRIu64 " KB, L2 %" PRIu64 " KB",
			minfo.cache_l1_data / 1024, minfo.cache_l2 / 1024);
		if (minfo.cache_l3 > 0) {
			printf(", L3 %" PRIu64 " KB", minfo.cache_l3 / 1024);
		}
		printf("\n");
	}

	speed_affinity();

	if (minfo.memory_total_bytes > 0) {
		printf("Memory %" PRIu64 " MB", minfo.memory_total_bytes / (1024 * 1024));
#if WORDS_BIGENDIAN
		printf(", big-endian %d-bit", (int)sizeof(void*) * 8);
#else
		printf(", little-endian %d-bit", (int)sizeof(void*) * 8);
#endif
		if (minfo.memory_page_size > 0)
			printf(", page %u KB", minfo.memory_page_size / 1024);
		printf("\n");
	} else {
#if WORDS_BIGENDIAN
		printf("Memory is big-endian %d-bit\n", (int)sizeof(void*) * 8);
#else
		printf("Memory is little-endian %d-bit\n", (int)sizeof(void*) * 8);
#endif
	}

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

	if (minfo.os_distribution[0] != 0 && minfo.os_name[0] != 0) {
		printf("Host %s (%s)\n", minfo.os_distribution, minfo.os_name);
	} else if (minfo.os_name[0] != 0) {
		printf("Host %s\n", minfo.os_name);
	}

	if (minfo.system_model[0] != 0 || minfo.hypervisor[0] != 0) {
		printf("System %s%s%s%s\n",
			minfo.system_model[0] ? minfo.system_model : "Generic",
			minfo.hypervisor[0] ? " [" : "",
			minfo.hypervisor[0] ? minfo.hypervisor : "",
			minfo.hypervisor[0] ? "]" : "");
	}

	printf("\n");

	printf("Speed test using %u data buffers of %u bytes, for a total of %u KiB.\n", nd, size, nd * size / KIBI);
	printf("Memory blocks have a displacement of %u bytes to improve cache performance.\n", raid_optimal_displacement(nv));
	printf("The reported values are the aggregate bandwidth of all data blocks in MB/s,\n");
	printf("not counting parity blocks.\n");
	printf("\n");

	speed_mem(nd, v, size, delta, period);
	speed_crc(nd, v, size, delta, period);
	speed_hash(nd, v, size, delta, period);

	raid_mode(RAID_MODE_CAUCHY_RAID);
	speed_gen(nd, v, size, delta, period, "RAID polynomial");

	raid_mode(RAID_MODE_CAUCHY_AES);
	speed_gen(nd, v, size, delta, period, "AES polynomial");

	raid_mode(RAID_MODE_VANDERMONDE_RAID);
	speed_genz(nd, v, size, delta, period);

	raid_mode(RAID_MODE_CAUCHY_RAID);
	speed_rec(nd, v, size, delta, period);

	printf("If the 'best' expectations are wrong, please report it at:\n\n");
	printf("    https://github.com/amadvance/snapraid/issues/64\n\n");

	free(v_alloc);
	free(v);
}

