dnl Process this file with autoconf to produce a configure script.
AC_PREREQ([2.65])
AC_INIT([snapraid], [1.3], [], [], [http://snapraid.sourceforge.net])
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

dnl Checks for library functions.
AC_SYS_LARGEFILE
AC_CHECK_FUNCS([memset strchr strerror strrchr mkdir])
AC_CHECK_FUNCS([getopt getopt_long snprintf vsnprintf])
AC_CHECK_FUNCS([ftruncate fsync access posix_fallocate posix_fadvise pread pwrite fgetc_unlocked ferror_unlocked fnmatch])

dnl Checks for architecture
AC_C_BIGENDIAN

dnl Checks for compiler
AC_CHECK_CC_OPT([-Wall], CFLAGS="$CFLAGS -Wall", [])
AC_CHECK_CC_OPT([-Wextra], CFLAGS="$CFLAGS -Wextra", [])

dnl Checks for test environment
AS_CASE([$host],
       [*-*-mingw*], [TESTENV="$WINE"],
       []
)

dnl Checks for options
AC_ARG_WITH([crypto],
	[AS_HELP_STRING([--with-crypto],
	[enable the use of the MD5 fast implementation of the OpenSSL crypto library])],
	[],
	[with_crypto=check])
AS_IF([test "x$with_crypto" != xno], [
	AC_CHECK_LIB([crypto], [MD5], [],
	[if test "x$with_crypto" != xcheck; then
		AC_MSG_FAILURE([--with-crypto was given, but test for OpenSSL crypto library failed])
	fi
	])
])

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
	],
	[])


AC_ARG_VAR([TESTENV], [Test environment])
AC_CONFIG_FILES([Makefile])
AC_OUTPUT

