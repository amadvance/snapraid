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

#ifndef __SUPPORT_H
#define __SUPPORT_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/****************************************************************************/
/* lock */

/**
 * Initialize and destroy the locks.
 */
void lock_init(void);
void lock_done(void);

/**
 * Lock used for accessing the global state.
 */
void lock_state(void);
void unlock_state(void);

/****************************************************************************/
/* string */

typedef struct ss {
	char* ptr;
	size_t size;
	size_t len;
} ss_t;

void ss_init(struct ss* s);
void ss_done(struct ss* s);
void ss_write(struct ss* s, const char* arg, size_t len);
void ss_prints(struct ss* s, const char* arg);
int ss_vprintf(struct ss* s, const char* fmt, va_list ap);
int ss_printf(struct ss* s, const char* fmt, ...)  __attribute__((format(attribute_printf, 2, 3)));
void ss_jsons(struct ss* s, int tab, const char* arg);
int ss_jsonf(struct ss* s, int tab, const char* fmt, ...)  __attribute__((format(attribute_printf, 3, 4)));

static inline size_t ss_len(struct ss* s)
{
	return s->len;
}

static inline const char* ss_ptr(struct ss* s)
{
	return s->ptr;
}

/**
 * Copy a string limiting the size.
 * Abort if too long.
 */
void scpy(char* dst, size_t size, const char* src);

int si64(int64_t* out, const char* src);
int su64(uint64_t* out, const char* src);
int si(int* out, const char* src);

/****************************************************************************/
/* memory */

/**
 * Safe malloc.
 * If no memory is available, it aborts.
 */
void* malloc_nofail(size_t size);

/**
 * Safe calloc.
 * If no memory is available, it aborts.
 */
void* calloc_nofail(size_t count, size_t size);

/**
 * Safe recalloc.
 * If no memory is available, it aborts.
 */
void* realloc_nofail(void* ptr, size_t size);

/**
 * Safe strdup.
 * If no memory is available, it aborts.
 */
char* strdup_nofail(const char* str);

#endif

