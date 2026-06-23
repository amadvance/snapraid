// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Andrea Mazzoleni

#ifndef __PORTABLE_H
#define __PORTABLE_H

#if HAVE_CONFIG_H
#include "config.h" /* Use " to include first in the same directory of this file */
#endif

/***************************************************************************/
/* Config */

#ifdef __MINGW32__
/**
 * Enable the GNU printf functions instead of using the MSVCRT ones.
 *
 * Note that this is the default if _POSIX is also defined.
 * To disable it you have to set it to 0.
 */
#define __USE_MINGW_ANSI_STDIO 1

/**
 * Define the MSVCRT version targeting Windows 7.
 */
#define __MSVCRT_VERSION__ 0x0601

/**
 * Include Windows 7 headers.
 *
 * Like for InitializeCriticalSection().
 */
#define _WIN32_WINNT 0x601

/**
 * Enable the rand_s() function.
 */
#define _CRT_RAND_S

/**
 * Undef as it clashes with windows.h declarations
 */
#undef DATADIR

#include <windows.h>
#endif

/**
 * Specify the format attribute for printf.
 */
#ifdef __MINGW32__
#if defined(__USE_MINGW_ANSI_STDIO) && __USE_MINGW_ANSI_STDIO == 1
#define attribute_printf gnu_printf /* GNU format */
#define printf(...) __mingw_printf(__VA_ARGS__) /* to support %z in printf */
#else
#define attribute_printf ms_printf /* MSVCRT format */
#endif
#else
#define attribute_printf printf /* GNU format is the default one */
#endif

/**
 * Compiler extension
 */
#ifndef __always_inline
#define __always_inline inline __attribute__((always_inline))
#endif

#ifndef __noreturn
#define __noreturn __attribute__((noreturn))
#endif

/**
 * Architecture for inline assembly.
 */
#if HAVE_ASSEMBLY
#if defined(__i386__)
#define CONFIG_X86 1
#define CONFIG_X86_32 1
#endif

#if defined(__x86_64__)
#define CONFIG_X86 1
#define CONFIG_X86_64 1
#endif

#if defined(__aarch64__) && defined(__ARM_FEATURE_CRC32)
#define CONFIG_ARM_CRC 1
#endif
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

#if HAVE_SYS_SYSCTL_H
#include <sys/sysctl.h>
#endif

#if HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif

#if HAVE_LINUX_FS_H
#include <linux/fs.h>
#endif

#if HAVE_LINUX_BTRFS_H
#include <linux/btrfs.h>
#endif

#if HAVE_LINUX_FIEMAP_H
#include <linux/fiemap.h>
#endif

#if HAVE_BLKID_BLKID_H
#include <blkid/blkid.h>
#if HAVE_BLKID_DEVNO_TO_DEVNAME && HAVE_BLKID_GET_TAG_VALUE
#define HAVE_BLKID 1
#endif
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

#if HAVE_MACH_MACH_TIME_H
#include <mach/mach_time.h>
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

#if HAVE_SYS_MKDEV
#include <sys/mkdev.h>
#endif

#if HAVE_SYS_SYSMACROS_H
#include <sys/sysmacros.h>
#endif

#if HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

#if HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif

#if HAVE_GETOPT_H
#include <getopt.h>
#endif

#if HAVE_MATH_H
#include <math.h>
#endif

#if HAVE_EXECINFO_H
#include <execinfo.h>
#endif

#ifdef HAVE_STRINGS
#include <strings.h>
#endif

#if HAVE_SYSLOG_H
#include <syslog.h>
#endif

#if HAVE_GRP_H
#include <grp.h>
#endif

#if HAVE_PWD_H
#include <pwd.h>
#endif

#if HAVE_SYS_SYSINFO_H
#include <sys/sysinfo.h>
#endif

#if HAVE_SYS_UTSNAME_H
#include <sys/utsname.h>
#endif

#if HAVE_ZLIB
#include <zlib.h>
#endif

#if HAVE_ZSTD
#include <zstd.h>
#endif

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <DiskArbitration/DiskArbitration.h>
#include <mach-o/dyld.h>
#endif

#if HAVE_IO_H
#include <io.h>
#endif

#if HAVE_GETOPT_LONG
#define SWITCH_GETOPT_LONG(a, b) a
#else
#define SWITCH_GETOPT_LONG(a, b) b
#endif

/**
 * Enables lock file support.
 */
#if HAVE_FLOCK && HAVE_FTRUNCATE
#define HAVE_LOCKFILE 1
#endif

#ifdef HAVE_LINUX_CLOSE_RANGE_H
#include <linux/close_range.h>
#endif

/*
 * Flags for open()
 */
#ifndef O_PATH
#ifdef O_SEARCH
#define O_PATH O_SEARCH /* macOS */
#else
#define O_PATH O_RDONLY /* POSIX */
#endif
#endif

/**
 * Includes specific support for Windows or Linux.
 */
#ifdef __MINGW32__
#include "mingw.h"
#else
#include "unix.h"
#endif

#endif

