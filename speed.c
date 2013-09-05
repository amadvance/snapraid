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

	unsigned count;
	unsigned delta = 100;
	unsigned block_size = 256 * 1024;
	unsigned diskmax = 8;
	void* buffer_alloc;
	unsigned char** buffer;

	buffer = malloc_nofail_vector_align(diskmax + 3, block_size, &buffer_alloc);

	/* initialize with fixed data */
	for(i=0;i<diskmax+3;++i) {
		memset(buffer[i], i, block_size);
	}
	for(i=0;i<HASH_SIZE;++i)
		seed[i] = i;

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

		printf("CPU %s, family %u, model %u, flags%s%s%s\n", vendor, family, model,
			cpu_has_mmx() ? " mmx" : "",
			cpu_has_sse2() ? " sse2" : "",
			cpu_has_slowmult() ? " slowmult" : ""
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

	printf("Expected fastest hash is ");
#if defined(__i386__) || defined(__x86_64__)
	if (sizeof(void*) == 4 && !cpu_has_slowmult())
		printf("Murmur3");
	else
		printf("Spooky2");
#else
	if (sizeof(void*) == 4)
		printf("Murmur3");
	else
		printf("Spooky2");
#endif
	printf("\n");

#if defined(__i386__) || defined(__x86_64__)
	printf("Expected fastest RAID5 is ");
	if (cpu_has_sse2())
#if defined(__x86_64__)
		printf("sse2x8");
#else
		printf("sse2x4");
#endif
	else if (cpu_has_mmx())
		printf("mmxx4");
	else
		printf("int32x2");
	printf("\n");
	printf("Expected fastest RAID6 is ");
	if (cpu_has_sse2())
#if defined(__x86_64__)
		printf("sse2x4");
#else
		printf("sse2x2");
#endif
	else if (cpu_has_mmx())
		printf("mmxx2");
	else
		printf("int32x2");
	printf("\n");
#endif
	printf("If these expectations are false, please report it in the SnapRAID forum\n");

	printf("\n");

	printf("Speed test with %d disk and %d buffer size, for a total of %u KiB...\n", diskmax, block_size, (diskmax + 2) * block_size / 1024);

	SPEED_START {
		for(j=0;j<diskmax;++j) {
			memset(buffer[j], j, block_size);
		}
	} SPEED_STOP

	printf("memset0 %"PRIu64" [MB/s]\n", ds / dt);

	SPEED_START {
		for(j=0;j<diskmax;++j) {
			crc32c_gen(0, buffer[j], block_size);
		}
	} SPEED_STOP

	printf("CRC table %"PRIu64" [MB/s]\n", ds / dt);

#if defined(__i386__) || defined(__x86_64__)
	if (cpu_has_sse42()) {
		SPEED_START {
			for(j=0;j<diskmax;++j) {
				crc32c_x86(0, buffer[j], block_size);
			}
		} SPEED_STOP

		printf("CRC intel-crc32 %"PRIu64" [MB/s]\n", ds / dt);
	}
#endif
	SPEED_START {
		for(j=0;j<diskmax;++j) {
			memhash(HASH_MURMUR3, seed, digest, buffer[j], block_size);
		}
	} SPEED_STOP

	printf("HASH Murmur3 %"PRIu64" [MB/s]\n", ds / dt);

	SPEED_START {
		for(j=0;j<diskmax;++j) {
			memhash(HASH_SPOOKY2, seed, digest, buffer[j], block_size);
		}
	} SPEED_STOP

	printf("HASH Spooky2 %"PRIu64" [MB/s]\n", ds / dt);

	SPEED_START {
		raid5_int32r2(buffer, diskmax, block_size);
	} SPEED_STOP

	printf("RAID5 int32x2 %"PRIu64" [MB/s]\n", ds / dt);

#if defined(__i386__) || defined(__x86_64__)
	if (cpu_has_mmx()) {
		SPEED_START {
			raid5_mmxr2(buffer, diskmax, block_size);
		} SPEED_STOP

		printf("RAID5 mmxx2 %"PRIu64" [MB/s]\n", ds / dt);

		SPEED_START {
			raid5_mmxr4(buffer, diskmax, block_size);
		} SPEED_STOP

		printf("RAID5 mmxx4 %"PRIu64" [MB/s]\n", ds / dt);
	}

	if (cpu_has_sse2()) {
		SPEED_START {
			raid5_sse2r2(buffer, diskmax, block_size);
		} SPEED_STOP

		printf("RAID5 sse2x2 %"PRIu64" [MB/s]\n", ds / dt);

		SPEED_START {
			raid5_sse2r4(buffer, diskmax, block_size);
		} SPEED_STOP

		printf("RAID5 sse2x4 %"PRIu64" [MB/s]\n", ds / dt);

#if defined(__x86_64__)
		SPEED_START {
			raid5_sse2r8(buffer, diskmax, block_size);
		} SPEED_STOP

		printf("RAID5 sse2x8 %"PRIu64" [MB/s]\n", ds / dt);
#endif
	}
#endif

	SPEED_START {
		raid6_int32r2(buffer, diskmax, block_size);
	} SPEED_STOP

	printf("RAID6 int32x2 %"PRIu64" [MB/s]\n", ds / dt);

#if defined(__i386__) || defined(__x86_64__)
	if (cpu_has_mmx()) {
		SPEED_START {
			raid6_mmxr2(buffer, diskmax, block_size);
		} SPEED_STOP

		printf("RAID6 mmxx2 %"PRIu64" [MB/s]\n", ds / dt);
	}

	if (cpu_has_sse2()) {
		SPEED_START {
			raid6_sse2r2(buffer, diskmax, block_size);
		} SPEED_STOP

		printf("RAID6 sse2x2 %"PRIu64" [MB/s]\n", ds / dt);

#if defined(__x86_64__)
		SPEED_START {
			raid6_sse2r4(buffer, diskmax, block_size);
		} SPEED_STOP

		printf("RAID6 sse2x4 %"PRIu64" [MB/s]\n", ds / dt);
#endif
	}
#endif

	free(buffer_alloc);
	free(buffer);
}

