/*
 * Copyright (C) 2025 Andrea Mazzoleni
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

/****************************************************************************/
/* lock */

/**
 * Lock used for accessing the state
 */
static pthread_mutex_t state_lock;

void lock_msg(void)
{
	pthread_mutex_lock(&state_lock);
}

void unlock_msg(void)
{
	pthread_mutex_unlock(&state_lock);
}

void lock_init(void)
{
	pthread_mutex_init(&state_lock, 0);
}

void lock_done(void)
{
	pthread_mutex_destroy(&state_lock);
}

/****************************************************************************/
/* string */

void ss_init(struct ss* s)
{
	s->ptr = 0;
	s->size = 0;
	s->len = 0;
}

static void ss_reserve(struct ss* s, size_t needed) 
{
	size_t new_size;
	
	needed += s->len;

	if (s->size >= needed)
		return;

	new_size = s->size;
	if (new_size < 16)
		new_size = 16; 

	while (new_size < needed)
		new_size *= 2;

	s->ptr = realloc_nofail(s->ptr, new_size);
	s->size = new_size;
}

void ss_done(struct ss* s)
{
	free(s->ptr);
}

void ss_write(struct ss* s, const char* arg, size_t len)
{
	ss_reserve(s, len);
	memcpy(s->ptr + s->len, arg, len);
	s->len += len;
}

void ss_prints(struct ss* s, const char* arg)
{
	size_t len = strlen(arg);
	ss_reserve(s, len);
	memcpy(s->ptr + s->len, arg, len);
	s->len += len;
}

int ss_vprintf(struct ss* s, const char* fmt, va_list ap)
{
	size_t available;
	ssize_t needed;
	va_list ap_retry;

	available = s->size - s->len;

	va_copy(ap_retry, ap);

	needed = vsnprintf(s->ptr + s->len, available, fmt, ap);
	if (needed < 0) {
		va_end(ap_retry);
		return -1;
	}

	if ((size_t)needed >= available) { /* truncation occurred */
		ss_reserve(s, (size_t)needed + 1);

		vsnprintf(s->ptr + s->len, (size_t)needed + 1, fmt, ap_retry);
	}

	s->len += (size_t)needed;
	va_end(ap_retry);
	
	return 0;
}

int ss_printf(struct ss* s, const char* fmt, ...) 
{
	va_list ap;
	int ret;

	va_start(ap, fmt);
	ret = ss_vprintf(s, fmt, ap);
	va_end(ap);

	return ret;
}

void ss_jsons(struct ss* s, int tab, const char* arg)
{
	while (tab > 0) {
		ss_write(s, "  ", 2);
		--tab;
	}

	ss_prints(s, arg);
}

int ss_jsonf(struct ss* s, int tab, const char* fmt, ...)
{
	va_list ap;
	int ret;

	while (tab > 0) {
		ss_write(s, "  ", 2);
		--tab;
	}

	va_start(ap, fmt);
	ret = ss_vprintf(s, fmt, ap);
	va_end(ap);

	return ret;
}

void scpy(char* dst, size_t size, const char* src)
{
	size_t len = strlen(src);

	if (len + 1 > size) {
		/* LCOV_EXCL_START */
		abort();
		/* LCOV_EXCL_STOP */
	}

	memcpy(dst, src, len + 1);
}

void scat(char* dst, size_t size, const char* src)
{
	size_t dst_len = strlen(dst);
	size_t src_len = strlen(src);

	if (dst_len + src_len + 1 > size) {
		/* LCOV_EXCL_START */
		abort();
		/* LCOV_EXCL_STOP */
	}

	memcpy(dst + dst_len, src, src_len + 1);
}

int si(int* out, const char* s)
{
	char* e;
	long v;

	errno = 0;
	v = strtol(s, &e, 10);
	if (errno != 0)
		return -1; /* overflow or underflow */

	if (e == s || *e != '\0')
		return -1; /* not a valid number */

	if (v < INT_MIN || v > INT_MAX)
		return -1; /* outside int range */

	*out = v;
	return 0;
}

int si64(int64_t* out, const char* s)
{
	char* e;
	long long v;

	errno = 0;
	v = strtoll(s, &e, 10);
	if (errno != 0)
		return -1; /* overflow or underflow */

	if (e == s || *e != '\0')
		return -1; /* not a valid number */

	*out = v;
	return 0;
}

int su64(uint64_t* out, const char* s)
{
	char* e;
	unsigned long long v;

	errno = 0;
	v = strtoull(s, &e, 10);
	if (errno != 0)
		return -1; /* overflow or underflow */

	if (e == s || *e != '\0')
		return -1; /* not a valid number */

	*out = v;
	return 0;
}

/****************************************************************************/
/* memory */

void* malloc_nofail(size_t size)
{
	void* ptr = malloc(size);

	if (!ptr) {
		/* LCOV_EXCL_START */
		abort();
		/* LCOV_EXCL_STOP */
	}

	return ptr;
}

void* calloc_nofail(size_t count, size_t size)
{
	void* ptr = calloc(count, size);

	if (!ptr) {
		/* LCOV_EXCL_START */
		abort();
		/* LCOV_EXCL_STOP */
	}

	return ptr;
}

void* realloc_nofail(void* ptr, size_t size)
{
	ptr = realloc(ptr, size);

	if (!ptr) {
		/* LCOV_EXCL_START */
		abort();
		/* LCOV_EXCL_STOP */
	}

	return ptr;
}

char* strdup_nofail(const char* str)
{
	char* ptr = strdup(str);

	if (!ptr) {
		/* LCOV_EXCL_START */
		abort();
		/* LCOV_EXCL_STOP */
	}

	return ptr;
}
