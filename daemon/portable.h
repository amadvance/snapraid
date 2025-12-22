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

#ifdef __MINGW32__
/**
 * Enable the GNU printf functions instead of using the MSVCRT ones.
 *
 * Note that this is the default if _POSIX is also defined.
 * To disable it you have to set it to 0.
 */
#define __USE_MINGW_ANSI_STDIO 1

/**
 * Define the MSVCRT version targeting Windows Vista.
 */
#define __MSVCRT_VERSION__ 0x0600

/**
 * Include Windows Vista headers.
 *
 * Like for InitializeCriticalSection().
 */
#define _WIN32_WINNT 0x600

/**
 * Enable the rand_s() function.l
 */
#define _CRT_RAND_S

#include <windows.h>
#endif

/**
 * Specify the format attribute for printf.
 */
#ifdef __MINGW32__
#if defined(__USE_MINGW_ANSI_STDIO) && __USE_MINGW_ANSI_STDIO == 1
#define attribute_printf gnu_printf /* GNU format */
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
#include <stdint.h>
#include <inttypes.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <getopt.h>
#include <pthread.h>

/**
 * Global variable to identify if Ctrl+C is pressed.
 */
extern volatile int global_interrupt;

#endif
