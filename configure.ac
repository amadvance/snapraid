dnl Process this file with autoconf to produce a configure script.
AC_PREREQ([2.65])
AC_INIT([snapraid], [6.0], [], [], [http://snapraid.sourceforge.net])
AM_INIT_AUTOMAKE([foreign no-dependencies subdir-objects])
AC_CONFIG_SRCDIR([snapraid.c])
AC_CONFIG_HEADER([config.h])
AC_CANONICAL_HOST

dnl Checks for programs.
AC_PROG_CC
AC_USE_SYSTEM_EXTENSIONS
AC_CHECK_PROG([VALGRIND],[valgrind],[valgrind],[])
AC_CHECK_PROG([WINE],[wine],[wine],[])
AC_CHECK_PROG([ADVD2],[advd2],[advd2],[])
AM_CONDITIONAL(HAVE_ADVD2, [test x"$ADVD2" != x])

dnl Checks for libraries.
AC_SYS_LARGEFILE

dnl Checks for header files.
AC_HEADER_ASSERT
AC_HEADER_STDC
AC_HEADER_DIRENT
AC_HEADER_TIME
AC_CHECK_HEADERS([fcntl.h stddef.h stdint.h stdlib.h string.h limits.h])
AC_CHECK_HEADERS([unistd.h getopt.h fnmatch.h io.h inttypes.h byteswap.h])
AC_CHECK_HEADERS([sys/time.h sys/file.h sys/ioctl.h sys/vfs.h sys/statfs.h sys/param.h sys/mount.h])
AC_CHECK_HEADERS([linux/fiemap.h linux/fs.h])

dnl Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_C_INLINE
AC_C_RESTRICT
AC_C_VOLATILE
AC_TYPE_SIZE_T
AC_TYPE_OFF_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T
AC_TYPE_INT8_T
AC_STRUCT_DIRENT_D_INO
AC_STRUCT_DIRENT_D_TYPE
AC_CHECK_MEMBERS([struct stat.st_mtim.tv_nsec, struct stat.st_mtimensec, struct stat.st_mtimespec.tv_nsec], [], [], [[
#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#if HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
]])
AC_CHECK_MEMBERS([struct statfs.f_type], [], [], [[
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
]])

dnl Checks for library functions.
AC_SYS_LARGEFILE
AC_CHECK_FUNCS([memset strchr strerror strrchr mkdir gettimeofday strtoul])
AC_CHECK_FUNCS([getopt getopt_long snprintf vsnprintf])
AC_CHECK_FUNCS([ftruncate fallocate fsync access posix_fallocate posix_fadvise])
AC_CHECK_FUNCS([pread pwrite getc_unlocked ferror_unlocked fnmatch])
AC_CHECK_FUNCS([futimes futimens futimesat localtime_r])
AC_CHECK_FUNCS([fstatat flock statfs])

dnl Checks for architecture
AC_C_BIGENDIAN

dnl Checks for compiler
AC_CHECK_CC_OPT([-Wall], CFLAGS="$CFLAGS -Wall", [])
AC_CHECK_CC_OPT([-Wextra], CFLAGS="$CFLAGS -Wextra", [])

dnl Checks for AS supporting the CRC instruction. In OpenBSD 5.4 it's not.
AC_MSG_CHECKING([for crc32b])
asmcrc=no
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#if defined(__i386__) || defined(__x86_64__) 
	unsigned f(unsigned crc, unsigned char b)
	{
		asm volatile ("crc32b %1, %0\n" : "+r" (crc) : "rm" (b));
		return crc;
	}
#else
#error not x86
#endif
]])],
[AC_DEFINE([HAVE_CRC32B], [1], [Define if statvfs exists.]) asmcrc=yes])
AC_MSG_RESULT([$asmcrc])

dnl Checks for test environment
AS_CASE([$host],
	[*-*-mingw*], [TESTENV="$WINE"],
	[SYMLINK=1]
)
AM_CONDITIONAL(HAVE_SYMLINK, [test x"$SYMLINK" != x])

AC_ARG_ENABLE([profiler],
	[AS_HELP_STRING([--enable-profiler],[enable the use of gprof for code coverage])],
	[
	TESTENV=""
	CFLAGS="-O2 -pg -g"
	],
	[])

AC_ARG_ENABLE([coverage],
	[AS_HELP_STRING([--enable-coverage],[enable the use of gcov for code coverage])],
	[
	TESTENV=""
	CFLAGS="-O0 -g -fprofile-arcs -ftest-coverage"
	],
	[])

AC_ARG_ENABLE([valgrind],
	[AS_HELP_STRING([--enable-valgrind],[enable the use of valgrind in testing])],
	[
	TESTENV="$VALGRIND --leak-check=full --track-fds=yes --error-exitcode=1"
	CFLAGS="$CFLAGS -DCHECKER"
	],
	[])

AC_ARG_ENABLE([sgcheck],
	[AS_HELP_STRING([--enable-sgcheck],[enable the use of sgcheck in testing])],
	[
	TESTENV="$VALGRIND --tool=exp-sgcheck --suppressions=sgcheck.supp --error-exitcode=1"
	CFLAGS="$CFLAGS -DCHECKER"
	],
	[])

AC_ARG_ENABLE([debug],
	[AS_HELP_STRING([--enable-debug],[enable debugging])],
	[
	TESTENV=""
	CFLAGS="-O0 -g -Wall -Wextra"
	],
	[])

AC_ARG_ENABLE([warning],
	[AS_HELP_STRING([--enable-warning],[enable extra warning])],
	[
	AC_CHECK_CC_OPT([-Wpointer-arith], CFLAGS="$CFLAGS -Wpointer-arith", [])
	AC_CHECK_CC_OPT([-Wcast-qual], CFLAGS="$CFLAGS -Wcast-qual", [])
	AC_CHECK_CC_OPT([-Wunused], CFLAGS="$CFLAGS -Wunused", [])
	AC_CHECK_CC_OPT([-Wunreachable-code], CFLAGS="$CFLAGS -Wunreachable-code", [])
	AC_CHECK_CC_OPT([-Wpadded], CFLAGS="$CFLAGS -Wpadded", [])
	AC_CHECK_CC_OPT([-Wshadow], CFLAGS="$CFLAGS -Wshadow", [])
	AC_CHECK_CC_OPT([-Weverything], CFLAGS="$CFLAGS -Weverything", [])
	],
	[])

AC_ARG_VAR([TESTENV], [Test environment])
AC_CONFIG_FILES([Makefile])
AC_OUTPUT

