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

#include <wchar.h>

/**
 * Redefines PATH_MAX to allow very long paths.
 */
#undef PATH_MAX
#define PATH_MAX 1024

/* Remap functions and types */
#define fopen windows_fopen
#define open windows_open
#define open_noatime windows_open
#define stat windows_stat
#define lstat windows_lstat
#define access windows_access
#define off_t off64_t
#define lseek lseek64
#define fstat windows_fstat
#define HAVE_FTRUNCATE 1
#define ftruncate windows_ftruncate
#define HAVE_FSYNC 1
#define fsync _commit
#define rename windows_rename
#define remove windows_remove
#define mkdir(a, b) windows_mkdir(a)
#define rmdir windows_rmdir
#define dirent windows_dirent
#define DIR windows_dir
#define opendir windows_opendir
#define readdir windows_readdir
#define closedir windows_closedir
#define HAVE_FUTIMENS 1
#define futimens windows_futimens
#define O_NOFOLLOW 0
#define dirent_hidden windows_dirent_hidden
#define HAVE_DIRENT_LSTAT 1
#define dirent_lstat windows_dirent_lstat
#define stat_desc windows_stat_desc
#define sleep windows_sleep
#define S_IFLNK 0x7000 /* 4==DIR, 8==REG */
#define S_ISLNK(m) (((m) & _S_IFMT) == S_IFLNK)
#define readlink windows_readlink
#define symlink windows_symlink
#define link windows_link

/**
 * Generic stat information.
 */
struct windows_stat {
	uint64_t st_ino;
	int64_t st_size;
	int64_t st_mtime;
	int32_t st_mtimensec;
	uint32_t st_mode;
	uint32_t st_nlink;
	uint32_t st_dev;
	int st_hidden;
	const char* st_desc;
};

/**
 * Like the C fstat() including the inode/device information.
 */
int windows_fstat(int fd, struct windows_stat* st);

/**
 * Like the C lstat() but without the inode information.
 * In Windows the inode information is not reported.
 * In Windows and in case of hardlinks, the size and the attributes of the file can
 * be completely bogus, because changes made by other hardlinks are reported in the
 * directory entry only when the file is opened.
 *
 * MSDN CreateHardLinks
 * http://msdn.microsoft.com/en-us/library/windows/desktop/aa363860%28v=vs.85%29.aspx
 * 'When you create a hard link on the NTFS file system, the file attribute information'
 * 'in the directory entry is refreshed only when the file is opened, or when'
 * 'GetFileInformationByHandle is called with the handle of a specific file.'
 *
 * MSDN HardLinks and Junctions
 * http://msdn.microsoft.com/en-us/library/windows/desktop/aa365006%28v=vs.85%29.aspx
 * 'However, the directory entry size and attribute information is updated only'
 * 'for the link through which the change was made.'
 *
 * Use lstat_ex() to override these limitations.
 */
int windows_lstat(const char* file, struct windows_stat* st);

/**
 * Like the C stat() including the inode/device information.
 */
int windows_stat(const char* file, struct windows_stat* st);

/**
 * Like the C access().
 */
int windows_access(const char* file, int mode);

/**
 * Like the C mkdir().
 */
int windows_mkdir(const char* file);

/**
 * Like rmdir().
 */
int windows_rmdir(const char* file);

/**
 * Like the C lstat() including the inode/device information.
 * It doesn't work for all kind of files and directories. For example "\System Volume Information" cannot be opened.
 * Note that instead lstat() works for all the files.
 */
#define HAVE_LSTAT_EX 1
int lstat_ex(const char* file, struct windows_stat* st);

/**
 * Like the C ftruncate().
 */
int windows_ftruncate(int fd, off64_t off);

/**
 * Like the C futimes().
 */
int windows_futimes(int fd, struct timeval tv[2]);

struct windows_timespec {
	int64_t tv_sec;
	int tv_nsec;
};

#define timespec windows_timespec

/**
 * Like the C futimens().
 */
int windows_futimens(int fd, struct windows_timespec tv[2]);

/**
 * Like the C rename().
 */
int windows_rename(const char* a, const char* b);

/**
 * Like the C remove().
 */
int windows_remove(const char* a);

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
	int d_hidden;
	WIN32_FIND_DATAW d_data;
};

/**
 * Like the C DIR.
 */
struct windows_dir_struct {
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

/**
 * Convert a dirent record to a lstat record.
 * Just like the one obtained calling lstat().
 */
void windows_dirent_lstat(const struct windows_dirent* dd, struct windows_stat* st);

/**
 * Like dirent_hidden().
 */
int windows_dirent_hidden(struct dirent* dd);

/**
 * Like stat_desc().
 */
const char* windows_stat_desc(struct stat* st);

/**
 * Like sleep().
 */
void windows_sleep(unsigned seconds);

/**
 * Like readlink().
 */
int windows_readlink(const char* file, char* buffer, size_t size);

/**
 * Like symlink().
 * Return ENOSYS if symlinks are not supported.
 */
int windows_symlink(const char* existing, const char* file);

/**
 * Like link().
 */
int windows_link(const char* existing, const char* file);

#endif
#endif

