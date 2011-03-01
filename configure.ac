dnl Process this file with autoconf to produce a configure script.
AC_INIT([snapraid], [0.1])
AM_INIT_AUTOMAKE([foreign no-dependencies])
AC_CONFIG_SRCDIR([snapraid.c])
AC_CONFIG_HEADER([config.h])
AC_CANONICAL_HOST
AC_COPYRIGHT([Copyright 2011 Andrea Mazzoleni])

dnl Checks for programs.
AC_PROG_CC

dnl Checks for libraries.
AC_SYS_LARGEFILE

dnl Checks for header files.
AC_HEADER_STDC
AC_HEADER_DIRENT
AC_HEADER_TIME
AC_FUNC_UTIME_NULL
AC_CHECK_HEADERS([unistd.h getopt.h stdint.h])

dnl Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_C_INLINE
AC_C_RESTRICT
AC_C_VOLATILE
AC_TYPE_SIZE_T

dnl Checks for library functions.
AC_CHECK_FUNCS([getopt getopt_long snprintf vsnprintf])
AC_CHECK_FUNCS([fsync fallocate])

dnl Configure for 64 bit files
CFLAGS="$CFLAGS -D_FILE_OFFSET_BITS=64"

dnl Enable GLIBC symbols
AC_DEFINE(_GNU_SOURCE,1,[Enable GLIBC symbols])

dnl Checks for architecture
AC_C_BIGENDIAN

dnl Checks for compiler
AC_CHECK_CC_OPT([-Wall], CFLAGS="$CFLAGS -Wall", [])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT

