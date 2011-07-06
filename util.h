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

#ifndef __UTIL_H
#define __UTIL_H

/****************************************************************************/
/* string */

/**
 * Encodes to hex.
 */
void strenchex(char* str, const void* void_data, unsigned data_len);

/**
 * Decodes from hex.
 */
char* strdechex(void* void_data, unsigned data_len, char* str);

/****************************************************************************/
/* stream */

#define STREAM_SIZE (64*1024)

#define STREAM_OK 0
#define STREAM_ERROR -1
#define STREAM_EOF 1

struct stream {
	unsigned char* buffer;
	unsigned char* pos;
	unsigned char* end;
	int state;
	int f;
};

/**
 * Opaque STREAM type. Like ::FILE.
 */
typedef struct stream STREAM;

/**
 * Open a stream for reading. Like fopen().
 */
STREAM* sopen_read(const char* file);

/**
 * Close a stream. Like fclose().
 */
void sclose(STREAM* s);

/**
 * Fill the stream buffer and read a char.
 */
int sflow(STREAM* s);

/**
 * Checks if the buffer has enough data loaded.
 */
static inline int sptrlookup(STREAM* s, int size)
{
	return s->pos + size <= s->end;
}

/**
 * Gets the current stream ptr.
 */
static inline unsigned char* sptrget(STREAM* s)
{
	return s->pos;
}

/**
 * Set the current stream ptr.
 */
static inline void sptrset(STREAM* s, unsigned char* ptr)
{
	s->pos = ptr;
}

/**
 * Read a char. Like fgetc().
 */
static inline int sgetc(STREAM* s)
{
	if (s->pos == s->end)
		return sflow(s);
	return *s->pos++;
}

/**
 * Unread a char.
 * Like ungetc() but you have to unget the same char read.
 */
static inline void sungetc(int c, STREAM* s)
{
	if (c != EOF)
		--s->pos;
}

/**
 * Check the error status. Like ferror().
 */
static inline int serror(STREAM* s)
{
	return s->state == STREAM_ERROR;
}

/**
 * Get a char from a stream, ignoring one '\r'.
 */
static inline int sgeteol(STREAM* f)
{
	int c;

	c = sgetc(f);
	if (c == '\r')
		c = sgetc(f);

	return c;
}

/**
 * Read all the spaces and tabs.
 * Returns the number of spaces and tabs read.
 */
static inline int sgetspace(STREAM* f)
{
	int count = 0;
	int c;

	c = sgetc(f);
	while (c == ' ' || c == '\t') {
		++count;
		c = sgetc(f);
	}

	sungetc(c, f);
	return count;
}

/**
 * Read until the first space or tab.
 * Stops at the first ' ', '\t', '\n' or EOF.
 * Returns <0 if the buffer is too small, or the number of chars read.
 */
int sgettok(STREAM* f, char* str, int size);

/**
 * Read until the end of line.
 * Stops at the first '\n' or EOF.
 * Returns <0 if the buffer is too small, or the number of chars read.
 */
int sgetline(STREAM* f, char* str, int size);

/**
 * Like sgetline() but removes ' ' and '\t' at the end.
 */
int sgetlasttok(STREAM* f, char* str, int size);

/**
 * Read a 32 bit number.
 * Stops at the first not digit char or EOF.
 * Returns <0 if there isn't enough to read.
 */
int sgetu32(STREAM* f, uint32_t* value);

/**
 * Read a 64 bit number.
 * Stops at the first not digit char.
 * Returns <0 if there isn't enough to read.
 */
int sgetu64(STREAM* f, uint64_t* value);

/**
 * Read an hexadecimal string of fixed length.
 * Returns <0 if there isn't enough to read.
 */
int sgethex(STREAM* f, void* data, int size);

/****************************************************************************/
/* path */

/**
 * Copies a path limiting the size.
 * Aborts if too long.
 */
void pathcpy(char* str, size_t size, const char* src);

/**
 * Imports a path limiting the size.
 * In Windows all the backslash are converted to the C standard of forward slash.
 * Aborts if too long.
 */
void pathimport(char* str, size_t size, const char* src);

/**
 * Prints a path.
 * Aborts if too long.
 */
void pathprint(char* str, size_t size, const char* format, ...);

/**
 * Ensures the presence of a terminating slash, if it isn't empty.
 * Aborts if too long. 
 */
void pathslash(char* str, size_t size);

/****************************************************************************/
/* memory */

/**
 * Safe malloc.
 * If no memory is available, it aborts.
 */
void* malloc_nofail(size_t size);

/**
 * Safe aligned malloc.
 * If no memory is available, it aborts.
 */
void* malloc_nofail_align(size_t size, void** freeptr);

/****************************************************************************/
/* hash */

/**
 * Size of the hash.
 */

#define HASH_SIZE 16

/**
 * Hash kinds.
 */
#define HASH_MURMUR3 0
#define HASH_MD5 1

/**
 * Computes the HASH of a memory block.
 */
void memhash(unsigned kind, void* digest, const void* src, unsigned size);

#endif

