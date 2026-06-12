// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2011 Andrea Mazzoleni

#ifndef __PORTABLE_MINGW_H
#define __PORTABLE_MINGW_H

#ifdef __MINGW32__ /* Only for MingW */

#include <wchar.h>

/* map Windows name to POSIX name */
#define strncasecmp _strnicmp

/****************************************************************************/
/* conversion */

/**
 * Size in chars of conversion buffers for u8to16() and u16to8().
 */
#define CONV_MAX PATH_MAX

/**
 * Convert a path to the Windows format.
 *
 * If only_is_required is 1, the extended-length format is used only if required.
 *
 * The exact operation done is:
 * - If it's a '\\?\' or '\\.\' path, convert any '/' to '\'.
 * - If it's a disk designator path, like 'D:\' or 'D:/', it prepends '\\?\' to the path and convert any '/' to '\'.
 * - If it's a UNC path, like ''\\server'', it prepends '\\?\UNC\' to the path and convert any '/' to '\'.
 * - Otherwise, only the UTF conversion is done. In this case Windows imposes a limit of 260 chars, and automatically convert any '/' to '\'.
 *
 * For more details see:
 * Naming Files, Paths, and Namespaces
 * http://msdn.microsoft.com/en-us/library/windows/desktop/aa365247%28v=vs.85%29.aspx#maxpath
 */
wchar_t* convert_arg(wchar_t* conv_buf, const char* src, int only_if_required);

#define convert(buf, a) convert_arg(buf, a, 0)
#define convert_if_required(buf, a) convert_arg(buf, a, 1)

char* u16tou8(char* conv_buf, const wchar_t* src);

/**
 * Convert a generic string from UTF8 to UTF16.
 */
wchar_t* u8tou16(wchar_t* conv_buf, const char* src);

/****************************************************************************/
/* file */

/**
 * Redefines PATH_MAX to allow very long paths.
 */
#undef PATH_MAX
#define PATH_MAX 1024

/* Remap functions and types */
#undef fopen
#define fopen windows_fopen
#undef open
#define open windows_open
#define open_noatime windows_open
#undef stat
#define stat windows_stat
#undef lstat
#define lstat windows_lstat
#undef off_t
#define off_t off64_t
#undef fstat
#define fstat windows_fstat
#define HAVE_FTRUNCATE 1
#undef ftruncate
#define ftruncate windows_ftruncate
#define HAVE_FALLOCATE 1
#undef fallocate
#define fallocate windows_fallocate
#define HAVE_FSYNC 1
#undef fsync
#define fsync windows_fsync
#define sync windows_sync
#undef rename
#define rename windows_rename
#undef remove
#define remove windows_remove
#undef mkdir
#define mkdir(a, b) windows_mkdir(a)
#undef rmdir
#define rmdir windows_rmdir
#undef dirent
#define dirent windows_dirent
#undef DIR
#define DIR windows_dir
#undef opendir
#define opendir windows_opendir
#undef readdir
#define readdir windows_readdir
#undef closedir
#define closedir windows_closedir
#define HAVE_FUTIMENS 1
#undef futimens
#define futimens windows_futimens
#define HAVE_UTIMENSAT 1
#define AT_FDCWD -1
#define AT_SYMLINK_NOFOLLOW 1
#undef utimensat
#define utimensat windows_utimensat
#define O_NOFOLLOW 0
#define dirent_hidden windows_dirent_hidden
#define HAVE_STRUCT_DIRENT_D_STAT 1
#undef HAVE_STRUCT_DIRENT_D_INO
#define HAVE_STRUCT_STAT_ST_NLINK 1
#define dirent_lstat windows_dirent_lstat
#define stat_desc windows_stat_desc
#undef sleep
#define sleep windows_sleep
#undef usleep
#define usleep windows_usleep
/* 4==DIR, 5,6,7=free, 8==REG */
#define S_IFLNK 0x5000 /* Symbolic link to file */
#define S_ISLNK(m) (((m) & _S_IFMT) == S_IFLNK)
#define S_IFLNKDIR 0x6000 /* Symbolic link to directory */
#define S_ISLNKDIR(m) (((m) & _S_IFMT) == S_IFLNKDIR)
#define S_IFJUN 0x7000 /* Junction */
#define S_ISJUN(m) (((m) & _S_IFMT) == S_IFJUN)
#undef readlink
#define readlink windows_readlink
#undef symlink
#define symlink windows_symlink
#undef link
#define link windows_link
#undef strerror
#define strerror windows_strerror
#undef read
#define read windows_read
#undef write
#define write windows_write
#undef lseek
#define lseek windows_lseek
#undef pread
#define pread windows_pread
#undef pwrite
#define pwrite windows_pwrite
#define direct_size windows_direct_size
#define HAVE_DIRECT_IO 1
#define O_DIRECT 0x10000000
#define O_DSYNC 0x20000000

/**
 * If nanoseconds are not supported, we report the special STAT_NSEC_INVALID value,
 * to mark that it's undefined.
 */
#define STAT_NSEC_INVALID -1

/* We have nano second support */
#define STAT_NSEC(st) ((int)(st)->st_mtimensec)

/**
 * Generic stat information.
 */
struct windows_stat {
	uint64_t st_ino;
	int64_t st_size;
	int64_t st_mtime;
	int32_t st_mtimensec;
	uint32_t st_mode;
	uint32_t st_dev;
	uint32_t st_nlink;
	int st_hidden;
	const char* st_desc;
	int st_sync; /**< If the information are in sync with the file-system. */
};

/**
 * Like the C fstat().
 */
int windows_fstat(int fd, struct windows_stat* st);

/**
 * Like the C lstat(), but with some limitations.
 *
 * The st_ino field may be 0 if it's not possible to read it in a fast way.
 * Specifically this always happens.
 *
 * In case of hardlinks, the size and the attributes of the file can
 * be completely bogus, because changes made by other hardlinks are reported
 * in the directory entry only when the file is opened.
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
 * Use lstat_sync() to override these limitations.
 */
int windows_lstat(const char* file, struct windows_stat* st);

/**
 * Like the C stat().
 */
int windows_stat(const char* file, struct windows_stat* st);

/**
 * Like the C mkdir().
 */
int windows_mkdir(const char* file);

/**
 * Like rmdir().
 */
int windows_rmdir(const char* file);

/**
 * Like the C lstat(), but with some limitations.
 *
 * This call fills all the st_* fields of the stat struct,
 * and if provided the pointer, also the physical offset.
 *
 * It doesn't work for all kinds of files and directories.
 * You must call it only for regular files.
 * For example, "C:\System Volume Information" cannot be accessed
 * with error ERROR_ACCESS_DENIED.
 *
 * Note that instead lstat() works for all the files.
 */
#define HAVE_LSTAT_SYNC 1
int lstat_sync(const char* file, struct windows_stat* st, uint64_t* physical);

/**
 * Like the C ftruncate().
 */
int windows_ftruncate(int fd, off64_t off);

/**
 * Like the C fallocate().
 */
int windows_fallocate(int fd, int mode, off64_t off, off64_t len);

/**
 * Like the C fsync().
 */
int windows_fsync(int fd);

/**
 * Like the C sync().
 */
int windows_sync(void);

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
 * Like the C utimensat().
 */
int windows_utimensat(int fd, const char* file, struct windows_timespec tv[2], int flags);

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
	struct windows_stat d_stat;
};

/**
 * Like the C DIR.
 */
struct windows_dir_struct;
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
 * Convert a dirent record to a lstat record, but with some limitations.
 *
 * The st_mode field may be 0 if the file is a reparse point.
 * Specifically this happens if we are using GetFileInformationByHandleEx()
 * to read the directory stream.
 *
 * The st_ino field may be 0 if it's not possible to read it in a fast way.
 * Specifically this happens if we are using FindFirst/FindNext to enumerate
 * the directory.
 *
 * In such cases, call lstat_sync() to fill the missing fields.
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
unsigned windows_sleep(unsigned seconds);

/**
 * List usleep().
 */
void windows_usleep(uint64_t useconds);

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
 * Like symlink().
 * Return ENOSYS if symlinks are not supported.
 */
int windows_symlink_directory(const char* existing, const char* file);

/**
 * Like link().
 */
int windows_link(const char* existing, const char* file);

/**
 * Like strerror().
 */
const char* windows_strerror(int err);

/**
 * Like read().
 */
ssize_t windows_read(int f, void* buffer, size_t size);

/**
 * Like write().
 */
ssize_t windows_write(int f, const void* buffer, size_t size);

/**
 * Like lseek().
 */
off_t windows_lseek(int f, off_t offset, int whence);

/**
 * Like pread().
 */
ssize_t windows_pread(int f, void* buffer, size_t size, off_t offset);

/**
 * Like pwrite().
 */
ssize_t windows_pwrite(int f, const void* buffer, size_t size, off_t offset);

/**
 * List direct_size().
 */
size_t windows_direct_size(void);

/**
 * Like GetFileAttributes()
 */
int windows_get_file_attributes(const char* file);

/**
 * Like SetFileAttributes()
 */
int windows_set_file_attributes(const char* file, int attributes);

/**
 * Convert Windows error to errno.
 */
void windows_errno(DWORD error);

/**
 * Return the executable directory.
 *
 * Ends with \
 */
const wchar_t* windows_exedir(void);

/**
 * Return if we are in the Wine environment.
 */
int windows_is_wine(void);

/****************************************************************************/
/* thread */

#define HAVE_THREAD 1
typedef void* windows_thread_t;
typedef CRITICAL_SECTION windows_mutex_t;
typedef CONDITION_VARIABLE windows_cond_t;
typedef void* windows_key_t;
/* remap to pthread */
#define thread_id_t windows_thread_t
#define thread_mutex_t windows_mutex_t
#define thread_cond_t windows_cond_t
#define thread_key_t windows_key_t
#define pthread_mutex_init windows_mutex_init
#define pthread_mutex_destroy windows_mutex_destroy
#define pthread_mutex_lock windows_mutex_lock
#define pthread_mutex_unlock windows_mutex_unlock
#define pthread_cond_init windows_cond_init
#define pthread_cond_destroy windows_cond_destroy
#define pthread_cond_signal windows_cond_signal
#define pthread_cond_broadcast windows_cond_broadcast
#define pthread_cond_wait windows_cond_wait
#define pthread_create windows_create
#define pthread_join windows_join
#define pthread_key_create windows_key_create
#define pthread_key_delete windows_key_delete
#define pthread_getspecific windows_getspecific
#define pthread_setspecific windows_setspecific

/**
 * Like the pthread_* equivalent.
 */
int windows_mutex_init(windows_mutex_t* mutex, void* attr);
int windows_mutex_destroy(windows_mutex_t* mutex);
int windows_mutex_lock(windows_mutex_t* mutex);
int windows_mutex_unlock(windows_mutex_t* mutex);
int windows_cond_init(windows_cond_t* cond, void* attr);
int windows_cond_destroy(windows_cond_t* cond);
int windows_cond_signal(windows_cond_t* cond);
int windows_cond_broadcast(windows_cond_t* cond);
int windows_cond_wait(windows_cond_t* cond, windows_mutex_t* mutex);
int windows_create(thread_id_t* thread, void* attr, void* (*func)(void*), void* arg);
int windows_join(thread_id_t thread, void** retval);

/**
 * Windows thread variables
 *
 * WARNING: windows_key_create and windows_key_delete must be called from a monothread context
 */
int windows_key_create(windows_key_t* key, void (*destructor)(void*));
int windows_key_delete(windows_key_t key);
void* windows_getspecific(windows_key_t key);
int windows_setspecific(windows_key_t key, void* value);

#endif
#endif

