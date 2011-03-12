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

#if HAVE_GETOPT_LONG
#define SWITCH_GETOPT_LONG(a,b) a
#else
#define SWITCH_GETOPT_LONG(a,b) b
#endif

#ifndef O_BINARY
#define O_BINARY 0
#endif

#ifdef __MINGW32__ /* Specific for MINGW */

/* Remap functions and types for 64 bit support */
#define stat _stati64
#define off_t off64_t
#define lseek lseek64
#define fstat _fstati64
#define HAVE_FTRUNCATE 1
#define ftruncate _chsize_s
#define HAVE_FSYNC 1
#define fsync _commit

#define rename windows_atomic_rename
static inline int windows_atomic_rename(const char* a, const char* b)
{
	if (!MoveFileEx(a, b, MOVEFILE_REPLACE_EXISTING)) {
		switch (GetLastError()) {
		case ERROR_ACCESS_DENIED :
			errno = EACCES;
			break;
		case ERROR_FILE_NOT_FOUND :
			errno = ENOENT;
			break;
		default :
			errno = EIO;
			break;
		}
		return -1;
	}

	return 0;
}
#endif

#endif

