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
 * Marks the end of the current token, and move to the next one.
 */
static inline char* strtoken(char* s)
{
	while (*s && *s != ' ')
		++s;
	if (*s)
		*s++ = 0;
	while (*s == ' ')
		++s;
	return s;
}

/**
 * Move to the next token skipping any separator.
 */
static inline char* strskip(char* s)
{
	while (*s == ' ')
		++s;
	return s;
}

/**
 * Gets a text line from a file.
 * Returns -1 on error, 0 on oef, and 1 if more data is available.
 */
int strgets(char* s, unsigned size, FILE* f);

/**
 * Convert a string to a 32 bit unsigned.
 * Returns 0 on success.
 */
int stru32(const char* s, uint32_t* value);

/**
 * Convert a string to a 64 bit unsigned.
 * Returns 0 on success.
 */
int stru64(const char* s, uint64_t* value);

/**
 * Encodes to hex.
 */
void strenchex(char* str, const void* void_data, unsigned data_len);

/**
 * Decodes from hex.
 */
char* strdechex(void* void_data, unsigned data_len, char* str);

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

