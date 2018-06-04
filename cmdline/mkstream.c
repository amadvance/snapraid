/*
 * Copyright (C) 2015 Andrea Mazzoleni
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

#include "stream.h"
#include "support.h"

#define STREAM_MAX 8
#define BUFFER_MAX 64
#define STR_MAX 128

void test(void)
{
	struct stream* s;
	char file[32];
	unsigned char buffer[BUFFER_MAX];
	char str[STR_MAX];
	unsigned i, j;
	uint32_t u32 = -1L;
	uint64_t u64 = -1LL;
	uint32_t put_crc_stored;
	uint32_t put_crc_computed;

	crc32c_init();

	s = sopen_multi_write(STREAM_MAX);
	for (i = 0; i < STREAM_MAX; ++i) {
		snprintf(file, sizeof(file), "stream%u.bin", i);
		remove(file);
		if (sopen_multi_file(s, i, file) != 0) {
			/* LCOV_EXCL_START */
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}

	for (j = 0; j < 256; ++j) {
		if (sputc(j, s) != 0) {
			/* LCOV_EXCL_START */
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}

	for (j = 0; j < 32; ++j) {
		if (sputb32(u32 >> j, s) != 0) {
			/* LCOV_EXCL_START */
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}

	for (j = 0; j < 64; ++j) {
		if (sputb64(u64 >> j, s) != 0) {
			/* LCOV_EXCL_START */
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}

	for (j = 0; j < BUFFER_MAX; ++j) {
		memset(buffer, j, j);
		if (swrite(buffer, j, s) != 0) {
			/* LCOV_EXCL_START */
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}

	for (j = 1; j < STR_MAX; ++j) {
		memset(str, ' ' + j, j - 1);
		str[j - 1] = 0;
		if (sputbs(str, s) != 0) {
			/* LCOV_EXCL_START */
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}

	put_crc_stored = scrc(s);
	put_crc_computed = scrc_stream(s);

	if (put_crc_stored != put_crc_computed) {
		/* LCOV_EXCL_START */
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	if (sputble32(put_crc_stored, s) != 0) {
		/* LCOV_EXCL_START */
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	if (sclose(s) != 0) {
		/* LCOV_EXCL_START */
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	for (i = 0; i < STREAM_MAX; ++i) {
		uint32_t get_crc_stored;
		uint32_t get_crc_computed;
		snprintf(file, sizeof(file), "stream%u.bin", i);

		s = sopen_read(file);
		if (s == 0) {
			/* LCOV_EXCL_START */
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		for (j = 0; j < 256; ++j) {
			int c = sgetc(s);
			if (c == EOF || (unsigned char)c != j) {
				/* LCOV_EXCL_START */
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
		}

		for (j = 0; j < 32; ++j) {
			uint32_t v32;
			if (sgetb32(s, &v32) != 0 || v32 != (u32 >> j)) {
				/* LCOV_EXCL_START */
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
		}

		for (j = 0; j < 64; ++j) {
			uint64_t v64;
			if (sgetb64(s, &v64) != 0 || v64 != (u64 >> j)) {
				/* LCOV_EXCL_START */
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
		}

		for (j = 1; j < BUFFER_MAX; ++j) {
			char copy[BUFFER_MAX];
			memset(buffer, j, j);
			if (sread(s, copy, j) != 0 || memcmp(copy, buffer, j) != 0) {
				/* LCOV_EXCL_START */
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
		}

		for (j = 1; j < STR_MAX; ++j) {
			char copy[STR_MAX];
			memset(str, ' ' + j, j - 1);
			str[j - 1] = 0;
			if (sgetbs(s, copy, sizeof(copy)) != 0 || strcmp(copy, str) != 0) {
				/* LCOV_EXCL_START */
				exit(EXIT_FAILURE);
				/* LCOV_EXCL_STOP */
			}
		}

		/* get the computed CRC *before* reading the stored one */
		get_crc_computed = scrc(s);

		if (sgetble32(s, &get_crc_stored) != 0) {
			/* LCOV_EXCL_START */
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		if (get_crc_stored != put_crc_stored) {
			/* LCOV_EXCL_START */
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		if (get_crc_stored != get_crc_computed) {
			/* LCOV_EXCL_START */
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		if (sclose(s) != 0) {
			/* LCOV_EXCL_START */
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}

	for (i = 0; i < STREAM_MAX; ++i) {
		uint32_t get_crc_stored;
		uint32_t get_crc_computed;
		unsigned char buf[4];
		snprintf(file, sizeof(file), "stream%u.bin", i);

		s = sopen_read(file);
		if (s == 0) {
			/* LCOV_EXCL_START */
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		if (sdeplete(s, buf) != 0) {
			/* LCOV_EXCL_START */
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		/* get the stored crc from the last four bytes */
		get_crc_stored = buf[0] | (uint32_t)buf[1] << 8 | (uint32_t)buf[2] << 16 | (uint32_t)buf[3] << 24;

		if (get_crc_stored != put_crc_stored) {
			/* LCOV_EXCL_START */
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		/* get the computed CRC *after* reading the stored one */
		get_crc_computed = scrc(s);

		/* adjust the stored crc to include itself */
		get_crc_stored = crc32c(get_crc_stored, buf, 4);

		if (get_crc_stored != get_crc_computed) {
			/* LCOV_EXCL_START */
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		if (sclose(s) != 0) {
			/* LCOV_EXCL_START */
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}
}

int main(void)
{
	unsigned i;

	lock_init();

	for (i = 1; i <= 16; ++i) {

		/* test with different stream buffer size */
		STREAM_SIZE = i;

		printf("Test stream buffer size %u\n", i);

		test();
	}

	return 0;
}

