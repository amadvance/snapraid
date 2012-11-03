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

#ifndef __PORTABLE_H
#define __PORTABLE_H

#if HAVE_CONFIG_H
#include "config.h" /* Use " to include first in the same directory of this file */
#endif

/***************************************************************************/
/* Config */

#ifdef __MINGW32__ /* Specific for MINGW */
#define __MSVCRT_VERSION__ 0x0601 /* Define the MSVCRT version required */
#include <windows.h>
#endif

/* Include some standard headers */
#include <stdio.h>
#include <stdlib.h> /* On many systems (e.g., Darwin), `stdio.h' is a prerequisite. */
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <limits.h>

#if HAVE_STDINT_H
#include <stdint.h>
#endif

#if HAVE_INTTYPES_H
#include <inttypes.h>
#endif

#if HAVE_UNISTD_H
#include <unistd.h>
#endif

#if TIME_WITH_SYS_TIME
#include <sys/time.h>
#include <time.h>
#else
#if HAVE_SYS_TIME_H
#include <sys/time.h>
#else
#include <time.h>
#endif
#endif

#if HAVE_DIRENT_H
#include <dirent.h>
#define NAMLEN(dirent) strlen((dirent)->d_name)
#else
#define dirent direct
#define NAMLEN(dirent) (dirent)->d_namlen
#if HAVE_SYS_NDIR_H
#include <sys/ndir.h>
#endif
#if HAVE_SYS_DIR_H
#include <sys/dir.h>
#endif
#if HAVE_NDIR_H
#include <ndir.h>
#endif
#endif

#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#if HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

#if HAVE_GETOPT_H
#include <getopt.h>
#endif

#if HAVE_FNMATCH_H
#include <fnmatch.h>
#else
#include "fnmatch.h"
#endif

#if HAVE_IO_H
#include <io.h>
#endif

#if HAVE_GETOPT_LONG
#define SWITCH_GETOPT_LONG(a,b) a
#else
#define SWITCH_GETOPT_LONG(a,b) b
#endif

#ifdef __MINGW32__ /* Specific for MingW */
#include "mingw.h"

/* We have nano second support */
#define STAT_NSEC(st) (st)->st_mtimensec

#else /* Specific for Unix */

#define O_BINARY 0 /**< Not used in Unix. */
#define O_SEQUENTIAL 0 /**< In Unix posix_fadvise() shall be used. */

/* Check if we have nanoseconds support */
#if HAVE_STRUCT_STAT_ST_MTIM_TV_NSEC
#define STAT_NSEC(st) (st)->st_mtim.tv_nsec /* Linux */
#elif HAVE_STRUCT_STAT_ST_MTIMENSEC
#define STAT_NSEC(st) (st)->st_mtimensec /* NetBSD */
#elif HAVE_STRUCT_STAT_ST_MTIMESPEC_TV_NSEC
#define STAT_NSEC(st) (st)->st_mtimespec.tv_nsec /* FreeBSD, Mac OS X */
#else
/**
 * If nanoseconds are not supported, we report the special FILE_MTIME_NSEC_INVALID value,
 * to mark that it's undefined.
 * This result in not having it saved into the content file as a dummy 0.
 */
#define STAT_NSEC(st) -1
#endif

#ifdef O_NOATIME
/**
 * Open a file with the O_NOATIME flag to avoid to update the acces time.
 */
static int open_noatime(const char* file, int flags)
{
	int f = open(file, flags | O_NOATIME);
	if (f == -1 && errno == EPERM)
		f = open(file, flags);
	return f;
}
#else
#define open_noatime open 
#endif

/**
 * Check if the specified file is hidden.
 */
static inline int stat_hidden(struct dirent* dd, struct stat* st)
{
	(void)st;
	return dd->d_name[0] == '.';
}

/**
 * Return a description of the file type.
 */
static inline const char* stat_desc(struct stat* st)
{
	if (S_ISREG(st->st_mode))
		return "regular";
	if (S_ISDIR(st->st_mode))
		return "directory";
	if (S_ISCHR(st->st_mode))
		return "character";
	if (S_ISBLK(st->st_mode))
		return "block-device";
	if (S_ISFIFO(st->st_mode))
		return "fifo";
	if (S_ISLNK(st->st_mode))
		return "link";
	if (S_ISLNK(st->st_mode))
		return "symbolic-link";
	if (S_ISSOCK(st->st_mode))
		return "socket";
	return "unknown";
}
#endif

#endif

