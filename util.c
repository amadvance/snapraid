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
		close(s->handle[0].f);
		free(s->handle);
		free(s);
		return 0;
	}
#endif

	s->buffer = malloc_nofail(STREAM_SIZE);
	s->pos = s->buffer;
	s->end = s->buffer;
	s->state = STREAM_STATE_READ;
	s->state_index = 0;

	return s;
}

STREAM* sopen_multi_write(unsigned count)
{
	unsigned i;

	STREAM* s = malloc_nofail(sizeof(STREAM));

	s->handle_size = count;
	s->handle = malloc_nofail(count * sizeof(struct stream_handle));

	for(i=0;i<count;++i)
		s->handle[i].f = -1;

	s->buffer = malloc_nofail(STREAM_SIZE);
	s->pos = s->buffer;
	s->end = s->buffer + STREAM_SIZE;
	s->state = STREAM_STATE_WRITE;
	s->state_index = 0;

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
		return -1;
	}

#if HAVE_POSIX_FADVISE
	/* advise sequential access */
	ret = posix_fadvise(f, 0, 0, POSIX_FADV_SEQUENTIAL);
	if (ret != 0) {
		close(f);
		return -1;
	}
#endif

	s->handle[i].f = f;

	return 0;
}

STREAM* sopen_write(const char* file)
{
	STREAM* s;

	s = sopen_multi_write(1);
	if (!s)
		return 0;

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
		if (sflush(s) != 0)
			fail = 1;
	}

	for(i=0;i<s->handle_size;++i) {
		if (close(s->handle[i].f) != 0)
			fail = 1;
	}

	free(s->handle);
	free(s->buffer);
	free(s);

	if (fail)
		return -1;

	return 0;
}

int sfill(STREAM* s)
{
	ssize_t ret;

	if (s->state != STREAM_STATE_READ)
		return EOF;

	ret = read(s->handle[0].f, s->buffer, STREAM_SIZE);

	if (ret < 0) {
		s->state = STREAM_STATE_ERROR;
		return EOF;
	}
	if (ret == 0) {
		s->state = STREAM_STATE_EOF;
		return EOF;
	}

	s->pos = s->buffer;
	s->end = s->buffer + ret;

	return *s->pos++;
}

int sflush(STREAM* s)
{
	ssize_t ret;
	ssize_t size;
	unsigned i;

	if (s->state != STREAM_STATE_WRITE)
		return EOF;

	size = s->pos - s->buffer;
	if (!size)
		return 0;

	for(i=0;i<s->handle_size;++i) {
		ret = write(s->handle[i].f, s->buffer, size);

		if (ret != size) {
			s->state = STREAM_STATE_ERROR;
			s->state_index = i;
			return EOF;
		}
	}

	s->pos = s->buffer;

	return 0;
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

	return i - str;
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

	return i - str;
}

int sgetlasttok(STREAM* f, char* str, int size)
{
	int ret;

	ret = sgetline(f, str, size);
	if (ret < 0)
		return ret;

	while (ret > 0 && (str[ret-1] == ' ' || str[ret-1] == '\t')) {
		--ret;
	}

	str[ret] = 0;

	return ret;
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

int sputs(const char* str, STREAM* f)
{
	while (*str) {
		if (sputc(*str++, f) != 0)
			return -1;
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
			*pos++ = *data++;
		}

		sptrset(f, pos);
	} else {
		/* standard version using sputc() */
		while (size--) {
			if (sputc(*data++, f) != 0)
				return -1;
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

			if (sputc(strhexset[b >> 4], f) != 0)
				return -1;
			if (sputc(strhexset[b & 0xF], f) != 0)
				return -1;

			++data;
			--size;
		}
	}

	return 0;
}

#if HAVE_FSYNC
int ssync(STREAM* s)
{
	unsigned i;

	for(i=0;i<s->handle_size;++i) {
		if (fsync(s->handle[i].f) != 0) {
			s->state = STREAM_STATE_ERROR;
			s->state_index = i;
			return -1;
		}
	}

	return 0;
}
#endif

/****************************************************************************/
/* path */

void pathcpy(char* dst, size_t size, const char* src)
{
	size_t len = strlen(src);

	if (len + 1 > size) {
		fprintf(stderr, "Path too long\n");
		exit(EXIT_FAILURE);
	}

	memcpy(dst, src, len + 1);
}

void pathcat(char* dst, size_t size, const char* src)
{
	size_t dst_len = strlen(dst);
	size_t src_len = strlen(src);

	if (dst_len + src_len + 1 > size) {
		fprintf(stderr, "Path too long\n");
		exit(EXIT_FAILURE);
	}

	memcpy(dst + dst_len, src, src_len + 1);
}

void pathimport(char* dst, size_t size, const char* src)
{
	pathcpy(dst, size, src);

#ifdef _WIN32
	/* convert all Windows '\' to '/' */
	while (*dst) {
		if (*dst == '\\')
			*dst = '/';
		++dst;
	}
#endif
}

void pathprint(char* dst, size_t size, const char* format, ...)
{
	size_t len;
	va_list ap;
	
	va_start(ap, format);
	len = vsnprintf(dst, size, format, ap);
	va_end(ap);

	if (len >= size) {
		fprintf(stderr, "Path too long\n");
		exit(EXIT_FAILURE);
	}
}

void pathslash(char* dst, size_t size)
{
	size_t len = strlen(dst);

	if (len > 0 && dst[len - 1] != '/') {
		if (len + 2 >= size) {
			fprintf(stderr, "Path too long\n");
			exit(EXIT_FAILURE);
		}

		dst[len] = '/';
		dst[len+1] = 0;
	}
}

int pathcmp(const char* a, const char* b)
{
#ifdef _WIN32
	char ai[PATH_MAX];
	char bi[PATH_MAX];

	/* import to convert \ to / */
	pathimport(ai, sizeof(ai), a);
	pathimport(bi, sizeof(bi), b);

	/* case insensitive compare in Windows */
	return stricmp(ai, bi);
#else
	return strcmp(a, b);
#endif
}

/****************************************************************************/
/* filesystem */

int mkancestor(const char* file)
{
	char dir[PATH_MAX];
	char* c;
	
	pathcpy(dir, sizeof(dir), file);
	
	c = strrchr(dir, '/');
	if (!c) {
		/* no ancestor */
		return 0;
	}

	/* clear the file */
	*c = 0;

	if (*dir == 0) {
		/* nothing more to do */
		return 0;
	}

	/* try creating it  */
	if (mkdir(dir, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) == 0) {
		/* just created */
		return 0;
	}
	if (errno == EEXIST) {
		/* it already exists */
		return 0;
	}

	/* recursively create them all */
	if (mkancestor(dir) != 0) {
		return -1;
	}

	/* create it */
	if (mkdir(dir, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) != 0) {
		fprintf(stderr, "Error creating directory '%s'. %s.\n", dir, strerror(errno));
		return -1;
	}

	return 0;
}

/****************************************************************************/
/* mem */

static size_t mcounter;

size_t malloc_counter(void)
{
	return mcounter;
}

void* malloc_nofail(size_t size)
{
	void* ptr = malloc(size);

	if (!ptr) {
		/* don't use printf to avoid any possible extra allocation */
		write(2, "Low Memory\n", 11);
		exit(EXIT_FAILURE);
	}

#ifndef CHECKER /* Don't preinitialize when running for valgrind */
	/* Here we preinitialize the memory to ensure that the OS is really allocating it */
	/* and not only reserving the addressable space. */
	/* Otherwise we are risking that the OOM (Out Of Memory) killer in Linux will kill the process. */
	/* Filling the memory doesn't ensure to disable OOM, but it increase a lot the chances to */
	/* get a real error from malloc() instead than a process killed. */
	/* Note that calloc() doesn't have the same effect. */
	memset(ptr, 0xA5, size);
#endif

	mcounter += size;

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

char* strdup_nofail(const char* str)
{
	size_t size;
	char* ptr;

	size = strlen(str) + 1;

	ptr = malloc(size);

	if (!ptr) {
		/* don't use printf to avoid any possible extra allocation */
		write(2, "Low Memory\n", 11);
		exit(EXIT_FAILURE);
	}

	memcpy(ptr, str, size);

	mcounter += size;

	return ptr;
}

/****************************************************************************/
/* hash */

/* Rotate left 32 */
inline uint32_t rotl32(uint32_t x, int8_t r)
{
	return (x << r) | (x >> (32 - r));
}

inline uint64_t rotl64(uint64_t x, int8_t r)
{
	return (x << r) | (x >> (64 - r));
}

/* Swap endianess */
#if HAVE_BYTESWAP_H

#include <byteswap.h>

#define swap32(x) bswap_32(x)
#define swap64(x) bswap_64(x)

#else
static inline uint32_t swap32(uint32_t v)
{
	return (rotl32(v, 8) & 0x00ff00ff)
		| (rotl32(v, 24) & 0xff00ff00);
}

static inline uint64_t swap64(uint64_t v)
{
	return (rotl64(v, 8) & 0x000000ff000000ffLLU)
		| (rotl64(v, 24) & 0x0000ff000000ff00LLU)
		| (rotl64(v, 40) & 0x00ff000000ff0000LLU)
		| (rotl64(v, 56) & 0xff000000ff000000LLU);
}
#endif

#include "murmurhash3.c"
#include "spookyhash2.c"

void memhash(unsigned kind, const unsigned char* seed, void* digest, const void* src, unsigned size)
{
	uint32_t seed32;
	uint64_t seed64[2];
	switch (kind) {
	case HASH_MURMUR3 :
		seed32 = ((uint32_t*)seed)[0] ^ ((uint32_t*)seed)[1] ^ ((uint32_t*)seed)[2] ^ ((uint32_t*)seed)[3];
#if WORDS_BIGENDIAN
		seed32 = swap32(seed32);
#endif
		MurmurHash3_x86_128(src, size, seed32, digest);
		break;
	case HASH_SPOOKY2 :
		seed64[0] = ((uint64_t*)seed)[0];
		seed64[1] = ((uint64_t*)seed)[1];
#if WORDS_BIGENDIAN
		seed64[0] = swap64(seed64[0]);
		seed64[1] = swap64(seed64[1]);
#endif
		SpookyHash128(src, size, seed64[0], seed64[1], digest);
		break;
	default:
		fprintf(stderr, "Internal inconsistency in hash function %u\n", kind);
		exit(EXIT_FAILURE);
		break;
	}
}

/****************************************************************************/
/* random */

void randomize(void* void_ptr, unsigned size)
{
	unsigned char* ptr = void_ptr;
	unsigned i;

	srand(time(0));

	for(i=0;i<size;++i)
		ptr[i] = rand();
}

