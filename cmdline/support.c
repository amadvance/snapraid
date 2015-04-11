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

/****************************************************************************/
/* print */

int msg_level = 0;

/**
 * Mutex used for printf.
 *
 * In Windows printf() is not atomic, and multiple threads
 * will have output interleaved.
 *
 * Note that even defining __USE_MINGW_ANSI_STDIO the problem persists.
 *
 * See for example:
 *
 * Weird output when I use pthread and printf.
 * http://stackoverflow.com/questions/13190254/weird-output-when-i-use-pthread-and-printf
 *
 * This is also required in other OS because we split output in stdlog in
 * two fprintf calls.
 */
static pthread_mutex_t msg_lock = PTHREAD_MUTEX_INITIALIZER;

/*
 * Note that in the following functions we always flush both
 * stdout and stderr, because we want to ensure that they mixes
 * well when redirected to files
 *
 * The buffering is similar at the "line buffered" one, that
 * is not available on Windows, so we emulate it in this way.
 *
 * For stdlog the flush is only limited.
 * To ensure flushing the caller should use msg_flush().
 */

void msg_error(const char* format, ...)
{
	va_list ap;

	pthread_mutex_lock(&msg_lock);

	if (stdlog) {
		va_start(ap, format);
		fprintf(stdlog, "msg:error: ");
		vfprintf(stdlog, format, ap);
		fflush(stdlog);
		va_end(ap);
	}

	va_start(ap, format);
	vfprintf(stderr, format, ap);
	fflush(stderr);
	va_end(ap);

	pthread_mutex_unlock(&msg_lock);
}

void msg_warning(const char* format, ...)
{
	va_list ap;

	pthread_mutex_lock(&msg_lock);

	if (stdlog) {
		va_start(ap, format);
		fprintf(stdlog, "msg:warning: ");
		vfprintf(stdlog, format, ap);
		fflush(stdlog);
		va_end(ap);
	} else {
		va_start(ap, format);
		vfprintf(stderr, format, ap);
		fflush(stderr);
		va_end(ap);
	}

	pthread_mutex_unlock(&msg_lock);
}

void msg_tag(const char* format, ...)
{
	va_list ap;

	pthread_mutex_lock(&msg_lock);

	if (stdlog) {
		va_start(ap, format);
		vfprintf(stdlog, format, ap);
		va_end(ap);
	}

	pthread_mutex_unlock(&msg_lock);
}

void msg_status(const char* format, ...)
{
	va_list ap;

	pthread_mutex_lock(&msg_lock);

	if (stdlog) {
		va_start(ap, format);
		fprintf(stdlog, "msg:status: ");
		vfprintf(stdlog, format, ap);
		fflush(stdlog);
		va_end(ap);
	}

	if (msg_level >= MSG_STATUS) {
		va_start(ap, format);
		vfprintf(stdout, format, ap);
		fflush(stdout);
		va_end(ap);
	}

	pthread_mutex_unlock(&msg_lock);
}

void msg_info(const char* format, ...)
{
	va_list ap;

	pthread_mutex_lock(&msg_lock);

	/* don't output in stdlog as these messages */
	/* are always paired with a msg_tag() call */

	if (msg_level >= MSG_INFO) {
		va_start(ap, format);
		vfprintf(stdout, format, ap);
		fflush(stdout);
		va_end(ap);
	}

	pthread_mutex_unlock(&msg_lock);
}

void msg_progress(const char* format, ...)
{
	va_list ap;

	pthread_mutex_lock(&msg_lock);

	if (stdlog) {
		va_start(ap, format);
		fprintf(stdlog, "msg:progress: ");
		vfprintf(stdlog, format, ap);
		fflush(stdlog);
		va_end(ap);
	}

	if (msg_level >= MSG_PROGRESS) {
		va_start(ap, format);
		vfprintf(stdout, format, ap);
		fflush(stdout);
		va_end(ap);
	}

	pthread_mutex_unlock(&msg_lock);
}

void msg_bar(const char* format, ...)
{
	va_list ap;

	pthread_mutex_lock(&msg_lock);

	/* don't output in stdlog as these messages */
	/* are intended for screen only */
	/* also don't flush stdout as they are intended to be partial messages */

	if (msg_level >= MSG_BAR) {
		va_start(ap, format);
		vfprintf(stdout, format, ap);
		va_end(ap);
	}

	pthread_mutex_unlock(&msg_lock);
}

void msg_verbose(const char* format, ...)
{
	va_list ap;

	pthread_mutex_lock(&msg_lock);

	if (stdlog) {
		va_start(ap, format);
		fprintf(stdlog, "msg:verbose: ");
		vfprintf(stdlog, format, ap);
		fflush(stdlog);
		va_end(ap);
	}

	if (msg_level >= MSG_VERBOSE) {
		va_start(ap, format);
		vfprintf(stdout, format, ap);
		fflush(stdout);
		va_end(ap);
	}

	pthread_mutex_unlock(&msg_lock);
}

void msg_flush(void)
{
	pthread_mutex_lock(&msg_lock);

	if (stdlog)
		fflush(stdlog);
	fflush(stdout);
	fflush(stderr);

	pthread_mutex_unlock(&msg_lock);
}

/**
 * Number of buffers for esc().
 */
#define ESC_MAX 4

/**
 * Buffer table to allow multiple calls to esc().
 */
static char esc_table[ESC_MAX][PATH_MAX*2+1];

/**
 * Next buffer to use.
 */
static unsigned esc_index = 0;

#define ESCAPE(from,to) \
	case from : \
		if (p == end) \
			goto bail; \
		*p++ = '\\'; \
		if (p == end) \
			goto bail; \
		*p++ = to; \
		break

const char* esc(const char* str)
{
	char* begin = esc_table[esc_index];
	char* end = begin + PATH_MAX;
	char* p = begin;

	/* next call uses the next buffer */
	esc_index = (esc_index + 1) % ESC_MAX;

	/* copy string with escaping */
	while (*str) {
		char c = *str;

		switch (c) {

		ESCAPE('\n', 'n');
		ESCAPE('\r', 'r');
		ESCAPE(':', 'd');
		ESCAPE('\\', '\\');

		default:
			if (p == end)
				goto bail;
			*p++ = c;
			break;
		}

		++str;
	}

	/* put final 0 */
	if (p == end)
		goto bail;
	*p = 0;

	return begin;

bail:
	/* LCOV_EXCL_START */
	msg_error("Escape too long\n");
	exit(EXIT_FAILURE);
	/* LCOV_EXCL_STOP */
}

/****************************************************************************/
/* path */

void pathcpy(char* dst, size_t size, const char* src)
{
	size_t len = strlen(src);

	if (len + 1 > size) {
		/* LCOV_EXCL_START */
		msg_error("Path too long\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	memcpy(dst, src, len + 1);
}

void pathcat(char* dst, size_t size, const char* src)
{
	size_t dst_len = strlen(dst);
	size_t src_len = strlen(src);

	if (dst_len + src_len + 1 > size) {
		/* LCOV_EXCL_START */
		msg_error("Path too long\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	memcpy(dst + dst_len, src, src_len + 1);
}

void pathcatc(char* dst, size_t size, char c)
{
	size_t dst_len = strlen(dst);

	if (dst_len + 2 > size) {
		/* LCOV_EXCL_START */
		msg_error("Path too long\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	dst[dst_len] = c;
	dst[dst_len + 1] = 0;
}

void pathimport(char* dst, size_t size, const char* src)
{
	pathcpy(dst, size, src);

#ifdef _WIN32
	/* convert all Windows '\' to C '/' */
	while (*dst) {
		if (*dst == '\\')
			*dst = '/';
		++dst;
	}
#endif
}

void pathexport(char* dst, size_t size, const char* src)
{
	pathcpy(dst, size, src);

#ifdef _WIN32
	/* convert all C '/' to Windows '\' */
	while (*dst) {
		if (*dst == '/')
			*dst = '\\';
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
		/* LCOV_EXCL_START */
		msg_error("Path too long\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}
}

void pathslash(char* dst, size_t size)
{
	size_t len = strlen(dst);

	if (len > 0 && dst[len - 1] != '/') {
		if (len + 2 >= size) {
			/* LCOV_EXCL_START */
			msg_error("Path too long\n");
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		dst[len] = '/';
		dst[len + 1] = 0;
	}
}

void pathcut(char* dst)
{
	char* slash = strrchr(dst, '/');
	if (slash)
		slash[1] = 0;
	else
		dst[0] = 0;
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
	struct stat st;
	char* c;

	pathcpy(dir, sizeof(dir), file);

	c = strrchr(dir, '/');
	if (!c) {
		/* no ancestor */
		return 0;
	}

	/* clear the file */
	*c = 0;

	/* if it's the root dir */
	if (*dir == 0) {
		/* nothing more to do */
		return 0;
	}

#ifdef _WIN32
	/* if it's a drive specificaion like "C:" */
	if (isalpha(dir[0]) && dir[1] == ':' && dir[2] == 0) {
		/* nothing more to do */
		return 0;
	}
#endif

	/*
	 * Check if the dir already exists using lstat().
	 *
	 * Note that in Windows when dealing with read-only media
	 * you cannot try to create the directory, and expecting
	 * the EEXIST error because the call will fail with ERROR_WRITE_PROTECTED.
	 *
	 * Also in Windows it's better to use lstat() than stat() because it
	 * doen't need to open the dir with CreateFile().
	 */
	if (lstat(dir, &st) == 0) {
		/* it already exists */
		return 0;
	}

	/* recursively create them all */
	if (mkancestor(dir) != 0) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	/* create it */
	if (mkdir(dir, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) != 0) {
		/* LCOV_EXCL_START */
		msg_error("Error creating directory '%s'. %s.\n", dir, strerror(errno));
		return -1;
		/* LCOV_EXCL_STOP */
	}

	return 0;
}

int fmtime(int f, int64_t mtime_sec, int mtime_nsec)
{
#if HAVE_FUTIMENS
	struct timespec tv[2];
#else
	struct timeval tv[2];
#endif
	int ret;

#if HAVE_FUTIMENS /* futimens() is preferred because it gives nanosecond precision */
	tv[0].tv_sec = mtime_sec;
	if (mtime_nsec != STAT_NSEC_INVALID)
		tv[0].tv_nsec = mtime_nsec;
	else
		tv[0].tv_nsec = 0;
	tv[1].tv_sec = tv[0].tv_sec;
	tv[1].tv_nsec = tv[0].tv_nsec;

	ret = futimens(f, tv);
#elif HAVE_FUTIMES /* fallback to futimes() if nanosecond precision is not available */
	tv[0].tv_sec = mtime_sec;
	if (mtime_nsec != STAT_NSEC_INVALID)
		tv[0].tv_usec = mtime_nsec / 1000;
	else
		tv[0].tv_usec = 0;
	tv[1].tv_sec = tv[0].tv_sec;
	tv[1].tv_usec = tv[0].tv_usec;

	ret = futimes(f, tv);
#elif HAVE_FUTIMESAT /* fallback to futimesat() for Solaris, it only has futimesat() */
	tv[0].tv_sec = mtime_sec;
	if (mtime_nsec != STAT_NSEC_INVALID)
		tv[0].tv_usec = mtime_nsec / 1000;
	else
		tv[0].tv_usec = 0;
	tv[1].tv_sec = tv[0].tv_sec;
	tv[1].tv_usec = tv[0].tv_usec;

	ret = futimesat(f, 0, tv);
#else
#error No function available to set file timestamps with sub-second precision
#endif

	return ret;
}

/****************************************************************************/
/* memory */

static size_t mcounter;

size_t malloc_counter(void)
{
	return mcounter;
}

static ssize_t malloc_print(int f, const char* str)
{
	ssize_t len = 0;
	while (str[len])
		++len;
	return write(f, str, len);
}

static ssize_t malloc_printn(int f, size_t value)
{
	char buf[32];
	int i;

	if (!value)
		return write(f, "0", 1);

	i = sizeof(buf);
	while (value) {
		buf[--i] = (value % 10) + '0';
		value /= 10;
	}

	return write(f, buf + i, sizeof(buf) - i);
}

void malloc_fail(size_t size)
{
	/* don't use printf to avoid any possible extra allocation */
	/* LCOV_EXCL_START */
	int f = 2; /* stderr */

	malloc_print(f, "Failed for Low Memory!\n");
	malloc_print(f, "Allocating ");
	malloc_printn(f, size);
	malloc_print(f, " bytes.\n");
	malloc_print(f, "Already allocated ");
	malloc_printn(f, malloc_counter());
	malloc_print(f, " bytes.\n");
	if (sizeof(void*) == 4) {
		malloc_print(f, "You are currently using a 32 bits executable.\n");
		malloc_print(f, "If you have more than 4GB of memory, please upgrade to a 64 bits one.\n");
	}
	/* LCOV_EXCL_STOP */
}

void* malloc_nofail(size_t size)
{
	void* ptr = malloc(size);

	if (!ptr) {
		/* LCOV_EXCL_START */
		malloc_fail(size);
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
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

void* calloc_nofail(size_t count, size_t size)
{
	void* ptr;

	size *= count;

	/* see the note in malloc_nofail() of why we don't use calloc() */
	ptr = malloc(size);

	if (!ptr) {
		/* LCOV_EXCL_START */
		malloc_fail(size);
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	memset(ptr, 0, size);

	mcounter += size;

	return ptr;
}

char* strdup_nofail(const char* str)
{
	size_t size;
	char* ptr;

	size = strlen(str) + 1;

	ptr = malloc(size);

	if (!ptr) {
		/* LCOV_EXCL_START */
		malloc_fail(size);
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	memcpy(ptr, str, size);

	mcounter += size;

	return ptr;
}

/****************************************************************************/
/* smartctl */

/**
 * Matches a string with the specified pattern.
 * Like sscanf() a space match any sequence of spaces.
 * Returns 0 if it matches.
 */
static int smatch(const char* str, const char* pattern)
{
	while (*pattern) {
		if (isspace(*pattern)) {
			++pattern;
			while (isspace(*str))
				++str;
		} else if (*pattern == *str) {
			++pattern;
			++str;
		} else
			return -1;
	}

	return 0;
}

int smartctl_attribute(FILE* f, uint64_t* smart, char* serial)
{
	unsigned i;
	int inside;

	/* preclear attribute */
	*serial = 0;
	for (i = 0; i < SMART_COUNT; ++i)
		smart[i] = SMART_UNASSIGNED;

	/* read the file */
	inside = 0;
	while (1) {
		char buf[256];
		char attr[64];
		unsigned id;
		uint64_t raw;
		char* s;

		s = fgets(buf, sizeof(buf), f);
		if (s == 0)
			break;

		/* skip initial spaces */
		while (isspace(*s))
			++s;

		if (*s == 0) {
			inside = 0;
		} else if (smatch(s, "ID#") == 0) {
			inside = 1;
		} else if (smatch(s, "No Errors Logged") == 0) {
			smart[SMART_ERROR] = 0;
		} else if (sscanf(s, "ATA Error Count: %" SCNu64, &raw) == 1) {
			smart[SMART_ERROR] = raw;
		} else if (sscanf(s, "Serial Number: %63s", serial) == 1) {
		} else if (smatch(s, "Rotation Rate: Solid State") == 0) {
			smart[SMART_ROTATION_RATE] = 0;
		} else if (sscanf(s, "Rotation Rate: %" SCNu64, &smart[SMART_ROTATION_RATE]) == 1) {
		} else if (sscanf(s, "User Capacity: %63s", attr) == 1) {
			smart[SMART_SIZE] = 0;
			for (i = 0; attr[i]; ++i) {
				if (isdigit(attr[i])) {
					smart[SMART_SIZE] *= 10;
					smart[SMART_SIZE] += attr[i] - '0';
				}
			}
		} else if (inside) {
			if (sscanf(s, "%u %*s %*s %*s %*s %*s %*s %*s %*s %" SCNu64, &id, &raw) != 2) {
				/* LCOV_EXCL_START */
				msg_error("Invalid smartctl line '%s'.\n", s);
				return -1;
				/* LCOV_EXCL_STOP */
			}

			if (id >= 256) {
				/* LCOV_EXCL_START */
				msg_error("Invalid SMART id '%u'.\n", id);
				return -1;
				/* LCOV_EXCL_STOP */
			}

			smart[id] = raw;
		}
	}

	return 0;
}

