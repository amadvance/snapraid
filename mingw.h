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

/* Remap functions and types for 64 bit support */
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

#define HAVE_STAT_INODE 1

/**
 * Like the C stat() with inode information.
 * It doesn't work for all kind of files and directories. For example "\System Volume Information" cannot be opened.
 */
int stat_inode(const char* file, struct windows_stat* st);

/**
 * Like the C ftruncate().
 */
int windows_ftruncate(int fd, off64_t off);

/**
 * Like the C rename().
 */
int windows_rename(const char* a, const char* b);

#endif
#endif

