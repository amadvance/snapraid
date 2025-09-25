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
/* lock */

/**
 * Locks used externally.
 */
#if HAVE_THREAD
static thread_mutex_t msg_lock;
static thread_mutex_t memory_lock;
#endif

void lock_msg(void)
{
#if HAVE_THREAD
	thread_mutex_lock(&msg_lock);
#endif
}

void unlock_msg(void)
{
#if HAVE_THREAD
	thread_mutex_unlock(&msg_lock);
#endif
}

void lock_memory(void)
{
#if HAVE_THREAD
	thread_mutex_lock(&memory_lock);
#endif
}

void unlock_memory(void)
{
#if HAVE_THREAD
	thread_mutex_unlock(&memory_lock);
#endif
}

void lock_init(void)
{
#if HAVE_THREAD
	/* initialize the locks as first operation as log_fatal depends on them */
	thread_mutex_init(&msg_lock);
	thread_mutex_init(&memory_lock);
#endif
}

void lock_done(void)
{
#if HAVE_THREAD
	thread_mutex_destroy(&msg_lock);
	thread_mutex_destroy(&memory_lock);
#endif
}

/****************************************************************************/
/* print */

int msg_level = 0;
FILE* stdlog = 0;

/*
 * Note that in the following functions we always flush both
 * stdout and stderr, because we want to ensure that they mixes
 * well when redirected to files
 *
 * The buffering is similar at the "line buffered" one, that
 * is not available on Windows, so we emulate it in this way.
 *
 * For stdlog flushing is limited. To ensure flushing the
 * caller should use log_flush().
 */

void log_fatal(const char* format, ...)
{
	va_list ap;

	lock_msg();

	if (stdlog) {
		va_start(ap, format);
		fprintf(stdlog, "msg:fatal: ");
		vfprintf(stdlog, format, ap);
		fflush(stdlog);
		va_end(ap);
	}

	va_start(ap, format);
	vfprintf(stderr, format, ap);
	fflush(stderr);
	va_end(ap);

	unlock_msg();
}

void log_error(const char* format, ...)
{
	va_list ap;

	lock_msg();

	if (stdlog) {
		va_start(ap, format);
		fprintf(stdlog, "msg:error: ");
		vfprintf(stdlog, format, ap);
		fflush(stdlog);
		va_end(ap);
	} else {
		va_start(ap, format);
		vfprintf(stderr, format, ap);
		fflush(stderr);
		va_end(ap);
	}

	unlock_msg();
}

void log_expected(const char* format, ...)
{
	va_list ap;

	lock_msg();

	if (stdlog) {
		va_start(ap, format);
		fprintf(stdlog, "msg:expected: ");
		vfprintf(stdlog, format, ap);
		fflush(stdlog);
		va_end(ap);
	}

	unlock_msg();
}

void log_tag(const char* format, ...)
{
	va_list ap;

	lock_msg();

	if (stdlog) {
		va_start(ap, format);
		vfprintf(stdlog, format, ap);
		/* here we intentionally don't flush */
		/* to make the output faster */
		va_end(ap);
	}

	unlock_msg();
}

void log_flush(void)
{
	lock_msg();

	if (stdlog)
		fflush(stdlog);
	fflush(stdout);
	fflush(stderr);

	unlock_msg();
}

void msg_status(const char* format, ...)
{
	va_list ap;

	lock_msg();

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

	unlock_msg();
}

void msg_info(const char* format, ...)
{
	va_list ap;

	lock_msg();

	/* don't output in stdlog as these messages */
	/* are always paired with a msg_tag() call */

	if (msg_level >= MSG_INFO) {
		va_start(ap, format);
		vfprintf(stdout, format, ap);
		fflush(stdout);
		va_end(ap);
	}

	unlock_msg();
}

void msg_progress(const char* format, ...)
{
	va_list ap;

	lock_msg();

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

	unlock_msg();
}

void msg_bar(const char* format, ...)
{
	va_list ap;

	lock_msg();

	/* don't output in stdlog as these messages */
	/* are intended for screen only */
	/* also don't flush stdout as they are intended to be partial messages */

	if (msg_level >= MSG_BAR) {
		va_start(ap, format);
		vfprintf(stdout, format, ap);
		va_end(ap);
	}

	unlock_msg();
}

void msg_verbose(const char* format, ...)
{
	va_list ap;

	lock_msg();

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

	unlock_msg();
}

void msg_flush(void)
{
	lock_msg();

	fflush(stdout);
	fflush(stderr);

	unlock_msg();
}

void printc(char c, size_t pad)
{
	while (pad) {
		/* group writes in long pieces */
		char buf[128];
		size_t len = pad;

		if (len >= sizeof(buf))
			len = sizeof(buf) - 1;

		memset(buf, c, len);
		buf[len] = 0;

		fputs(buf, stdout);

		pad -= len;
	}
}

void printr(const char* str, size_t pad)
{
	size_t len;

	len = strlen(str);

	if (len < pad)
		printc(' ', pad - len);

	fputs(str, stdout);
}

void printl(const char* str, size_t pad)
{
	size_t len;

	fputs(str, stdout);

	len = strlen(str);

	if (len < pad)
		printc(' ', pad - len);
}

void printp(double v, size_t pad)
{
	char buf[64];
	const char* s = "%";

	if (v > 0.1)
		snprintf(buf, sizeof(buf), "%5.2f%s", v, s);
	else if (v > 0.01)
		snprintf(buf, sizeof(buf), "%6.3f%s", v, s);
	else if (v > 0.001)
		snprintf(buf, sizeof(buf), "%7.4f%s", v, s);
	else if (v > 0.0001)
		snprintf(buf, sizeof(buf), "%8.5f%s", v, s);
	else if (v > 0.00001)
		snprintf(buf, sizeof(buf), "%9.6f%s", v, s);
	else if (v > 0.000001)
		snprintf(buf, sizeof(buf), "%10.7f%s", v, s);
	else if (v > 0.0000001)
		snprintf(buf, sizeof(buf), "%11.8f%s", v, s);
	else if (v > 0.00000001)
		snprintf(buf, sizeof(buf), "%12.9f%s", v, s);
	else if (v > 0.000000001)
		snprintf(buf, sizeof(buf), "%13.10f%s", v, s);
	else if (v > 0.0000000001)
		snprintf(buf, sizeof(buf), "%14.11f%s", v, s);
	else if (v > 0.00000000001)
		snprintf(buf, sizeof(buf), "%15.12f%s", v, s);
	else if (v > 0.000000000001)
		snprintf(buf, sizeof(buf), "%16.13f%s", v, s);
	else
		snprintf(buf, sizeof(buf), "%17.14f%s", v, s);
	printl(buf, pad);
}

#define ESCAPE(from,escape,to) \
	case from : \
		if (p == end) \
			goto bail; \
		*p++ = escape; \
		if (p == end) \
			goto bail; \
		*p++ = to; \
		break

const char* esc_tag(const char* str, char* buffer)
{
	char* begin = buffer;
	char* end = begin + ESC_MAX;
	char* p = begin;

	/* copy string with escaping */
	while (*str) {
		char c = *str;

		switch (c) {

		ESCAPE('\n', '\\', 'n');
		ESCAPE('\r', '\\', 'r');
		ESCAPE(':', '\\', 'd');
		ESCAPE('\\', '\\', '\\');

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
	log_fatal("Escape for log is too long\n");
	exit(EXIT_FAILURE);
	/* LCOV_EXCL_STOP */
}

const char* esc_shell_multi(const char** str_map, unsigned str_max, char* buffer)
{
	char* begin = buffer;
	char* end = begin + ESC_MAX;
	char* p = begin;
	unsigned str_mac;
	const char* str;

#ifdef _WIN32
	int has_quote = 0;

	for (str_mac = 0; str_mac < str_max; ++str_mac) {
		str = str_map[str_mac];
		if (strchr(str, ' ') != 0)
			has_quote = 1;
	}

	if (has_quote) {
		if (p == end)
			goto bail;
		*p++ = '"';
	}
#endif

	/* copy string with escaping */
	str_mac = 0;
	str = str_map[str_mac];
	while (1) {
		/* get the next char */
		char c = *str;

		/* if one string is finished, go to the next */
		while (c == 0 && str_mac + 1 < str_max) {
			++str_mac;
			str = str_map[str_mac];
			c = *str;
		}

		/* if we read all the strings, stop */
		if (!c)
			break;

		switch (c) {
#ifdef _WIN32
		/*
		 * Windows shell escape
		 *
		 * The Windows NT Command Shell
		 * https://technet.microsoft.com/en-us/library/cc723564.aspx
		 */
		case '"' :
			/* double quote, it needs to be quoted with \ */
			if (has_quote) {
				/* " -> "\"" -> (close quote)(quoted with \ ")(reopen quote) */
				if (p == end)
					goto bail;
				*p++ = '"';
				if (p == end)
					goto bail;
				*p++ = '\\';
				if (p == end)
					goto bail;
				*p++ = '"';
				if (p == end)
					goto bail;
				*p++ = '"';
			} else {
				/* " -> \" */
				if (p == end)
					goto bail;
				*p++ = '\\';
				if (p == end)
					goto bail;
				*p++ = '"';
			}
			break;
		case '&' :
		case '|' :
		case '(' :
		case ')' :
		case '<' :
		case '>' :
		case '^' :
			/* reserved chars, they need to be quoted with ^ */
			if (has_quote) {
				if (p == end)
					goto bail;
				*p++ = c;
			} else {
				if (p == end)
					goto bail;
				*p++ = '^';
				if (p == end)
					goto bail;
				*p++ = c;
			}
			break;
#else
		/* special chars that need to be quoted */
		case ' ' : /* space */
		case '~' : /* home */
		case '`' : /* command */
		case '#' : /* comment */
		case '$' : /* variable */
		case '&' : /* background job */
		case '*' : /* wildcard */
		case '(' : /* shell */
		case ')' : /* shell */
		case '\\': /* quote */
		case '|' : /* pipe */
		case '[' : /* wildcard */
		case ']' : /* wildcard */
		case '{' : /* code */
		case '}' : /* code */
		case ';' : /* separator */
		case '\'': /* quote */
		case '"' : /* quote */
		case '<' : /* redirect */
		case '>' : /* redirect */
		case '?' : /* wildcard */
			if (p == end)
				goto bail;
			*p++ = '\\';
			if (p == end)
				goto bail;
			*p++ = c;
			break;
#endif
		default :
			/* unquoted */
			if (p == end)
				goto bail;
			*p++ = c;
			break;
		}

		++str;
	}

#ifdef _WIN32
	if (has_quote) {
		if (p == end)
			goto bail;
		*p++ = '"';
	}
#endif

	/* put final 0 */
	if (p == end)
		goto bail;
	*p = 0;

	return begin;

bail:
	/* LCOV_EXCL_START */
	log_fatal("Escape for shell is too long\n");
	exit(EXIT_FAILURE);
	/* LCOV_EXCL_STOP */
}

char* strpolish(char* s)
{
	char* i = s;

	while (*i) {
		if (isspace(*i) || !isprint(*i))
			*i = ' ';
		++i;
	}

	return s;
}

unsigned strsplit(char** split_map, unsigned split_max, char* str, const char* delimiters)
{
	unsigned mac = 0;

	/* skip initial delimiters */
	str += strspn(str, delimiters);

	while (*str != 0 || mac == split_max) {
		/* start of the token */
		split_map[mac] = str;
		++mac;

		/* find the first delimiter or the end of the string */
		str += strcspn(str, delimiters);

		/* put the final terminator if missing */
		if (*str != 0)
			*str++ = 0;

		/* skip trailing delimiters */
		str += strspn(str, delimiters);
	}

	return mac;
}

/****************************************************************************/
/* path */

void pathcpy(char* dst, size_t size, const char* src)
{
	size_t len = strlen(src);

	if (len + 1 > size) {
		/* LCOV_EXCL_START */
		log_fatal("Path too long '%s'\n", src);
		os_abort();
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
		log_fatal("Path too long '%s%s'\n", dst, src);
		os_abort();
		/* LCOV_EXCL_STOP */
	}

	memcpy(dst + dst_len, src, src_len + 1);
}

void pathcatl(char* dst, size_t dst_len, size_t size, const char* src)
{
	size_t src_len = strlen(src);

	if (dst_len + src_len + 1 > size) {
		/* LCOV_EXCL_START */
		log_fatal("Path too long '%s%s'\n", dst, src);
		os_abort();
		/* LCOV_EXCL_STOP */
	}

	memcpy(dst + dst_len, src, src_len + 1);
}

void pathcatc(char* dst, size_t size, char c)
{
	size_t dst_len = strlen(dst);

	if (dst_len + 2 > size) {
		/* LCOV_EXCL_START */
		log_fatal("Path too long '%s%c'\n", dst, c);
		os_abort();
		/* LCOV_EXCL_STOP */
	}

	dst[dst_len] = c;
	dst[dst_len + 1] = 0;
}

void pathimport(char* dst, size_t size, const char* src)
{
	pathcpy(dst, size, src);

#ifdef _WIN32
	/* convert the  Windows dir separator '\' to C '/', */
	/* and the Windows escaping  char '^' to the fnmatch '\' */
	while (*dst) {
		switch (*dst) {
		case '\\' :
			*dst = '/';
			break;
		case '^' :
			*dst = '\\';
			break;
		}
		++dst;
	}
#endif
}

void pathexport(char* dst, size_t size, const char* src)
{
	pathcpy(dst, size, src);

#ifdef _WIN32
	/* invert the import */
	while (*dst) {
		switch (*dst) {
		case '/' :
			*dst = '\\';
			break;
		case '\\' :
			*dst = '^';
			break;
		}
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
		if (size > 0) {
			dst[size - 1] = 0;
			log_fatal("Path too long '%s...'\n", dst);
		} else {
			log_fatal("Path too long for empty size'\n");
		}
		os_abort();
		/* LCOV_EXCL_STOP */
	}
}

void pathslash(char* dst, size_t size)
{
	size_t len = strlen(dst);

	if (len > 0 && dst[len - 1] != '/') {
		if (len + 2 >= size) {
			/* LCOV_EXCL_START */
			log_fatal("Path too long '%s/'\n", dst);
			os_abort();
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
/* file-system */

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
	/* if it's a drive specification like "C:" */
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
	 * doesn't need to open the dir with CreateFile().
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
		log_fatal("Error creating directory '%s'. %s.\n", dir, strerror(errno));
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

int lmtime(const char* path, int64_t mtime_sec, int mtime_nsec)
{
#if HAVE_UTIMENSAT
	struct timespec tv[2];
#else
	struct timeval tv[2];
#endif
	int ret;

#if HAVE_UTIMENSAT /* utimensat() is preferred because it gives nanosecond precision */
	tv[0].tv_sec = mtime_sec;
	if (mtime_nsec != STAT_NSEC_INVALID)
		tv[0].tv_nsec = mtime_nsec;
	else
		tv[0].tv_nsec = 0;
	tv[1].tv_sec = tv[0].tv_sec;
	tv[1].tv_nsec = tv[0].tv_nsec;

	ret = utimensat(AT_FDCWD, path, tv, AT_SYMLINK_NOFOLLOW);
#elif HAVE_LUTIMES /* fallback to lutimes() if nanosecond precision is not available */
	tv[0].tv_sec = mtime_sec;
	if (mtime_nsec != STAT_NSEC_INVALID)
		tv[0].tv_usec = mtime_nsec / 1000;
	else
		tv[0].tv_usec = 0;
	tv[1].tv_sec = tv[0].tv_sec;
	tv[1].tv_usec = tv[0].tv_usec;

	ret = lutimes(path, tv);
#elif HAVE_FUTIMESAT /* fallback to futimesat() for Solaris, it only has futimesat() */
	tv[0].tv_sec = mtime_sec;
	if (mtime_nsec != STAT_NSEC_INVALID)
		tv[0].tv_usec = mtime_nsec / 1000;
	else
		tv[0].tv_usec = 0;
	tv[1].tv_sec = tv[0].tv_sec;
	tv[1].tv_usec = tv[0].tv_usec;

	ret = futimesat(AT_FDCWD, path, tv);
#else
#error No function available to set file timestamps with sub-second precision
#endif

	return ret;
}

/****************************************************************************/
/* advise */

void advise_init(struct advise_struct* advise, int mode)
{
	advise->mode = mode;
	advise->dirty_begin = 0;
	advise->dirty_end = 0;
}

int advise_flags(struct advise_struct* advise)
{
	int flags = 0;

	if (advise->mode == ADVISE_SEQUENTIAL
		|| advise->mode == ADVISE_FLUSH
		|| advise->mode == ADVISE_FLUSH_WINDOW
		|| advise->mode == ADVISE_DISCARD
		|| advise->mode == ADVISE_DISCARD_WINDOW
	)
		flags |= O_SEQUENTIAL;

#if HAVE_DIRECT_IO
	if (advise->mode == ADVISE_DIRECT)
		flags |= O_DIRECT;
#endif

	return flags;
}

int advise_open(struct advise_struct* advise, int f)
{
	(void)advise;
	(void)f;

#if HAVE_POSIX_FADVISE
	if (advise->mode == ADVISE_SEQUENTIAL
		|| advise->mode == ADVISE_FLUSH
		|| advise->mode == ADVISE_FLUSH_WINDOW
		|| advise->mode == ADVISE_DISCARD
		|| advise->mode == ADVISE_DISCARD_WINDOW
	) {
		int ret;

		/* advise noreuse access, this avoids to pollute the page cache */
		/* supported from Linux Kernel 6.3 with this commit: https://github.com/torvalds/linux/commit/17e810229cb3068b692fa078bd9b3a6527e0866a */
		ret = posix_fadvise(f, 0, 0, POSIX_FADV_NOREUSE);
		if (ret == ENOSYS) {
			/* call is not supported */
			ret = 0;
		}
		if (ret != 0) {
			/* LCOV_EXCL_START */
			errno = ret; /* posix_fadvise return the error code */
			return -1;
			/* LCOV_EXCL_STOP */
		}

		/* advise sequential access, this doubles the read-ahead window size */
		ret = posix_fadvise(f, 0, 0, POSIX_FADV_SEQUENTIAL);
		if (ret == ENOSYS) {
			/* call is not supported, like in armhf, see posix_fadvise manpage */
			ret = 0;
		}
		if (ret != 0) {
			/* LCOV_EXCL_START */
			errno = ret; /* posix_fadvise return the error code */
			return -1;
			/* LCOV_EXCL_STOP */
		}
	}
#endif

	return 0;
}

int advise_write(struct advise_struct* advise, int f, data_off_t offset, data_off_t size)
{
	data_off_t flush_offset;
	data_off_t flush_size;
	data_off_t discard_offset;
	data_off_t discard_size;

	(void)f;
	(void)flush_offset;
	(void)flush_size;
	(void)discard_offset;
	(void)discard_size;

	flush_offset = 0;
	flush_size = 0;
	discard_offset = 0;
	discard_size = 0;

	/*
	 * Follow Linus recommendations about fast writes.
	 *
	 * Linus "Unexpected splice "always copy" behavior observed"
	 * http://thread.gmane.org/gmane.linux.kernel/987247/focus=988070
	 * ---
	 * I have had _very_ good experiences with even a rather trivial
	 * file writer that basically used (iirc) 8MB windows, and the logic was very
	 * trivial:
	 *
	 *  - before writing a new 8M window, do "start writeback"
	 *    (SYNC_FILE_RANGE_WRITE) on the previous window, and do
	 *    a wait (SYNC_FILE_RANGE_WAIT_AFTER) on the window before that.
	 *
	 * in fact, in its simplest form, you can do it like this (this is from my
	 * "overwrite disk images" program that I use on old disks):
	 *
	 * for (index = 0; index < max_index ;index++) {
	 *   if (write(fd, buffer, BUFSIZE) != BUFSIZE)
	 *     break;
	 *   // This won't block, but will start writeout asynchronously
	 *   sync_file_range(fd, index*BUFSIZE, BUFSIZE, SYNC_FILE_RANGE_WRITE);
	 *   // This does a blocking write-and-wait on any old ranges
	 *   if (index)
	 *     sync_file_range(fd, (index-1)*BUFSIZE, BUFSIZE, SYNC_FILE_RANGE_WAIT_BEFORE|SYNC_FILE_RANGE_WRITE|SYNC_FILE_RANGE_WAIT_AFTER);
	 * }
	 *
	 * and even if you don't actually do a discard (maybe we should add a
	 * SYNC_FILE_RANGE_DISCARD bit, right now you'd need to do a separate
	 * fadvise(FADV_DONTNEED) to throw it out) the system behavior is pretty
	 * nice, because the heavy writer gets good IO performance _and_ leaves only
	 * easy-to-free pages around after itself.
	 * ---
	 *
	 * Linus "Unexpected splice "always copy" behavior observed"
	 * http://thread.gmane.org/gmane.linux.kernel/987247/focus=988176
	 * ---
	 * The behavior for dirty page writeback is _not_ well defined, and
	 * if you do POSIX_FADV_DONTNEED, I would suggest you do it as part of that
	 * writeback logic, ie you do it only on ranges that you have just waited on.
	 *
	 * IOW, in my example, you'd couple the
	 *
	 *   sync_file_range(fd, (index-1)*BUFSIZE, BUFSIZE, SYNC_FILE_RANGE_WAIT_BEFORE|SYNC_FILE_RANGE_WRITE|SYNC_FILE_RANGE_WAIT_AFTER);
	 *
	 * with a
	 *
	 *   posix_fadvise(fd, (index-1)*BUFSIZE, BUFSIZE, POSIX_FADV_DONTNEED);
	 *
	 * afterwards to throw out the pages that you just waited for.
	 * ---
	 */

	switch (advise->mode) {
	case ADVISE_FLUSH :
		flush_offset = offset;
		flush_size = size;
		break;
	case ADVISE_DISCARD :
		discard_offset = offset;
		discard_size = size;
		break;
	case ADVISE_FLUSH_WINDOW :
		/* if the dirty range can be extended */
		if (advise->dirty_end == offset) {
			/* extent the dirty range */
			advise->dirty_end += size;

			/* if we reached the window size */
			if (advise->dirty_end - advise->dirty_begin >= ADVISE_WINDOW_SIZE) {
				/* flush the window  */
				flush_offset = advise->dirty_begin;
				flush_size = ADVISE_WINDOW_SIZE;

				/* remove it from the dirty range */
				advise->dirty_begin += ADVISE_WINDOW_SIZE;
			}
		} else {
			/* otherwise flush the existing dirty */
			flush_offset = advise->dirty_begin;
			flush_size = advise->dirty_end - advise->dirty_begin;

			/* and set the new range as dirty */
			advise->dirty_begin = offset;
			advise->dirty_end = offset + size;
		}
		break;
	case ADVISE_DISCARD_WINDOW :
		/* if the dirty range can be extended */
		if (advise->dirty_end == offset) {
			/* extent the dirty range */
			advise->dirty_end += size;

			/* if we reached the double window size */
			if (advise->dirty_end - advise->dirty_begin >= 2 * ADVISE_WINDOW_SIZE) {
				/* discard the first window */
				discard_offset = advise->dirty_begin;
				discard_size = ADVISE_WINDOW_SIZE;

				/* remove it from the dirty range */
				advise->dirty_begin += ADVISE_WINDOW_SIZE;

				/* flush the second window */
				flush_offset = advise->dirty_begin;
				flush_size = ADVISE_WINDOW_SIZE;
			}
		} else {
			/* otherwise discard the existing dirty */
			discard_offset = advise->dirty_begin;
			discard_size = advise->dirty_end - advise->dirty_begin;

			/* and set the new range as dirty */
			advise->dirty_begin = offset;
			advise->dirty_end = offset + size;
		}
		break;
	}

#if HAVE_SYNC_FILE_RANGE
	if (flush_size != 0) {
		int ret;

		/* start writing immediately */
		ret = sync_file_range(f, flush_offset, flush_size, SYNC_FILE_RANGE_WRITE);
		if (ret != 0) {
			/* LCOV_EXCL_START */
			return -1;
			/* LCOV_EXCL_STOP */
		}
	}
#endif

#if HAVE_SYNC_FILE_RANGE && HAVE_POSIX_FADVISE
	if (discard_size != 0) {
		int ret;

		/* send the data to the disk and wait until it's written */
		ret = sync_file_range(f, discard_offset, discard_size, SYNC_FILE_RANGE_WAIT_BEFORE | SYNC_FILE_RANGE_WRITE | SYNC_FILE_RANGE_WAIT_AFTER);
		if (ret != 0) {
			/* LCOV_EXCL_START */
			return -1;
			/* LCOV_EXCL_STOP */
		}

		/* flush the data from the cache */
		ret = posix_fadvise(f, discard_offset, discard_size, POSIX_FADV_DONTNEED);
		/* for POSIX_FADV_DONTNEED we don't allow failure with ENOSYS */
		if (ret != 0) {
			/* LCOV_EXCL_START */
			errno = ret; /* posix_fadvise return the error code */
			return -1;
			/* LCOV_EXCL_STOP */
		}
	}
#endif

	return 0;
}

int advise_read(struct advise_struct* advise, int f, data_off_t offset, data_off_t size)
{
	(void)advise;
	(void)f;
	(void)offset;
	(void)size;

#if HAVE_POSIX_FADVISE
	if (advise->mode == ADVISE_DISCARD
		|| advise->mode == ADVISE_DISCARD_WINDOW
	) {
		int ret;

		/* flush the data from the cache */
		ret = posix_fadvise(f, offset, size, POSIX_FADV_DONTNEED);
		/* for POSIX_FADV_DONTNEED we don't allow failure with ENOSYS */
		if (ret != 0) {
			/* LCOV_EXCL_START */
			errno = ret; /* posix_fadvise return the error code */
			return -1;
			/* LCOV_EXCL_STOP */
		}
	}
#endif

	/*
	 * Here we cannot call posix_fadvise(..., POSIX_FADV_WILLNEED) for the next block
	 * because it may be blocking.
	 *
	 * Ted Ts'o "posix_fadvise(POSIX_FADV_WILLNEED) waits before returning?"
	 * https://lkml.org/lkml/2010/12/6/122
	 * ---
	 * readahead and posix_fadvise(POSIX_FADV_WILLNEED) work exactly the same
	 * way, and in fact share mostly the same code path (see
	 * force_page_cache_readahead() in mm/readahead.c).
	 *
	 * They are asynchronous in that there is no guarantee the pages will be
	 * in the page cache by the time they return.  But at the same time, they
	 * are not guaranteed to be non-blocking.  That is, the work of doing the
	 * readahead does not take place in a kernel thread.  So if you try to
	 * request I/O than will fit in the request queue, the system call will
	 * block until some I/O is completed so that more I/O requested cam be
	 * loaded onto the request queue.
	 *
	 * The only way to fix this would be to either put the work on a kernel
	 * thread (i.e., some kind of workqueue) or in a userspace thread.  For
	 * ion programmer wondering what to do today, I'd suggest the
	 * latter since it will be more portable across various kernel versions.
	 *
	 * This does leave the question about whether we should change the kernel
	 * to allow readahead() and posix_fadvise(POSIX_FADV_WILLNEED) to be
	 * non-blocking and do this work in a workqueue (or via some kind of
	 * callback/continuation scheme).  My worry is just doing this if a user
	 * application does something crazy, like request gigabytes and gigabytes
	 * of readahead, and then repented of their craziness, there should be a
	 * way of cancelling the readahead request.  Today, the user can just
	 * kill the application.  But if we simply shove the work to a kernel
	 * thread, it becomes a lot harder to cancel the readahead request.  We'd
	 * have to invent a new API, and then have a way to know whether the user
	 * has access to kill a particular readahead request, etc.
	 * ---
	 */

	return 0;
}

/****************************************************************************/
/* memory */

/**
 * Total amount of memory allocated.
 */
static size_t mcounter;

size_t malloc_counter_get(void)
{
	size_t ret;

	lock_memory();

	ret = mcounter;

	unlock_memory();

	return ret;
}

void malloc_counter_inc(size_t inc)
{
	lock_memory();

	mcounter += inc;

	unlock_memory();
}

/* LCOV_EXCL_START */
static ssize_t malloc_print(int f, const char* str)
{
	ssize_t len = 0;

	while (str[len])
		++len;
	return write(f, str, len);
}
/* LCOV_EXCL_STOP */

/* LCOV_EXCL_START */
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
/* LCOV_EXCL_STOP */

/* LCOV_EXCL_START */
void malloc_fail(size_t size)
{
	/* don't use printf to avoid any possible extra allocation */
	int f = 2; /* stderr */

	malloc_print(f, "Failed for Low Memory!\n");
	malloc_print(f, "Allocating ");
	malloc_printn(f, size);
	malloc_print(f, " bytes.\n");
	malloc_print(f, "Already allocated ");
	malloc_printn(f, malloc_counter_get());
	malloc_print(f, " bytes.\n");
	if (sizeof(void*) == 4) {
		malloc_print(f, "You are currently using a 32 bits executable.\n");
		malloc_print(f, "If you have more than 4GB of memory, please upgrade to a 64 bits one.\n");
	}
}
/* LCOV_EXCL_STOP */

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

	malloc_counter_inc(size);

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

	malloc_counter_inc(size);

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

	malloc_counter_inc(size);

	return ptr;
}

/****************************************************************************/
/* smartctl */

/**
 * Match a string with the specified pattern.
 * Like sscanf() a space match any sequence of spaces.
 * Return 0 if it matches.
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

int smartctl_attribute(FILE* f, const char* file, const char* name, uint64_t* smart, char* serial, char* vendor, char* model)
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
		unsigned id;
		uint64_t raw;
		char* s;

		s = fgets(buf, sizeof(buf), f);
		if (s == 0)
			break;

		/* remove extraneous chars */
		s = strpolish(buf);

		log_tag("smartctl:%s:%s:out: %s\n", file, name, s);

		/* skip initial spaces */
		while (isspace(*s))
			++s;

		if (*s == 0) {
			inside = 0;
		/* common */
		} else if (smatch(s, "Rotation Rate: Solid State") == 0) {
			smart[SMART_ROTATION_RATE] = 0;
		} else if (sscanf(s, "Rotation Rate: %" SCNu64, &smart[SMART_ROTATION_RATE]) == 1) {
		} else if (smatch(s, "User Capacity:") == 0) {
			char* begin = strchr(s, ':');
			char* end = strstr(s, "bytes");
			if (begin != 0 && end != 0 && begin < end) {
				char* p;
				smart[SMART_SIZE] = 0;
				for (p = begin; p != end; ++p) {
					if (isdigit(*p)) {
						smart[SMART_SIZE] *= 10;
						smart[SMART_SIZE] += *p - '0';
					}
				}
			}
		} else if (sscanf(s, "Device Model: %63s %63s", vendor, model) == 2) {
		} else if (sscanf(s, "Device Model: %63s", model) == 1) {
		/* SCSI */
		} else if (sscanf(s, "Serial number: %63s", serial) == 1) { /* note "n" of "number" lower case */
		} else if (sscanf(s, "Elements in grown defect list: %" SCNu64, &smart[SMART_REALLOCATED_SECTOR_COUNT]) == 1) {
		} else if (sscanf(s, "Current Drive Temperature: %" SCNu64, &smart[SMART_TEMPERATURE_CELSIUS]) == 1) {
		} else if (sscanf(s, "Drive Trip Temperature: %" SCNu64, &smart[SMART_AIRFLOW_TEMPERATURE_CELSIUS]) == 1) {
		} else if (sscanf(s, "Accumulated start-stop cycles: %" SCNu64, &smart[SMART_START_STOP_COUNT]) == 1) {
		} else if (sscanf(s, "Accumulated load-unload cycles: %" SCNu64, &smart[SMART_LOAD_CYCLE_COUNT]) == 1) {
		} else if (sscanf(s, "  number of hours powered up = %" SCNu64, &smart[SMART_POWER_ON_HOURS]) == 1) {
		/* ATA */
		} else if (sscanf(s, "Serial Number: %63s", serial) == 1) {
		} else if (smatch(s, "ID#") == 0) {
			inside = 1;
		} else if (smatch(s, "No Errors Logged") == 0) {
			smart[SMART_ERROR] = 0;
		} else if (sscanf(s, "ATA Error Count: %" SCNu64, &raw) == 1) {
			smart[SMART_ERROR] = raw;
		} else if (inside) {
			if (sscanf(s, "%u %*s %*s %*s %*s %*s %*s %*s %*s %" SCNu64, &id, &raw) != 2) {
				/* LCOV_EXCL_START */
				log_fatal("Invalid smartctl line '%s'.\n", s);
				return -1;
				/* LCOV_EXCL_STOP */
			}

			if (id >= 256) {
				/* LCOV_EXCL_START */
				log_fatal("Invalid SMART id '%u'.\n", id);
				return -1;
				/* LCOV_EXCL_STOP */
			}

			smart[id] = raw;
		}
	}

	return 0;
}

int smartctl_flush(FILE* f, const char* file, const char* name)
{
	/* read the file */
	while (1) {
		char buf[256];
		char* s;

		s = fgets(buf, sizeof(buf), f);
		if (s == 0)
			break;

		/* remove extraneous chars */
		s = strpolish(buf);

		log_tag("smartctl:%s:%s:out: %s\n", file, name, s);
	}

	return 0;
}

/****************************************************************************/
/* thread */

#if HAVE_THREAD
void thread_mutex_init(thread_mutex_t* mutex)
{
	if (pthread_mutex_init(mutex, 0) != 0) {
		/* LCOV_EXCL_START */
		log_fatal("Failed call to pthread_mutex_init().\n");
		os_abort();
		/* LCOV_EXCL_STOP */
	}
}

void thread_mutex_destroy(thread_mutex_t* mutex)
{
	if (pthread_mutex_destroy(mutex) != 0) {
		/* LCOV_EXCL_START */
		log_fatal("Failed call to pthread_mutex_destroy().\n");
		os_abort();
		/* LCOV_EXCL_STOP */
	}
}

void thread_mutex_lock(thread_mutex_t* mutex)
{
	if (pthread_mutex_lock(mutex) != 0) {
		/* LCOV_EXCL_START */
		log_fatal("Failed call to pthread_mutex_lock().\n");
		os_abort();
		/* LCOV_EXCL_STOP */
	}
}

void thread_mutex_unlock(thread_mutex_t* mutex)
{
	if (pthread_mutex_unlock(mutex) != 0) {
		/* LCOV_EXCL_START */
		log_fatal("Failed call to pthread_mutex_unlock().\n");
		os_abort();
		/* LCOV_EXCL_STOP */
	}
}

void thread_cond_init(thread_cond_t* cond)
{
	if (pthread_cond_init(cond, 0) != 0) {
		/* LCOV_EXCL_START */
		log_fatal("Failed call to pthread_cond_init().\n");
		os_abort();
		/* LCOV_EXCL_STOP */
	}
}

void thread_cond_destroy(thread_cond_t* cond)
{
	if (pthread_cond_destroy(cond) != 0) {
		/* LCOV_EXCL_START */
		log_fatal("Failed call to pthread_cond_destroy().\n");
		os_abort();
		/* LCOV_EXCL_STOP */
	}
}

void thread_cond_signal(thread_cond_t* cond)
{
	if (pthread_cond_signal(cond) != 0) {
		/* LCOV_EXCL_START */
		log_fatal("Failed call to pthread_cond_signal().\n");
		os_abort();
		/* LCOV_EXCL_STOP */
	}
}

void thread_cond_broadcast(thread_cond_t* cond)
{
	if (pthread_cond_broadcast(cond) != 0) {
		/* LCOV_EXCL_START */
		log_fatal("Failed call to pthread_cond_broadcast().\n");
		os_abort();
		/* LCOV_EXCL_STOP */
	}
}

void thread_cond_wait(thread_cond_t* cond, thread_mutex_t* mutex)
{
	if (pthread_cond_wait(cond, mutex) != 0) {
		/* LCOV_EXCL_START */
		log_fatal("Failed call to pthread_cond_wait().\n");
		os_abort();
		/* LCOV_EXCL_STOP */
	}
}

/**
 * Implementation note about conditional variables.
 *
 * The conditional variables can be signaled inside or outside the mutex,
 * what is better it's debatable but in general doing that outside the mutex,
 * reduces the number of context switches.
 *
 * But when testing with helgrind and drd, this disallows such tools to
 * to see the dependency between the signal and the wait.
 *
 * To avoid it we signal everything inside the mutex. And we do this in both
 * test mode (with CHECKER defined) and release mode (CHECKER not defined),
 * to be on the safe side and avoid any difference in behaviour between test and
 * release.
 *
 * Here some interesting discussion:
 *
 * Condvars: signal with mutex locked or not?
 * http://www.domaigne.com/blog/computing/condvars-signal-with-mutex-locked-or-not/
 *
 * Calling pthread_cond_signal without locking mutex
 * http://stackoverflow.com/questions/4544234/calling-pthread-cond-signal-without-locking-mutex/4544494#4544494
 */

/**
 * Control when to signal the condition variables.
 */
int thread_cond_signal_outside = 0;

void thread_cond_signal_and_unlock(thread_cond_t* cond, thread_mutex_t* mutex)
{
	if (thread_cond_signal_outside) {
		/* without the thread checker unlock before signaling, */
		/* this reduces the number of context switches */
		thread_mutex_unlock(mutex);
	}

	thread_cond_signal(cond);

	if (!thread_cond_signal_outside) {
		/* with the thread checker unlock after signaling */
		/* to make explicit the condition and mutex relation */
		thread_mutex_unlock(mutex);
	}
}

void thread_cond_broadcast_and_unlock(thread_cond_t* cond, thread_mutex_t* mutex)
{
	if (thread_cond_signal_outside) {
		/* without the thread checker unlock before signaling, */
		/* this reduces the number of context switches */
		thread_mutex_unlock(mutex);
	}

	thread_cond_broadcast(cond);

	if (!thread_cond_signal_outside) {
		/* with the thread checker unlock after signaling */
		/* to make explicit the condition and mutex relation */
		thread_mutex_unlock(mutex);
	}
}

void thread_create(thread_id_t* thread, void* (* func)(void *), void *arg)
{
	if (pthread_create(thread, 0, func, arg) != 0) {
		/* LCOV_EXCL_START */
		log_fatal("Failed call to pthread_create().\n");
		os_abort();
		/* LCOV_EXCL_STOP */
	}
}

void thread_join(thread_id_t thread, void** retval)
{
	if (pthread_join(thread, retval) != 0) {
		/* LCOV_EXCL_START */
		log_fatal("Failed call to pthread_join().\n");
		os_abort();
		/* LCOV_EXCL_STOP */
	}
}

void thread_yield(void)
{
#ifdef __MINGW32__
	Sleep(0);
#else
	sched_yield();
#endif
}
#endif
