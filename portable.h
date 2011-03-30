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

#ifndef O_BINARY
#define O_BINARY 0
#endif

#ifdef __MINGW32__ /* Specific for MINGW */

/* Remap functions and types for 64 bit support */
#define stat windows_stat
#define off_t off64_t
#define lseek lseek64
#define fstat windows_fstat
#define HAVE_FTRUNCATE 1
#define ftruncate windows_ftruncate
#define HAVE_FSYNC 1
#define fsync _commit
#define rename windows_rename
#define mkdir(a, b) mkdir(a)

/**
 * Generic stat information.
 */
struct windows_stat {
	uint64_t st_ino;
	int64_t st_size;
	int64_t st_mtime;
	uint32_t st_mode;
};

/**
 * Convert Windows info to the Unix stat format.
 */
static inline void windows_info2stat(const BY_HANDLE_FILE_INFORMATION* info, struct windows_stat* st)
{
	/* Convert special attributes to a char device */
	if ((info->dwFileAttributes & (FILE_ATTRIBUTE_DEVICE | FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_OFFLINE | FILE_ATTRIBUTE_VIRTUAL | FILE_ATTRIBUTE_REPARSE_POINT)) != 0) {
		st->st_mode = S_IFCHR;
	} else if ((info->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
		st->st_mode = S_IFDIR;
	} else {
		st->st_mode = S_IFREG;
	}

	st->st_size = info->nFileSizeHigh;
	st->st_size <<= 32;
	st->st_size |= info->nFileSizeLow;

	st->st_mtime = info->ftLastWriteTime.dwHighDateTime;
	st->st_mtime <<= 32;
	st->st_mtime |= info->ftLastWriteTime.dwLowDateTime;

	/*
	 * Convert to unix time
	 *
	 * How To Convert a UNIX time_t to a Win32 FILETIME or SYSTEMTIME
	 * http://support.microsoft.com/kb/167296
	 */
	st->st_mtime = (st->st_mtime - 116444736000000000LL) / 10000000;

	st->st_ino = info->nFileIndexHigh;
	st->st_ino <<= 32;
	st->st_ino |= info->nFileIndexLow;
}

static inline int windows_fstat(int fd, struct windows_stat* st)
{
	BY_HANDLE_FILE_INFORMATION info;
	HANDLE h;

	h = (HANDLE)_get_osfhandle(fd);
	if (h == INVALID_HANDLE_VALUE) {
		errno = EBADF;
		return -1;
	}

	if (!GetFileInformationByHandle(h, &info))  {
		errno = EIO;
		return -1;
	}

	windows_info2stat(&info, st);

	return 0;
}

static inline int windows_stat(const char* file, struct windows_stat* st)
{
	BY_HANDLE_FILE_INFORMATION info;
	HANDLE h;

	h = CreateFile(file, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, 0);
	if (h == INVALID_HANDLE_VALUE) {
		DWORD error = GetLastError();
		switch (error) {
		case ERROR_FILE_NOT_FOUND :
			errno = ENOENT;
			break;
		default:
			errno = EIO;
			break;
		}
		return -1;
	}

	if (!GetFileInformationByHandle(h, &info))  {
		CloseHandle(h);
		errno = EIO;
		return -1;
	}

	windows_info2stat(&info, st);

	CloseHandle(h);

	return 0;
}

static inline int windows_ftruncate(int fd, off64_t off)
{
	HANDLE h;
	LARGE_INTEGER pos;

	if (fd == -1) {
		errno = EBADF;
		return -1;
	}

	h = (HANDLE)_get_osfhandle(fd);
	if (h == INVALID_HANDLE_VALUE) {
		errno = EBADF;
		return -1;
	}

	pos.QuadPart = off;
	if (!SetFilePointerEx(h, pos, 0, FILE_BEGIN)) {
		DWORD error = GetLastError();
		switch (error) {
		case ERROR_INVALID_HANDLE :
			errno = EBADF;
			break;
		default:
			errno = EIO;
			break;
		}
		return -1;
	}

	if (!SetEndOfFile(h)) {
		DWORD error = GetLastError();
		switch (error) {
		case ERROR_INVALID_HANDLE :
			errno = EBADF;
			break;
		case ERROR_ACCESS_DENIED :
			errno = EACCES;
			break;
		default:
			errno = EIO;
			break;
		}
		return -1;
	}

	return 0;
}

static inline int windows_rename(const char* a, const char* b)
{
	/*
	 * Implements an atomic rename in Windows.
	 * Not really atomic at now to support XP.
	 *
	 * Is an atomic file rename (with overwrite) possible on Windows?
	 * http://stackoverflow.com/questions/167414/is-an-atomic-file-rename-with-overwrite-possible-on-windows
	 */
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

