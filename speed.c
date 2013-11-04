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
#include "state.h"

/**
 * Differential us of two timeval.
 */
static int64_t diffgettimeofday(struct timeval* start, struct timeval* stop)
{
	return 1000000LL * (stop->tv_sec - start->tv_sec) + stop->tv_usec - start->tv_usec;
}

/**
 * Start time measurement.
 */
#define SPEED_START \
	count = 0; \
	gettimeofday(&start, 0); \
	do { \
		for(i=0;i<delta;++i)

/**
 * Stop time measurement.
 */
#define SPEED_STOP \
		count += delta; \
		gettimeofday(&stop, 0); \
	} while (diffgettimeofday(&start, &stop) < 1000000LL); \
	ds = block_size * (int64_t)count * diskmax; \
	dt = diffgettimeofday(&start, &stop);

void speed(void)
{
	struct timeval start;
	struct timeval stop;
	int64_t ds;
	int64_t dt;
	unsigned i, j;
	unsigned char digest[HASH_SIZE];
	unsigned char seed[HASH_SIZE];
	int d[LEV_MAX];
	int e[LEV_MAX];
	unsigned count;
	unsigned delta = 10;
	unsigned block_size = 256 * 1024;
	unsigned diskmax = 8;
	unsigned buffermax;
	void* buffer_alloc;
	unsigned char** buffer;
	unsigned char* zero;

	/* we need disk + 1 for each parity level buffers + 1 zero buffer */
	buffermax = diskmax + LEV_MAX + 1;

	buffer = malloc_nofail_vector_align(diskmax, buffermax, block_size, &buffer_alloc);
	mtest_vector(buffer, buffermax, block_size);

	/* initialize disks with fixed data */
	for(i=0;i<diskmax;++i) {
		memset(buffer[i], i, block_size);
	}

	/* zero buffer */
	zero = buffer[diskmax+LEV_MAX];
	memset(zero, 0, block_size);

	/* hash seed */
	for(i=0;i<HASH_SIZE;++i) {
		seed[i] = i;
	}

	/* basic disks and parity mapping */
	for(i=0;i<LEV_MAX;++i) {
		d[i] = i;
		e[i] = i;
	}

	printf(PACKAGE " v" VERSION " by Andrea Mazzoleni, " PACKAGE_URL "\n");

#ifdef __GNUC__
	printf("Compiler gcc " __VERSION__ "\n");
#endif

#if defined(__i386__) || defined(__x86_64__)
	{
		char vendor[CPU_VENDOR_MAX];
		unsigned family;
		unsigned model;

		cpu_info(vendor, &family, &model);

		printf("CPU %s, family %u, model %u, flags%s%s%s%s%s%s%s\n", vendor, family, model,
			cpu_has_mmx() ? " mmx" : "",
			cpu_has_sse2() ? " sse2" : "",
			cpu_has_ssse3() ? " ssse3" : "",
			cpu_has_sse42() ? " sse42" : "",
			cpu_has_avx() ? " avx" : "",
			cpu_has_slowmult() ? " slowmult" : "",
			cpu_has_slowpshufb()  ? " slowpshufb" : ""
			);
	}
#else
	printf("CPU is not a x86/x64\n");
#endif
#if WORDS_BIGENDIAN
	printf("Memory is big-endian %u-bit\n", (unsigned)sizeof(void*) * 8);
#else
	printf("Memory is little-endian %u-bit\n", (unsigned)sizeof(void*) * 8);
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

	printf("Speed test using %u buffers of %u bytes, for a total of %u KiB.\n", diskmax, block_size, diskmax * block_size / 1024);
	printf("All values are in MiB/s of data processed, not counting written parity.\n");
	printf("\n");

	printf("Memory write speed using the C memset() function:\n");
	printf("%8s", "memset");
	fflush(stdout);

	SPEED_START {
		for(j=0;j<diskmax;++j) {
			memset(buffer[j], j, block_size);
		}
	} SPEED_STOP

	printf("%8"PRIu64, ds / dt);
	printf("\n");
	printf("\n");

	/* crc table */
	printf("CRC used to check the content file integrity:\n");

	printf("%8s", "table");
	fflush(stdout);

	SPEED_START {
		for(j=0;j<diskmax;++j) {
			crc32c_gen(0, buffer[j], block_size);
		}
	} SPEED_STOP

	printf("%8"PRIu64, ds / dt);
	printf("\n");

	printf("%8s", "intel");
	fflush(stdout);

#if defined(__i386__) || defined(__x86_64__)
	if (cpu_has_sse42()) {
		SPEED_START {
			for(j=0;j<diskmax;++j) {
				crc32c_x86(0, buffer[j], block_size);
			}
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
#if defined(__i386__) || defined(__x86_64__)
	if (sizeof(void*) == 4 && !cpu_has_slowmult())
		printf("%8s", "murmur3");
	else
		printf("%8s", "spooky2");
#else
	if (sizeof(void*) == 4)
		printf("%8s", "murmur3");
	else
		printf("%8s", "spooky2");
#endif
	fflush(stdout);

	SPEED_START {
		for(j=0;j<diskmax;++j) {
			memhash(HASH_MURMUR3, seed, digest, buffer[j], block_size);
		}
	} SPEED_STOP

	printf("%8"PRIu64, ds / dt);
	fflush(stdout);

	SPEED_START {
		for(j=0;j<diskmax;++j) {
			memhash(HASH_SPOOKY2, seed, digest, buffer[j], block_size);
		}
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
#if defined(__i386__) || defined(__x86_64__)
	printf("%8s", "sse2");
#if defined(__x86_64__)
	printf("%8s", "sse2e");
#endif
	printf("%8s", "ssse3");
#if defined(__x86_64__)
	printf("%8s", "ssse3e");
#endif
#endif
	printf("\n");

	/* RAID5 */
	printf("%8s", "raid5");
	printf("%8s", raid5_tag());
	fflush(stdout);

	printf("%8s", "");

	SPEED_START {
		raid5_int32(buffer, diskmax, block_size);
	} SPEED_STOP

	printf("%8"PRIu64, ds / dt);
	fflush(stdout);

	SPEED_START {
		raid5_int64(buffer, diskmax, block_size);
	} SPEED_STOP

	printf("%8"PRIu64, ds / dt);
	fflush(stdout);

#if defined(__i386__) || defined(__x86_64__)
	if (cpu_has_sse2()) {
		SPEED_START {
			raid5_sse2(buffer, diskmax, block_size);
		} SPEED_STOP

		printf("%8"PRIu64, ds / dt);
		fflush(stdout);
	}
#endif
	printf("\n");

	/* RAID6 */
	printf("%8s", "raid6");
	printf("%8s", raid6_tag());
	fflush(stdout);

	printf("%8s", "");

	SPEED_START {
		raid6_int32(buffer, diskmax, block_size);
	} SPEED_STOP

	printf("%8"PRIu64, ds / dt);
	fflush(stdout);

	SPEED_START {
		raid6_int64(buffer, diskmax, block_size);
	} SPEED_STOP

	printf("%8"PRIu64, ds / dt);
	fflush(stdout);

#if defined(__i386__) || defined(__x86_64__)
	if (cpu_has_sse2()) {
		SPEED_START {
			raid6_sse2(buffer, diskmax, block_size);
		} SPEED_STOP

		printf("%8"PRIu64, ds / dt);
		fflush(stdout);

#if defined(__x86_64__)
		SPEED_START {
			raid6_sse2ext(buffer, diskmax, block_size);
		} SPEED_STOP

		printf("%8"PRIu64, ds / dt);
		fflush(stdout);
#endif
	}
#endif
	printf("\n");

	/* RAIDTP */
	printf("%8s", "raidTP");
	printf("%8s", raidTP_tag());
	fflush(stdout);

	SPEED_START {
		raidTP_int8(buffer, diskmax, block_size);
	} SPEED_STOP

	printf("%8"PRIu64, ds / dt);
	fflush(stdout);

	printf("%8s", "");
	printf("%8s", "");

	if (cpu_has_sse2()) {
		printf("%8s", "");

#if defined(__x86_64__)
		printf("%8s", "");
#endif
	}

#if defined(__i386__) || defined(__x86_64__)
	if (cpu_has_ssse3()) {
		SPEED_START {
			raidTP_ssse3(buffer, diskmax, block_size);
		} SPEED_STOP

		printf("%8"PRIu64, ds / dt);
		fflush(stdout);

#if defined(__x86_64__)
		SPEED_START {
			raidTP_ssse3ext(buffer, diskmax, block_size);
		} SPEED_STOP

		printf("%8"PRIu64, ds / dt);
		fflush(stdout);
#endif
	}
#endif
	printf("\n");

	/* RAIDQP */
	printf("%8s", "raidQP");
	printf("%8s", raidQP_tag());
	fflush(stdout);

	SPEED_START {
		raidQP_int8(buffer, diskmax, block_size);
	} SPEED_STOP

	printf("%8"PRIu64, ds / dt);
	fflush(stdout);

	printf("%8s", "");
	printf("%8s", "");

	if (cpu_has_sse2()) {
		printf("%8s", "");

#if defined(__x86_64__)
		printf("%8s", "");
#endif
	}

#if defined(__i386__) || defined(__x86_64__)
	if (cpu_has_ssse3()) {
		SPEED_START {
			raidQP_ssse3(buffer, diskmax, block_size);
		} SPEED_STOP

		printf("%8"PRIu64, ds / dt);
		fflush(stdout);

#if defined(__x86_64__)
		SPEED_START {
			raidQP_ssse3ext(buffer, diskmax, block_size);
		} SPEED_STOP

		printf("%8"PRIu64, ds / dt);
		fflush(stdout);
#endif
	}
#endif
	printf("\n");
	
	/* RAIDPP */
	printf("%8s", "raidPP");
	printf("%8s", raidPP_tag());
	fflush(stdout);

	SPEED_START {
		raidPP_int8(buffer, diskmax, block_size);
	} SPEED_STOP

	printf("%8"PRIu64, ds / dt);
	fflush(stdout);

	printf("%8s", "");
	printf("%8s", "");

	if (cpu_has_sse2()) {
		printf("%8s", "");

#if defined(__x86_64__)
		printf("%8s", "");
#endif
	}

#if defined(__i386__) || defined(__x86_64__)
	if (cpu_has_ssse3()) {
		SPEED_START {
			raidPP_ssse3(buffer, diskmax, block_size);
		} SPEED_STOP

		printf("%8"PRIu64, ds / dt);
		fflush(stdout);

#if defined(__x86_64__)
		SPEED_START {
			raidPP_ssse3ext(buffer, diskmax, block_size);
		} SPEED_STOP

		printf("%8"PRIu64, ds / dt);
		fflush(stdout);
#endif
	}
#endif
	printf("\n");

	/* RAIDHP */
	printf("%8s", "raidHP");
	printf("%8s", raidHP_tag());
	fflush(stdout);

	SPEED_START {
		raidHP_int8(buffer, diskmax, block_size);
	} SPEED_STOP

	printf("%8"PRIu64, ds / dt);
	fflush(stdout);

	printf("%8s", "");
	printf("%8s", "");

	if (cpu_has_sse2()) {
		printf("%8s", "");

#if defined(__x86_64__)
		printf("%8s", "");
#endif
	}

#if defined(__i386__) || defined(__x86_64__)
	if (cpu_has_ssse3()) {
		SPEED_START {
			raidHP_ssse3(buffer, diskmax, block_size);
		} SPEED_STOP

		printf("%8"PRIu64, ds / dt);
		fflush(stdout);

#if defined(__x86_64__)
		SPEED_START {
			raidHP_ssse3ext(buffer, diskmax, block_size);
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
	printf("%8s", "ssse3");
	printf("\n");

	printf("%8s", "recov1");
	printf("%8s", raid_recov1_tag());
	fflush(stdout);

	SPEED_START {
		for(j=0;j<diskmax;++j) {
			/* +1 to avoid RAID5 optimized case */
			raid_recov1_int8(1, d, e + 1, buffer, diskmax, zero, block_size);
		}
	} SPEED_STOP

	printf("%8"PRIu64, ds / dt);
	fflush(stdout);

	SPEED_START {
		for(j=0;j<diskmax;++j) {
			/* +1 to avoid RAID5 optimized case */
			raid_recov1_ssse3(1, d, e + 1, buffer, diskmax, zero, block_size);
		}
	} SPEED_STOP

	printf("%8"PRIu64, ds / dt);
	printf("\n");

	printf("%8s", "recov2");
	printf("%8s", raid_recov2_tag());
	fflush(stdout);

	SPEED_START {
		for(j=0;j<diskmax;++j) {
			/* +1 to avoid RAID6 optimized case */
			raid_recov2_int8(2, d, e + 1, buffer, diskmax, zero, block_size);
		}
	} SPEED_STOP

	printf("%8"PRIu64, ds / dt);
	fflush(stdout);

	SPEED_START {
		for(j=0;j<diskmax;++j) {
			/* +1 to avoid RAID6 optimized case */
			raid_recov2_ssse3(2, d, e + 1, buffer, diskmax, zero, block_size);
		}
	} SPEED_STOP

	printf("%8"PRIu64, ds / dt);
	printf("\n");

	printf("%8s", "recov3");
	printf("%8s", raid_recovX_tag());
	fflush(stdout);

	SPEED_START {
		for(j=0;j<diskmax;++j) {
			raid_recovX_int8(3, d, e, buffer, diskmax, zero, block_size);
		}
	} SPEED_STOP

	printf("%8"PRIu64, ds / dt);
	fflush(stdout);

	SPEED_START {
		for(j=0;j<diskmax;++j) {
			raid_recovX_ssse3(3, d, e, buffer, diskmax, zero, block_size);
		}
	} SPEED_STOP

	printf("%8"PRIu64, ds / dt);
	printf("\n");

	printf("%8s", "recov4");
	printf("%8s", raid_recovX_tag());
	fflush(stdout);

	SPEED_START {
		for(j=0;j<diskmax;++j) {
			raid_recovX_int8(4, d, e, buffer, diskmax, zero, block_size);
		}
	} SPEED_STOP

	printf("%8"PRIu64, ds / dt);
	fflush(stdout);

	SPEED_START {
		for(j=0;j<diskmax;++j) {
			raid_recovX_ssse3(4, d, e, buffer, diskmax, zero, block_size);
		}
	} SPEED_STOP

	printf("%8"PRIu64, ds / dt);
	printf("\n");

	printf("%8s", "recov5");
	printf("%8s", raid_recovX_tag());
	fflush(stdout);

	SPEED_START {
		for(j=0;j<diskmax;++j) {
			raid_recovX_int8(5, d, e, buffer, diskmax, zero, block_size);
		}
	} SPEED_STOP

	printf("%8"PRIu64, ds / dt);
	fflush(stdout);

	SPEED_START {
		for(j=0;j<diskmax;++j) {
			raid_recovX_ssse3(5, d, e, buffer, diskmax, zero, block_size);
		}
	} SPEED_STOP

	printf("%8"PRIu64, ds / dt);
	printf("\n");

	printf("%8s", "recov6");
	printf("%8s", raid_recovX_tag());
	fflush(stdout);

	SPEED_START {
		for(j=0;j<diskmax;++j) {
			raid_recovX_int8(6, d, e, buffer, diskmax, zero, block_size);
		}
	} SPEED_STOP

	printf("%8"PRIu64, ds / dt);
	fflush(stdout);

	SPEED_START {
		for(j=0;j<diskmax;++j) {
			raid_recovX_ssse3(6, d, e, buffer, diskmax, zero, block_size);
		}
	} SPEED_STOP

	printf("%8"PRIu64, ds / dt);
	printf("\n");
	printf("\n");

	printf("If the 'best' expectations are wrong, please report it in the SnapRAID forum\n\n");

	free(buffer_alloc);
	free(buffer);
}

