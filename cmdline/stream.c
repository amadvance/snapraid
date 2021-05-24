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
/* stream */

unsigned STREAM_SIZE = 64 * 1024;

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
	if (ret == ENOSYS) {
		log_fatal("WARNING! fadvise() is not supported in this platform. Performance may not be optimal!\n");
		/* call is not supported, like in armhf, see posix_fadvise manpage */
		ret = 0;
	}
	if (ret != 0) {
		/* LCOV_EXCL_START */
		close(s->handle[0].f);
		free(s->handle);
		free(s);
		errno = ret; /* posix_fadvise return the error code */
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

	/* O_EXCL to be resilient ensure to always create a new file and not use a stale link to the original file */
	f = open(file, O_WRONLY | O_CREAT | O_EXCL | O_BINARY | O_SEQUENTIAL, 0600);
	if (f == -1) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

#if HAVE_POSIX_FADVISE
	/* advise sequential access */
	ret = posix_fadvise(f, 0, 0, POSIX_FADV_SEQUENTIAL);
	if (ret == ENOSYS) {
		/* call is not supported, like in armhf, see posix_fadvise manpage */
		ret = 0;
	}
	if (ret != 0) {
		/* LCOV_EXCL_START */
		close(f);
		errno = ret; /* posix_fadvise return the error code */
		return -1;
		/* LCOV_EXCL_STOP */
	}
#endif

	s->handle[i].f = f;

	return 0;
}

STREAM* sopen_write(const char* file)
{
	STREAM* s = sopen_multi_write(1);

	if (sopen_multi_file(s, 0, file) != 0) {
		sclose(s);
		return 0;
	}

	return s;
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

/**
 * Fill the read stream buffer.
 * \return 0 if at least on char is read, or EOF on error.
 */
static int sfill(STREAM* s)
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

	return 0;
}

int sdeplete(STREAM* s, unsigned char* last)
{
	/* last four bytes */
	last[0] = 0;
	last[1] = 0;
	last[2] = 0;
	last[3] = 0;

	while (1) {
		/* increase the position up to 4 bytes before the end */
		if (s->pos + 4 <= s->end)
			s->pos = s->end - 4;

		/* insert the last 4 bytes */
		while (s->pos < s->end) {
			last[0] = last[1];
			last[1] = last[2];
			last[2] = last[3];
			last[3] = *s->pos++;
		}

		/* fill again the buffer until the end of the file */
		if (sfill(s) != 0) {
			/* on error fail */
			if (serror(s)) {
				/* LCOV_EXCL_START */
				return EOF;
				/* LCOV_EXCL_STOP */
			}

			/* on EOF terminate */
			break;
		}
	}

	return 0;
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

	/*
	 * Update the crc *after* writing the data.
	 *
	 * This must be done after the file write,
	 * to be able to detect memory errors on the buffer,
	 * happening during the write.
	 */
	s->crc = crc32c(s->crc, s->buffer, size);
	s->crc_uncached = s->crc;

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

int sgetc_uncached(STREAM* s)
{
	/* if at the end of the buffer, fill it */
	if (s->pos == s->end && sfill(s) != 0)
		return EOF;
	return *s->pos++;
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
			/* remove ending carriage return to support the Windows CR+LF format */
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
				/* remove ending carriage return to support the Windows CR+LF format */
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
				/* remove ending carriage return to support the Windows CR+LF format */
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
	if (c == '0') {
		*value = 0;
		return 0;
	} else if (c >= '1' && c <= '9') {
		uint32_t v;

		v = c - '0';

		c = sgetc(f);
		while (c >= '0' && c <= '9') {
			uint32_t digit;
			if (v > 0xFFFFFFFFU / 10) {
				/* LCOV_EXCL_START */
				/* overflow */
				return -1;
				/* LCOV_EXCL_STOP */
			}
			v *= 10;

			digit = c - '0';
			if (v > 0xFFFFFFFFU - digit) {
				/* LCOV_EXCL_START */
				/* overflow */
				return -1;
				/* LCOV_EXCL_STOP */
			}
			v += digit;

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

int swrite(const void* void_data, unsigned size, STREAM* f)
{
	const unsigned char* data = void_data;

	/* if there is enough space in memory */
	if (sptrlookup(f, size)) {
		/* optimized version with all the data in memory */
		unsigned char* pos = sptrget(f);

		/**
		 * Update the crc *before* writing the data in the buffer
		 *
		 * This must be done before the memory write,
		 * to be able to detect memory errors on the buffer,
		 * happening before we write it on the file.
		 */
		f->crc_stream = crc32c_plain(f->crc_stream, data, size);

		/* copy it */
		while (size--)
			*pos++ = *data++;

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

