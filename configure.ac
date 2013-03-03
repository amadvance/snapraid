dnl Process this file with autoconf to produce a configure script.
AC_PREREQ([2.65])
AC_INIT([snapraid], [3.0], [], [], [http://snapraid.sourceforge.net])
AM_INIT_AUTOMAKE([foreign no-dependencies -Wall])
AC_CONFIG_SRCDIR([snapraid.c])
AC_CONFIG_HEADER([config.h])
AC_CANONICAL_HOST

dnl Checks for programs.
AC_PROG_CC
AC_USE_SYSTEM_EXTENSIONS
AC_CHECK_PROG([VALGRIND],[valgrind],[valgrind --leak-check=full --track-fds=yes --error-exitcode=1],[])
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
AC_CHECK_HEADERS([sys/time.h unistd.h getopt.h fnmatch.h io.h inttypes.h])

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
AC_CHECK_MEMBERS([struct stat.st_mtim.tv_nsec, struct stat.st_mtimensec, struct stat.st_mtimespec.tv_nsec], [], [], [[
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
]])

dnl Checks for library functions.
AC_SYS_LARGEFILE
AC_CHECK_FUNCS([memset strchr strerror strrchr mkdir gettimeofday strtoul])
AC_CHECK_FUNCS([getopt getopt_long snprintf vsnprintf])
AC_CHECK_FUNCS([ftruncate fallocate fsync access posix_fallocate posix_fadvise pread pwrite getc_unlocked ferror_unlocked fnmatch])
AC_CHECK_FUNCS([futimes futimens futimesat fstatat])

dnl Checks for architecture
AC_C_BIGENDIAN

dnl Checks for compiler
AC_CHECK_CC_OPT([-Wall], CFLAGS="$CFLAGS -Wall", [])
AC_CHECK_CC_OPT([-Wextra], CFLAGS="$CFLAGS -Wextra", [])

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
	TESTENV="$VALGRIND"
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

AC_ARG_VAR([TESTENV], [Test environment])
AC_CONFIG_FILES([Makefile])
AC_OUTPUT

