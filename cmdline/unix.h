// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2011 Andrea Mazzoleni

#ifndef __UNIX_H
#define __UNIX_H

#ifdef __linux__
#define HAVE_LINUX_DEVICE 1 /**< In Linux enables special device support. */
#define HAVE_DIRECT_IO 1 /**< Support O_DIRECT in open(). */
#endif

#define O_BINARY 0 /**< Not used in Unix. */
#define O_SEQUENTIAL 0 /**< In Unix posix_fadvise() shall be used. */

/**
 * If nanoseconds are not supported, we report the special STAT_NSEC_INVALID value,
 * to mark that it's undefined.
 */
#define STAT_NSEC_INVALID -1

/* Check if we have nanoseconds support */
#if HAVE_STRUCT_STAT_ST_MTIM_TV_NSEC
#define STAT_NSEC(st) ((int)(st)->st_mtim.tv_nsec) /* Linux */
#elif HAVE_STRUCT_STAT_ST_MTIMENSEC
#define STAT_NSEC(st) ((int)(st)->st_mtimensec) /* NetBSD */
#elif HAVE_STRUCT_STAT_ST_MTIMESPEC_TV_NSEC
#define STAT_NSEC(st) ((int)(st)->st_mtimespec.tv_nsec) /* FreeBSD, Mac OS X */
#else
#define STAT_NSEC(st) STAT_NSEC_INVALID
#endif

/**
 * Open a file with the O_NOATIME flag to avoid to update the access time.
 */
int open_noatime(const char* file, int flags);

/**
 * Check if the specified file is hidden.
 */
int dirent_hidden(struct dirent* dd);

/**
 * Return a description of the file type.
 */
const char* stat_desc(struct stat* st);

/**
 * Return the alignment requirement for direct IO.
 */
size_t direct_size(void);

/****************************************************************************/
/* thread */

#if HAVE_PTHREAD_H
#include <pthread.h>
#endif

#if HAVE_PTHREAD_CREATE
#define HAVE_THREAD 1
typedef pthread_t thread_id_t;
typedef pthread_mutex_t thread_mutex_t;
typedef pthread_cond_t thread_cond_t;
#endif

#endif

