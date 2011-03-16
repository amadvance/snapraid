dnl Process this file with autoconf to produce a configure script.
AC_PREREQ([2.65])
AC_INIT([snapraid], [0.2], [], [], [http://snapraid.sourceforge.net])
AM_INIT_AUTOMAKE([foreign no-dependencies])
AC_CONFIG_SRCDIR([snapraid.c])
AC_CONFIG_HEADER([config.h])
AC_CANONICAL_HOST

dnl Checks for programs.
AC_PROG_CC
AC_USE_SYSTEM_EXTENSIONS

dnl Checks for libraries.
AC_SYS_LARGEFILE

dnl Checks for header files.
AC_HEADER_ASSERT
AC_HEADER_STDC
AC_HEADER_DIRENT
AC_HEADER_TIME
AC_CHECK_HEADERS([fcntl.h stddef.h stdint.h stdlib.h string.h sys/time.h unistd.h getopt.h fnmatch.h])

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
AC_CHECK_FUNCS([memset strchr strerror strrchr])
AC_CHECK_FUNCS([getopt getopt_long snprintf vsnprintf])
AC_CHECK_FUNCS([ftruncate fsync posix_fallocate posix_fadvise pread pwrite fgetc_unlocked ferror_unlocked])

dnl Checks for architecture
AC_C_BIGENDIAN

dnl Checks for compiler
AC_CHECK_CC_OPT([-Wall], CFLAGS="$CFLAGS -Wall", [])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT

