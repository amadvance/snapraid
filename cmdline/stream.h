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

#ifndef __STREAM_H
#define __STREAM_H

#include "util.h"

/****************************************************************************/
/* stream */

/**
 * Size of the buffer of the stream.
 *
 * It's not a constant for testing purpose.
 */
extern unsigned STREAM_SIZE;

#define STREAM_STATE_READ 0 /**< The stream is in a normal state of read. */
#define STREAM_STATE_WRITE 1 /**< The stream is in a normal state of write. */
#define STREAM_STATE_ERROR -1 /**< An error was encountered. */
#define STREAM_STATE_EOF 2 /**< The end of file was encountered. */

struct stream_handle {
	int f; /**< Handle of the file. */
	char path[PATH_MAX]; /**< Path of the file. */
};

struct stream {
	unsigned char* buffer; /**< Buffer of the stream. */
	unsigned char* pos; /**< Current position in the buffer. */
	unsigned char* end; /**< End position of the buffer. */
	int state; /**< State of the stream. One of STREAM_STATE. */
	int state_index; /**< Index of the handle causing a state change. */
	unsigned handle_size; /**< Number of handles. */
	struct stream_handle* handle; /**< Set of handles. */
	off_t offset; /**< Offset into the file. */
	off_t offset_uncached; /**< Offset into the file excluding the cached data. */

	/**
	 * CRC of the data read or written in the file.
	 *
	 * If reading, it's the CRC of all data read from the file,
	 * including the one in the buffer.
	 * If writing it's all the data wrote to the file,
	 * excluding the one still in buffer yet to be written.
	 */
	uint32_t crc;

	/**
	 * CRC of the file excluding the cached data in the buffer.
	 *
	 * If reading, it's the CRC of the data read from the file,
	 * excluding the one in the buffer.
	 * If writing it's all the data wrote to the file,
	 * excluding the one still in buffer yet to be written.
	 */
	uint32_t crc_uncached;

	/**
	 * CRC of the data written to the stream.
	 *
	 * This is an extra check of the data that is written to
	 * file to ensure that it's consistent even in case
	 * of memory errors.
	 *
	 * This extra check takes about 2 seconds for each GB of
	 * content file with the Intel CRC instruction,
	 * and about 4 seconds without it.
	 * But usually this doesn't slow down the write process,
	 * as the disk is the bottle-neck.
	 *
	 * Note that this CRC doesn't have the IV processing.
	 *
	 * Not used in reading.
	 * In writing, it's all the data wrote calling sput() functions.
	 */
	uint32_t crc_stream;
};

/**
 * Opaque STREAM type. Like ::FILE.
 */
typedef struct stream STREAM;

/**
 * Open a stream for reading. Like fopen("r").
 */
STREAM* sopen_read(const char* file);

/**
 * Open a stream for writing. Like fopen("w").
 */
STREAM* sopen_write(const char* file);

/**
 * Open a set of streams for writing. Like fopen("w").
 */
STREAM* sopen_multi_write(unsigned count);

/**
 * Specify the file to open.
 */
int sopen_multi_file(STREAM* s, unsigned i, const char* file);

/**
 * Close a stream. Like fclose().
 */
int sclose(STREAM* s);

/**
 * Return the handle of the file.
 * In case of multi file, the first one is returned.
 */
int shandle(STREAM* s);

/**
 * Read the stream until the end, and return the latest 4 chars.
 * The CRC of the file is also computed, and you can get it using scrc().
 * \return 0 on success, or EOF on error.
 */
int sdeplete(STREAM* s, unsigned char* last);

/**
 * Flush the write stream buffer.
 * \return 0 on success, or EOF on error.
 */
int sflush(STREAM* s);

/**
 * Get the file pointer.
 */
int64_t stell(STREAM* s);

/**
 * Get the CRC of the processed data.
 */
uint32_t scrc(STREAM* s);

/**
 * Get the CRC of the processed data in put.
 */
uint32_t scrc_stream(STREAM* s);

/**
 * Check if the buffer has enough data loaded.
 */
static inline int sptrlookup(STREAM* s, int size)
{
	return s->pos + size <= s->end;
}

/**
 * Get the current stream ptr.
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
 * Check the error status. Like ferror().
 */
static inline int serror(STREAM* s)
{
	return s->state == STREAM_STATE_ERROR;
}

/**
 * Check the eof status. Like feof().
 */
static inline int seof(STREAM* s)
{
	return s->state == STREAM_STATE_EOF;
}

/**
 * Get the index of the handle that caused the error.
 */
static inline int serrorindex(STREAM* s)
{
	return s->state_index;
}

/**
 * Get the path of the handle that caused the error.
 */
static inline const char* serrorfile(STREAM* s)
{
	return s->handle[s->state_index].path;
}

/**
 * Sync the stream. Like fsync().
 */
int ssync(STREAM* s);

/****************************************************************************/
/* get */

/**
 * \internal Used by sgetc().
 * \note Don't call this directly, but use sgetc().
 */
int sgetc_uncached(STREAM* s);

/**
 * Read a char. Like fgetc().
 */
static inline int sgetc(STREAM* s)
{
	if (tommy_unlikely(s->pos == s->end))
		return sgetc_uncached(s);
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
 * Read a fixed amount of chars.
 * Return 0 on success, or -1 on error.
 */
int sread(STREAM* f, void* void_data, unsigned size);

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
 * Return the number of spaces and tabs read.
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
 * Stop at the first ' ', '\t', '\n' or EOF.
 * Return <0 if the buffer is too small, or the number of chars read.
 */
int sgettok(STREAM* f, char* str, int size);

/**
 * Read until the end of line.
 * Stop at the first '\n' or EOF. Note that '\n' is left in the stream.
 * Return <0 if the buffer is too small, or the number of chars read.
 */
int sgetline(STREAM* f, char* str, int size);

/**
 * Like sgetline() but remove ' ' and '\t' at the end.
 */
int sgetlasttok(STREAM* f, char* str, int size);

/**
 * Read a 32 bit number.
 * Stop at the first not digit char or EOF.
 * Return <0 if there isn't enough to read.
 */
int sgetu32(STREAM* f, uint32_t* value);

/****************************************************************************/
/* binary get */

/**
 * Read a binary 32 bit number in packet format.
 * Return <0 if there isn't enough to read.
 */
int sgetb32(STREAM* f, uint32_t* value);

/**
 * Read a binary 64 bit number in packet format.
 * Return <0 if there isn't enough to read.
 */
int sgetb64(STREAM* f, uint64_t* value);

/**
 * Read a binary 32 bit number in little endian format.
 * Return <0 if there isn't enough to read.
 */
int sgetble32(STREAM* f, uint32_t* value);

/**
 * Read a binary string.
 * Return -1 on error or if the buffer is too small, or the number of chars read.
 */
int sgetbs(STREAM* f, char* str, int size);

/****************************************************************************/
/* put */

/**
 * Write a char. Like fputc().
 * Return 0 on success or -1 on error.
 */
static inline int sputc(int c, STREAM* s)
{
	if (s->pos == s->end) {
		if (sflush(s) != 0)
			return -1;
	}

	/**
	 * Update the crc *before* writing the data in the buffer
	 *
	 * This must be done before the memory write,
	 * to be able to detect memory errors on the buffer,
	 * happening before we write it on the file.
	 */
	s->crc_stream = crc32c_plain_char(s->crc_stream, c);

	*s->pos++ = c;

	return 0;
}

/**
 * Write a end of line.
 * Return 0 on success or -1 on error.
 */
static inline int sputeol(STREAM* s)
{
#ifdef _WIN32
	if (sputc('\r', s) != 0)
		return -1;
#endif
	return sputc('\n', s);
}

/**
 * Write a sized string.
 * Return 0 on success or -1 on error.
 */
int swrite(const void* data, unsigned size, STREAM* f);

/****************************************************************************/
/* binary put */

/**
 * Write a binary 32 bit number in packed format.
 * Return 0 on success or -1 on error.
 */
int sputb32(uint32_t value, STREAM* s);

/**
 * Write a binary 64 bit number in packed format.
 * Return 0 on success or -1 on error.
 */
int sputb64(uint64_t value, STREAM* s);

/**
 * Write a binary 32 bit number in little endian format.
 * Return 0 on success or -1 on error.
 */
int sputble32(uint32_t value, STREAM* s);

/**
 * Write a binary string.
 * Return 0 on success or -1 on error.
 */
int sputbs(const char* str, STREAM* s);

#endif

