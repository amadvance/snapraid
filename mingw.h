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

#ifndef __PORTABLE_MINGW_H
#define __PORTABLE_MINGW_H

#ifdef __MINGW32__ /* Only for MingW */

#include "wchar.h"

/**
 * Redefines PATH_MAX to allow long UTF8 names.
 */
#undef PATH_MAX
#define PATH_MAX (1024+64)

/* Remap functions and types */
#define fopen windows_fopen
#define open windows_open
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
#define dirent windows_dirent
#define DIR windows_dir
#define opendir windows_opendir
#define readdir windows_readdir
#define closedir windows_closedir

/**
 * Generic stat information.
 */
struct windows_stat {
	uint64_t st_ino;
	int64_t st_size;
	int64_t st_mtime;
	uint32_t st_mode;
	uint32_t st_nlink;
};

/**
 * Like the C fstat() but without inode information.
 */
int windows_fstat(int fd, struct windows_stat* st);

/**
 * Like the C stat() but without inode information.
 */
int windows_stat(const char* file, struct windows_stat* st);

/**
 * Like the C stat() with inode information.
 * It doesn't work for all kind of files and directories. For example "\System Volume Information" cannot be opened.
 */
#define HAVE_STAT_INODE 1
int stat_inode(const char* file, struct windows_stat* st);

/**
 * Like the C ftruncate().
 */
int windows_ftruncate(int fd, off64_t off);

/**
 * Like the C rename().
 */
int windows_rename(const char* a, const char* b);

/**
 * Like the C fopen().
 */
FILE* windows_fopen(const char* file, const char* mode);

/**
 * Like the C open().
 */
int windows_open(const char* file, int flags, ...);

/**
 * Like the C dirent.
 */
struct windows_dirent {
	char d_name[PATH_MAX];
};

/**
 * Like the C DIR.
 */
struct windows_dir_struct {
	WIN32_FIND_DATAW data;
	HANDLE h;
	struct windows_dirent buffer;
	int flags;
};
typedef struct windows_dir_struct windows_dir;

/**
 * Like the C opendir().
 */
windows_dir* windows_opendir(const char* dir);

/**
 * Like the C readdir().
 */
struct windows_dirent* windows_readdir(windows_dir* dirstream);

/**
 * Like the C closedir().
 */
int windows_closedir(windows_dir* dirstream);

#endif
#endif

