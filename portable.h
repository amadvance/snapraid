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
#define __MSVCRT_VERSION__ 0x0601 /* Define the MSVCRT version targetting Windows XP */
#define _WIN32_WINNT 0x501 /* Include Windows XP CreateHardLinkW */
#include <windows.h>
#endif

/**
 * Includes some platform specific headers.
 */
#if HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif

#if HAVE_SYS_MOUNT_H
#include <sys/mount.h>
#endif

#if HAVE_SYS_VFS_H
#include <sys/vfs.h>
#endif

#if HAVE_SYS_STATFS_H
#include <sys/statfs.h>
#endif

#if HAVE_SYS_FILE_H
#include <sys/file.h>
#endif

#if HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif

#if HAVE_LINUX_FS_H
#include <linux/fs.h>
#endif

#if HAVE_LINUX_FIEMAP_H
#include <linux/fiemap.h>
#endif

/**
 * Includes some standard headers.
 */
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

/**
 * Disable case check in Windows.
 */
#ifdef _WIN32
#define FNM_CASEINSENSITIVE_FOR_WIN FNM_CASEFOLD
#else
#define FNM_CASEINSENSITIVE_FOR_WIN 0
#endif

#if HAVE_IO_H
#include <io.h>
#endif

#if HAVE_GETOPT_LONG
#define SWITCH_GETOPT_LONG(a,b) a
#else
#define SWITCH_GETOPT_LONG(a,b) b
#endif

/**
 * Enables lock file support.
 */
#if HAVE_FLOCK && HAVE_FTRUNCATE
#define HAVE_LOCKFILE 1
#endif

/**
 * Includes specific support for Windows or Linux.
 */
#ifdef __MINGW32__
#include "mingw.h"
#else
#include "unix.h"
#endif

/**
 * Another name for link() to avoid confusion with local variables called "link".
 */
static inline int hardlink(const char* a, const char* b)
{
	return link(a, b);
}

/**
 * Get the device UUID.
 * Returns 0 on success.
 */
int devuuid(uint64_t device, char* uuid, size_t size);

/**
 * Special value returned when the file doesn't have a real offset.
 * For example, because it's stored in the NTFS MFT.
 */
#define FILEPHY_WITHOUT_OFFSET 0

/**
 * Special value returned when the filesystem doesn't report any offset for unknown reason.
 */
#define FILEPHY_UNREPORTED_OFFSET 1

/**
 * Value indicating real offsets. All offsets greater or equal at this one are real.
 */
#define FILEPHY_REAL_OFFSET 2

/**
 * Get the physcal address of the specified file.
 * This is expected to be just a hint and not necessarely correct or unique.
 * Returns 0 on success.
 */
int filephy(const char* path, struct stat* st, uint64_t* physical);

/**
 * Checks if the underline filesystem support persistent inodes.
 * Returns -1 on error, 0 on success.
 */
int fsinfo(const char* path, int* has_persistent_inode);

/**
 * Initializes the system.
 */
void os_init(void);

/**
 * Deintialize the system.
 */
void os_done(void);

/**
 * Global log file.
 */
FILE* stdlog;

#endif

