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

	unsigned count;
	unsigned delta = 100;
	unsigned block_size = 256 * 1024;
	unsigned diskmax = 4;
	void* buffer_alloc;
	unsigned char* buffer_aligned;
	unsigned char** buffer;

	buffer_aligned = malloc_nofail_align((diskmax + 2) * block_size, &buffer_alloc);
	buffer = malloc_nofail((diskmax + 2) * sizeof(void*));
	for(i=0;i<diskmax+2;++i) {
		buffer[i] = buffer_aligned + i * block_size;
		memset(buffer[i], i, block_size);
	}

#ifdef __GNUC__
	printf("Compiled with gcc " __VERSION__ "\n");
#endif

	printf("Speed test with %d disk and %d buffer size...\n", diskmax, block_size);

	SPEED_START {
		for(j=0;j<diskmax;++j) {
			memset(buffer[j], j, block_size);
		}
	} SPEED_STOP

	printf("memset %llu [MB/s]\n", ds / dt);

	SPEED_START {
		for(j=0;j<diskmax;++j) {
			memhash(HASH_MD5, digest, buffer[j], block_size);
		}
	} SPEED_STOP

	printf("MD5 %llu [MB/s]\n", ds / dt);

	SPEED_START {
		for(j=0;j<diskmax;++j) {
			memhash(HASH_MURMUR3, digest, buffer[j], block_size);
		}
	} SPEED_STOP

	printf("Murmur3 %llu [MB/s]\n", ds / dt);

	count = 0;
	SPEED_START {
		raid5_int32r2(buffer, diskmax, block_size);
	} SPEED_STOP

	printf("RAID5 int32x2 %llu [MB/s]\n", ds / dt);

#if defined(__i386__) || defined(__x86_64__)
	if (cpu_has_mmx()) {
		SPEED_START {
			raid5_mmxr2(buffer, diskmax, block_size);
		} SPEED_STOP

		printf("RAID5 mmxx2 %llu [MB/s]\n", ds / dt);
	}

	if (cpu_has_sse2()) {
		SPEED_START {
			raid5_sse2r2(buffer, diskmax, block_size);
		} SPEED_STOP

		printf("RAID5 sse2x2 %llu [MB/s]\n", ds / dt);
	}
#endif

	SPEED_START {
		raid6_int32r2(buffer, diskmax, block_size);
	} SPEED_STOP

	printf("RAID6 int32x2 %llu [MB/s]\n", ds / dt);

#if defined(__i386__) || defined(__x86_64__)
	if (cpu_has_mmx()) {
		SPEED_START {
			raid6_mmxr2(buffer, diskmax, block_size);
		} SPEED_STOP

		printf("RAID6 mmxx2 %llu [MB/s]\n", ds / dt);
	}

	if (cpu_has_sse2()) {
		SPEED_START {
			raid6_sse2r2(buffer, diskmax, block_size);
		} SPEED_STOP

		printf("RAID6 sse2x2 %llu [MB/s]\n", ds / dt);
	}
#endif

	free(buffer_alloc);
	free(buffer);
}

