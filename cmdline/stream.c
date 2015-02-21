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

#include "support.h"
#include "util.h"
#include "stream.h"

/****************************************************************************/
/* hex conversion table */

static char strhexset[16] = "0123456789abcdef";

static unsigned strdecset[256] =
{
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, -1, -1, -1, -1, -1, -1,
	-1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
};

/****************************************************************************/
/* stream */

unsigned STREAM_SIZE = 1024 * 64;

STREAM* sopen_read(const char* file)
{
#if HAVE_POSIX_FADVISE
	int ret;
#endif
	STREAM* s = malloc_nofail(sizeof(STREAM));

	s->handle_size = 1;
	s->handle = malloc_nofail(sizeof(struct stream_handle));

	pathcpy(s->handle[0].path, sizeof(s->handle[0].path), file);
	s->handle[0].f = open(file, O_RDONLY | O_BINARY | O_SEQUENTIAL);
	if (s->handle[0].f == -1) {
		free(s->handle);
		free(s);
		return 0;
	}

#if HAVE_POSIX_FADVISE
	/* advise sequential access */
	ret = posix_fadvise(s->handle[0].f, 0, 0, POSIX_FADV_SEQUENTIAL);
	if (ret != 0) {
		/* LCOV_EXCL_START */
		close(s->handle[0].f);
		free(s->handle);
		free(s);
		return 0;
		/* LCOV_EXCL_STOP */
	}
#endif

	s->buffer = malloc_nofail_test(STREAM_SIZE);
	s->pos = s->buffer;
	s->end = s->buffer;
	s->state = STREAM_STATE_READ;
	s->state_index = 0;
	s->offset = 0;
	s->offset_uncached = 0;
	s->crc = 0;
	s->crc_uncached = 0;
	s->crc_stream = CRC_IV;

	return s;
}

STREAM* sopen_multi_write(unsigned count)
{
	unsigned i;

	STREAM* s = malloc_nofail(sizeof(STREAM));

	s->handle_size = count;
	s->handle = malloc_nofail(count * sizeof(struct stream_handle));

	for (i = 0; i < count; ++i)
		s->handle[i].f = -1;

	s->buffer = malloc_nofail_test(STREAM_SIZE);
	s->pos = s->buffer;
	s->end = s->buffer + STREAM_SIZE;
	s->state = STREAM_STATE_WRITE;
	s->state_index = 0;
	s->offset = 0;
	s->offset_uncached = 0;
	s->crc = 0;
	s->crc_uncached = 0;
	s->crc_stream = CRC_IV;

	return s;
}

int sopen_multi_file(STREAM* s, unsigned i, const char* file)
{
#if HAVE_POSIX_FADVISE
	int ret;
#endif
	int f;

	pathcpy(s->handle[i].path, sizeof(s->handle[i].path), file);

	f = open(file, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY | O_SEQUENTIAL, 0600);
	if (f == -1) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

#if HAVE_POSIX_FADVISE
	/* advise sequential access */
	ret = posix_fadvise(f, 0, 0, POSIX_FADV_SEQUENTIAL);
	if (ret != 0) {
		/* LCOV_EXCL_START */
		close(f);
		return -1;
		/* LCOV_EXCL_STOP */
	}
#endif

	s->handle[i].f = f;

	return 0;
}

int sclose(STREAM* s)
{
	int fail = 0;
	unsigned i;

	if (s->state == STREAM_STATE_WRITE) {
		if (sflush(s) != 0) {
			/* LCOV_EXCL_START */
			fail = 1;
			/* LCOV_EXCL_STOP */
		}
	}

	for (i = 0; i < s->handle_size; ++i) {
		if (close(s->handle[i].f) != 0) {
			/* LCOV_EXCL_START */
			fail = 1;
			/* LCOV_EXCL_STOP */
		}
	}

	free(s->handle);
	free(s->buffer);
	free(s);

	if (fail) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	return 0;
}

int shandle(STREAM* s)
{
	if (!s->handle_size) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	return s->handle[0].f;
}

int sfill(STREAM* s)
{
	ssize_t ret;

	if (s->state != STREAM_STATE_READ) {
		/* LCOV_EXCL_START */
		return EOF;
		/* LCOV_EXCL_STOP */
	}

	ret = read(s->handle[0].f, s->buffer, STREAM_SIZE);

	if (ret < 0) {
		/* LCOV_EXCL_START */
		s->state = STREAM_STATE_ERROR;
		return EOF;
		/* LCOV_EXCL_STOP */
	}
	if (ret == 0) {
		s->state = STREAM_STATE_EOF;
		return EOF;
	}

	/* update the crc */
	s->crc_uncached = s->crc;
	s->crc = crc32c(s->crc, s->buffer, ret);

	/* update the offset */
	s->offset_uncached = s->offset;
	s->offset += ret;

	s->pos = s->buffer;
	s->end = s->buffer + ret;

	return *s->pos++;
}

int sflush(STREAM* s)
{
	ssize_t ret;
	ssize_t size;
	unsigned i;

	if (s->state != STREAM_STATE_WRITE) {
		/* LCOV_EXCL_START */
		return EOF;
		/* LCOV_EXCL_STOP */
	}

	size = s->pos - s->buffer;
	if (!size)
		return 0;

	/* update the crc */
	s->crc = crc32c(s->crc, s->buffer, size);
	s->crc_uncached = s->crc;

	for (i = 0; i < s->handle_size; ++i) {
		ret = write(s->handle[i].f, s->buffer, size);

		if (ret != size) {
			/* LCOV_EXCL_START */
			s->state = STREAM_STATE_ERROR;
			s->state_index = i;
			return EOF;
			/* LCOV_EXCL_STOP */
		}
	}

	/* update the offset */
	s->offset += size;
	s->offset_uncached = s->offset;

	s->pos = s->buffer;

	return 0;
}

int64_t stell(STREAM* s)
{
	return s->offset_uncached + (s->pos - s->buffer);
}

uint32_t scrc(STREAM*s)
{
	return crc32c(s->crc_uncached, s->buffer, s->pos - s->buffer);
}

uint32_t scrc_stream(STREAM*s)
{
	return s->crc_stream ^ CRC_IV;
}

int sgettok(STREAM* f, char* str, int size)
{
	char* i = str;
	char* send = str + size;
	int c;

	while (1) {
		c = sgetc(f);
		if (c == EOF) {
			break;
		}
		if (c == ' ' || c == '\t') {
			sungetc(c, f);
			break;
		}
		if (c == '\n') {
			/* remove ending carrige return to support the Windows CR+LF format */
			if (i != str && i[-1] == '\r')
				--i;
			sungetc(c, f);
			break;
		}

		*i++ = c;

		if (i == send) {
			/* LCOV_EXCL_START */
			return -1;
			/* LCOV_EXCL_STOP */
		}
	}

	*i = 0;

	return i - str;
}

int sread(STREAM* f, void* void_data, unsigned size)
{
	unsigned char* data = void_data;

	/* if there is enough space in memory */
	if (sptrlookup(f, size)) {
		/* optimized version with all the data in memory */
		unsigned char* pos = sptrget(f);

		/* copy it */
		while (size--)
			*data++ = *pos++;

		sptrset(f, pos);
	} else {
		/* standard version using sgetc() */
		while (size--) {
			int c = sgetc(f);
			if (c == EOF) {
				/* LCOV_EXCL_START */
				return -1;
				/* LCOV_EXCL_STOP */
			}

			*data++ = c;
		}
	}

	return 0;
}

int sgetline(STREAM* f, char* str, int size)
{
	char* i = str;
	char* send = str + size;
	int c;

	/* if there is enough data in memory */
	if (sptrlookup(f, size)) {
		/* optimized version with all the data in memory */
		unsigned char* pos = sptrget(f);

		while (1) {
			c = *pos++;
			if (c == '\n') {
				/* remove ending carrige return to support the Windows CR+LF format */
				if (i != str && i[-1] == '\r')
					--i;
				--pos;
				break;
			}

			*i++ = c;

			if (i == send) {
				/* LCOV_EXCL_START */
				return -1;
				/* LCOV_EXCL_STOP */
			}
		}

		sptrset(f, pos);
	} else {
		while (1) {
			c = sgetc(f);
			if (c == EOF) {
				/* LCOV_EXCL_START */
				break;
				/* LCOV_EXCL_STOP */
			}
			if (c == '\n') {
				/* remove ending carrige return to support the Windows CR+LF format */
				if (i != str && i[-1] == '\r')
					--i;
				sungetc(c, f);
				break;
			}

			*i++ = c;

			if (i == send) {
				/* LCOV_EXCL_START */
				return -1;
				/* LCOV_EXCL_STOP */
			}
		}
	}

	*i = 0;

	return i - str;
}

int sgetlasttok(STREAM* f, char* str, int size)
{
	int ret;

	ret = sgetline(f, str, size);
	if (ret < 0) {
		/* LCOV_EXCL_START */
		return ret;
		/* LCOV_EXCL_STOP */
	}

	while (ret > 0 && (str[ret - 1] == ' ' || str[ret - 1] == '\t'))
		--ret;

	str[ret] = 0;

	return ret;
}

int sgetu32(STREAM* f, uint32_t* value)
{
	int c;

	c = sgetc(f);
	if (c >= '0' && c <= '9') {
		uint32_t v;

		v = c - '0';

		c = sgetc(f);
		while (c >= '0' && c <= '9') {
			v *= 10;
			v += c - '0';
			c = sgetc(f);
		}

		*value = v;

		sungetc(c, f);
		return 0;
	} else {
		/* LCOV_EXCL_START */
		/* nothing read */
		return -1;
		/* LCOV_EXCL_STOP */
	}
}

int sgetu64(STREAM* f, uint64_t* value)
{
	int c;

	c = sgetc(f);
	if (c >= '0' && c <= '9') {
		uint64_t v;

		v = c - '0';

		c = sgetc(f);
		while (c >= '0' && c <= '9') {
			v *= 10;
			v += c - '0';
			c = sgetc(f);
		}

		*value = v;

		sungetc(c, f);
		return 0;
	} else {
		/* LCOV_EXCL_START */
		/* nothing read */
		return -1;
		/* LCOV_EXCL_STOP */
	}
}

int sgethex(STREAM* f, void* void_data, int size)
{
	unsigned char* data = void_data;

	/* if there is enough data in memory */
	if (sptrlookup(f, size * 2)) {
		/* optimized version with all the data in memory */
		unsigned char* pos = sptrget(f);
		unsigned x = 0;

		while (size--) {
			unsigned b0;
			unsigned b1;
			unsigned b;

			b0 = strdecset[pos[0]];
			b1 = strdecset[pos[1]];
			pos += 2;

			b = (b0 << 4) | b1;

			x |= b;

			*data++ = b;
		}

		/* at the end check if a digit was wrong */
		if (x > 0xFF) {
			/* LCOV_EXCL_START */
			return -1;
			/* LCOV_EXCL_STOP */
		}

		sptrset(f, pos);
	} else {
		/* standard version using sgetc() */
		while (size--) {
			unsigned b0;
			unsigned b1;
			unsigned b;
			int c;

			c = sgetc(f);
			if (c == EOF) {
				/* LCOV_EXCL_START */
				return -1;
				/* LCOV_EXCL_STOP */
			}
			b0 = strdecset[(unsigned char)c];

			c = sgetc(f);
			if (c == EOF) {
				/* LCOV_EXCL_START */
				return -1;
				/* LCOV_EXCL_STOP */
			}
			b1 = strdecset[(unsigned char)c];

			b = (b0 << 4) | b1;

			if (b > 0xFF) {
				/* LCOV_EXCL_START */
				return -1;
				/* LCOV_EXCL_STOP */
			}

			*data++ = b;
		}
	}

	return 0;
}

int sgetb32(STREAM* f, uint32_t* value)
{
	uint32_t v;
	unsigned char b;
	unsigned char s;
	int c;

	v = 0;
	s = 0;
loop:
	c = sgetc(f);
	if (c == EOF) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	b = (unsigned char)c;
	if ((b & 0x80) == 0) {
		v |= (uint32_t)b << s;
		s += 7;
		if (s >= 32) {
			/* LCOV_EXCL_START */
			return -1;
			/* LCOV_EXCL_STOP */
		}
		goto loop;
	}

	v |= (uint32_t)(b & 0x7f) << s;

	*value = v;

	return 0;
}

int sgetb64(STREAM* f, uint64_t* value)
{
	uint64_t v;
	unsigned char b;
	unsigned char s;
	int c;

	v = 0;
	s = 0;
loop:
	c = sgetc(f);
	if (c == EOF) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	b = (unsigned char)c;
	if ((b & 0x80) == 0) {
		v |= (uint64_t)b << s;
		s += 7;
		if (s >= 64) {
			/* LCOV_EXCL_START */
			return -1;
			/* LCOV_EXCL_STOP */
		}
		goto loop;
	}

	v |= (uint64_t)(b & 0x7f) << s;

	*value = v;

	return 0;
}

int sgetble32(STREAM* f, uint32_t* value)
{
	unsigned char buf[4];

	if (sread(f, buf, 4) != 0) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	*value = buf[0] | (uint32_t)buf[1] << 8 | (uint32_t)buf[2] << 16 | (uint32_t)buf[3] << 24;

	return 0;
}

int sgetbs(STREAM* f, char* str, int size)
{
	uint32_t len;

	if (sgetb32(f, &len) < 0) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	if (len + 1 > (uint32_t)size) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	str[len] = 0;

	return sread(f, str, (int)len);
}

int sputs(const char* str, STREAM* f)
{
	while (*str) {
		if (sputc(*str++, f) != 0) {
			/* LCOV_EXCL_START */
			return -1;
			/* LCOV_EXCL_STOP */
		}
	}

	return 0;
}

int swrite(const void* void_data, unsigned size, STREAM* f)
{
	const unsigned char* data = void_data;

	/* if there is enough space in memory */
	if (sptrlookup(f, size)) {
		/* optimized version with all the data in memory */
		unsigned char* pos = sptrget(f);

		/* copy it */
		while (size--) {
			f->crc_stream = crc32c_plain(f->crc_stream, *data);
			*pos++ = *data++;
		}

		sptrset(f, pos);
	} else {
		/* standard version using sputc() */
		while (size--) {
			if (sputc(*data++, f) != 0) {
				/* LCOV_EXCL_START */
				return -1;
				/* LCOV_EXCL_STOP */
			}
		}
	}

	return 0;
}

int sputu32(uint32_t value, STREAM* s)
{
	char buf[16];
	int i;

	if (!value)
		return sputc('0', s);

	i = sizeof(buf);

	while (value) {
		buf[--i] = (value % 10) + '0';
		value /= 10;
	}

	return swrite(buf + i, sizeof(buf) - i, s);
}

int sputu64(uint64_t value, STREAM* s)
{
	char buf[32];
	uint32_t value32;
	int i;

	if (!value)
		return sputc('0', s);

	i = sizeof(buf);

	while (value > 0xFFFFFFFF) {
		buf[--i] = (value % 10) + '0';
		value /= 10;
	}

	value32 = (uint32_t)value;

	while (value32) {
		buf[--i] = (value32 % 10) + '0';
		value32 /= 10;
	}

	return swrite(buf + i, sizeof(buf) - i, s);
}

int sputhex(const void* void_data, int size, STREAM* f)
{
	const unsigned char* data = void_data;

	/* if there is enough space in memory */
	if (sptrlookup(f, size * 2)) {
		/* optimized version with all the data in memory */
		unsigned char* pos = sptrget(f);

		while (size) {
			unsigned b = *data;

			*pos++ = strhexset[b >> 4];
			*pos++ = strhexset[b & 0xF];

			++data;
			--size;
		}

		sptrset(f, pos);
	} else {
		/* standard version using sputc() */
		while (size) {
			unsigned b = *data;

			if (sputc(strhexset[b >> 4], f) != 0) {
				/* LCOV_EXCL_START */
				return -1;
				/* LCOV_EXCL_STOP */
			}
			if (sputc(strhexset[b & 0xF], f) != 0) {
				/* LCOV_EXCL_START */
				return -1;
				/* LCOV_EXCL_STOP */
			}

			++data;
			--size;
		}
	}

	return 0;
}

int sputb32(uint32_t value, STREAM* s)
{
	unsigned char b;
	unsigned char buf[16];
	unsigned i;

	i = 0;
loop:
	b = value & 0x7f;
	value >>= 7;

	if (value) {
		buf[i++] = b;
		goto loop;
	}

	buf[i++] = b | 0x80;

	return swrite(buf, i, s);
}

int sputb64(uint64_t value, STREAM* s)
{
	unsigned char b;
	unsigned char buf[16];
	unsigned i;

	i = 0;
loop:
	b = value & 0x7f;
	value >>= 7;

	if (value) {
		buf[i++] = b;
		goto loop;
	}

	buf[i++] = b | 0x80;

	return swrite(buf, i, s);
}

int sputble32(uint32_t value, STREAM* s)
{
	unsigned char buf[4];

	buf[0] = value & 0xFF;
	buf[1] = (value >> 8) & 0xFF;
	buf[2] = (value >> 16) & 0xFF;
	buf[3] = (value >> 24) & 0xFF;

	return swrite(buf, 4, s);
}

int sputbs(const char* str, STREAM* f)
{
	size_t len = strlen(str);

	if (sputb32(len, f) != 0) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	return swrite(str, len, f);
}

#if HAVE_FSYNC
int ssync(STREAM* s)
{
	unsigned i;

	for (i = 0; i < s->handle_size; ++i) {
		if (fsync(s->handle[i].f) != 0) {
			/* LCOV_EXCL_START */
			s->state = STREAM_STATE_ERROR;
			s->state_index = i;
			return -1;
			/* LCOV_EXCL_STOP */
		}
	}

	return 0;
}
#endif

