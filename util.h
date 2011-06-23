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
/* get */

/**
 * Get a char from a stream.
 */
static inline int strgetc(FILE* f)
{
#if HAVE_GETC_UNLOCKED
	return getc_unlocked(f);
#else
	return getc(f);
#endif
}

/**
 * Get a char from a stream, ignoring one '\r'.
 */
static inline int strgetcnl(FILE* f)
{
	int c;

	c = strgetc(f);
	if (c == '\r')
		c = strgetc(f);

	return c;
}

/**
 * Read all the spaces and tabs.
 * Returns the number of spaces and tabs read.
 */
static inline int strgetspace(FILE* f)
{
	int count = 0;
	int c;

	c = strgetc(f);
	while (c == ' ' || c == '\t') {
		++count;
		c = strgetc(f);
	}

	ungetc(c, f);
	return count;
}

/**
 * Read until the first space or tab.
 * Stops at the first ' ', '\t', '\n' or EOF.
 * Returns error if the buffer is too small.
 */
int strgettoken(FILE* f, char* str, unsigned size);

/**
 * Read until the end of line.
 * Stops at the first '\n' or EOF.
 * Returns error if the buffer is too small.
 */
int strgetline(FILE* f, char* str, unsigned size);

/**
 * Read a 32 bit number.
 * Stops at the first not digit char or EOF.
 * Returns error if there isn't enough to read.
 */
int strgetu32(FILE* f, uint32_t* value);

/**
 * Read a 64 bit number.
 * Stops at the first not digit char.
 * Returns error if there isn't enough to read.
 */
int strgetu64(FILE* f, uint64_t* value);

/**
 * Read an hexadecimal string of fixed length.
 * Returns error if there isn't enough to read.
 */
int strgethex(FILE* f, void* void_data, unsigned data_len);

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

