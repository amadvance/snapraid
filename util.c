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

#include "util.h"

/****************************************************************************/
/* string */

static char strhexset[16] = "0123456789abcdef";

void strenchex(char* str, const void* void_data, unsigned data_len)
{
	const unsigned char* data = void_data;
	unsigned i;

	for(i=0;i<data_len;++i) {
		unsigned char b = data[i];
		*str++ = strhexset[b >> 4];
		*str++ = strhexset[b & 0xF];
	}
}

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

char* strdechex(void* void_data, unsigned data_len, char* str)
{
	unsigned char* data = void_data;
	unsigned i;

	for(i=0;i<data_len;++i) {
		unsigned b0;
		unsigned b1;
		unsigned b;

		b0 = strdecset[(unsigned char)str[0]];
		b1 = strdecset[(unsigned char)str[1]];

		b = (b0 << 4) | b1;

		if (b > 0xFF)
			return str;

		data[0] = b;

		data += 1;
		str += 2;
	}

	return 0;
}

/****************************************************************************/
/* stream */

STREAM* sopen_read(const char* file)
{
	STREAM* s = malloc_nofail(sizeof(STREAM));

	s->f = open(file, O_RDONLY | O_BINARY | O_SEQUENTIAL);
	if (s->f == -1) {
		free(s);
		return 0;
	}

	s->buffer = malloc_nofail(STREAM_SIZE);
	s->pos = s->buffer;
	s->end = s->buffer;
	s->state = STREAM_OK;

	return s;
}

void sclose(STREAM* s)
{
	close(s->f);
	free(s->buffer);
	free(s);
}

int sflow(STREAM* s)
{
	ssize_t ret;

	if (s->state != STREAM_OK)
		return EOF;

	ret = read(s->f, s->buffer, STREAM_SIZE);

	if (ret < 0) {
		s->state = STREAM_ERROR;
		return EOF;
	}
	if (ret == 0) {
		s->state = STREAM_EOF;
		return EOF;
	}

	s->pos = s->buffer;
	s->end = s->buffer + ret;

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
			/* remove ending carrige return to support the Windows CR+LF format */
			if (i != str && i[-1] == '\r')
				--i;
			sungetc(c, f);
			break;
		}

		*i++ = c;

		if (i == send)
			return -1;
	}

	*i = 0;

	return 0;
}

int sgetline(STREAM* f, char* str, int size)
{
	char* i = str;
	char* send = str + size;
	int c;

	/* if there is enough data in memory */
	if (sptrlookup(f, size)) {
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

			if (i == send)
				return -1;
		}

		sptrset(f, pos);
	} else {
		while (1) {
			c = sgetc(f);
			if (c == EOF) {
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

			if (i == send)
				return -1;
		}
	}

	*i = 0;

	return 0;
}

int sgetu32(STREAM* f, uint32_t* value)
{
	int c;

	c = sgetc(f);
	if (c>='0' && c<='9') {
		uint32_t v;

		v = c - '0';

		c = sgetc(f);
		while (c>='0' && c<='9') {
			v *= 10;
			v += c - '0';
			c = sgetc(f);
		}

		*value = v;

		sungetc(c, f);
		return 0;
	} else {
		/* nothing read */
		return -1;
	}
}

int sgetu64(STREAM* f, uint64_t* value)
{
	int c;

	c = sgetc(f);
	if (c>='0' && c<='9') {
		uint64_t v;

		v = c - '0';

		c = sgetc(f);
		while (c>='0' && c<='9') {
			v *= 10;
			v += c - '0';
			c = sgetc(f);
		}

		*value = v;

		sungetc(c, f);
		return 0;
	} else {
		/* nothing read */
		return -1;
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
			return -1;
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
			if (c == EOF)
				return -1;
			b0 = strdecset[c];

			c = sgetc(f);
			if (c == EOF)
				return -1;
			b1 = strdecset[c];

			b = (b0 << 4) | b1;

			if (b > 0xFF)
				return -1;

			*data++ = b;
		}
	}

	return 0;
}

/****************************************************************************/
/* path */

void pathcpy(char* str, size_t size, const char* src)
{
	size_t len = strlen(src);
	
	if (len + 1 >= size) {
		fprintf(stderr, "Path too long\n");
		exit(EXIT_FAILURE);
	}

	memcpy(str, src, len + 1);
}

void pathimport(char* str, size_t size, const char* src)
{
	pathcpy(str, size, src);

#ifdef _WIN32
	/* convert all Windows '\' to '/' */
	while (*str) {
		if (*str == '\\')
			*str = '/';
		++str;
	}
#endif
}

void pathprint(char* str, size_t size, const char* format, ...)
{
	size_t len;
	va_list ap;
	
	va_start(ap, format);
	len = vsnprintf(str, size, format, ap);
	va_end(ap);

	if (len >= size) {
		fprintf(stderr, "Path too long\n");
		exit(EXIT_FAILURE);
	}
}

void pathslash(char* str, size_t size)
{
	size_t len = strlen(str);

	if (len > 0 && str[len - 1] != '/') {
		if (len + 2 >= size) {
			fprintf(stderr, "Path too long\n");
			exit(EXIT_FAILURE);
		}

		str[len] = '/';
		str[len+1] = 0;
	}
}

/****************************************************************************/
/* mem */

void* malloc_nofail(size_t size)
{
	void* ptr = malloc(size);

	if (!ptr) {
		fprintf(stderr, "Low memory\n");
		exit(EXIT_FAILURE);
	}

	return ptr;
}

#define ALIGN 256

void* malloc_nofail_align(size_t size, void** freeptr)
{
	unsigned char* ptr = malloc_nofail(size + ALIGN);
	uintptr_t offset;

	*freeptr = ptr;

	offset = ((uintptr_t)ptr) % ALIGN;

	if (offset != 0) {
		ptr += ALIGN - offset;
	}

	return ptr;
}

#if HAVE_LIBCRYPTO
#include <openssl/md5.h>
#else
#include "md5.c"
#endif

#include "murmurhash3.c"

void memhash(unsigned kind, void* digest, const void* src, unsigned size)
{
	if (kind == HASH_MURMUR3) {
		MurmurHash3_x86_128(src, size, 0, digest);
		return;
	}

	if (kind == HASH_MD5) {
#if HAVE_LIBCRYPTO
		MD5((void*)src, size, digest);
#else
		struct md5_t md5;
		md5_init(&md5);
		md5_update(&md5, src, size);
		md5_final(&md5, digest);
#endif
		return;
	}
}

