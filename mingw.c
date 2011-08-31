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

#include "portable.h"

#ifdef __MINGW32__ /* Only for MingW */

#include "mingw.h"

/**
 * Number of conversion buffers.
 */
#define CONV_ROLL 4

/**
 * Buffers for UTF16.
 */
static unsigned conv_utf16 = 0;
static wchar_t conv_utf16_buffer[CONV_ROLL][PATH_MAX];

/**
 * Buffers for UTF8.
 */
static unsigned conv_utf8 = 0;
static char conv_utf8_buffer[CONV_ROLL][PATH_MAX];

/**
 * Converts from UTF8 to UTF16.
 */
static wchar_t* u8tou16(const char* src)
{
	int ret;

	if (++conv_utf16 == CONV_ROLL)
		conv_utf16 = 0;

	ret = MultiByteToWideChar(CP_UTF8, 0, src, -1, conv_utf16_buffer[conv_utf16], sizeof(conv_utf16_buffer[0]) / sizeof(wchar_t));

	if (ret <= 0) {
		fprintf(stderr, "Error converting name '%s' from UTF-8 to UTF-16\n", src);
		exit(EXIT_FAILURE);
	}

	return conv_utf16_buffer[conv_utf16];
}

/**
 * Converts from UTF16 to UTF8.
 */
static char* u16tou8(const wchar_t* src)
{
	int ret;

	if (++conv_utf8 == CONV_ROLL)
		conv_utf8 = 0;

	ret = WideCharToMultiByte(CP_UTF8, 0, src, -1, conv_utf8_buffer[conv_utf8], sizeof(conv_utf8_buffer[0]), 0, 0);
	if (ret < 0) {
		fwprintf(stderr, L"Error converting name %s from UTF-16 to UTF-8\n", src);
		exit(EXIT_FAILURE);
	}

	return conv_utf8_buffer[conv_utf8];
}

/**
 * Converts Windows info to the Unix stat format.
 */
static void windows_info2stat(const BY_HANDLE_FILE_INFORMATION* info, struct windows_stat* st)
{
	/* Convert special attributes to a char device */
	/* Note that the FILE_ATTRIBUTE_HIDDEN attribute is intentionally ignored as */
	/* hidden files should be backuped like any other */
	if ((info->dwFileAttributes & FILE_ATTRIBUTE_DEVICE) != 0) {
		st->st_mode = S_IFBLK;
	} else if ((info->dwFileAttributes & (FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_OFFLINE | FILE_ATTRIBUTE_REPARSE_POINT)) != 0) {
		st->st_mode = S_IFCHR;
	} else if ((info->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
		st->st_mode = S_IFDIR;
	} else {
		st->st_mode = S_IFREG;
	}

	st->st_size = info->nFileSizeHigh;
	st->st_size <<= 32;
	st->st_size |= info->nFileSizeLow;

	st->st_mtime = info->ftLastWriteTime.dwHighDateTime;
	st->st_mtime <<= 32;
	st->st_mtime |= info->ftLastWriteTime.dwLowDateTime;

	/*
	 * Convert to unix time
	 *
	 * How To Convert a UNIX time_t to a Win32 FILETIME or SYSTEMTIME
	 * http://support.microsoft.com/kb/167296
	 */
	st->st_mtime = (st->st_mtime - 116444736000000000LL) / 10000000;

	st->st_ino = info->nFileIndexHigh;
	st->st_ino <<= 32;
	st->st_ino |= info->nFileIndexLow;

	st->st_nlink = info->nNumberOfLinks;
}

/**
 * Converts Windows findfirst info to the Unix stat format.
 */
static void windows_finddata2stat(const WIN32_FIND_DATAW* info, struct windows_stat* st)
{
	/* Convert special attributes */
	/* Note that the FILE_ATTRIBUTE_HIDDEN attribute is intentionally ignored as */
	/* hidden files should be backuped like any other */
	if ((info->dwFileAttributes & FILE_ATTRIBUTE_DEVICE) != 0) {
		st->st_mode = S_IFBLK;
	} else if ((info->dwFileAttributes & (
			FILE_ATTRIBUTE_SYSTEM /* System files */
			| FILE_ATTRIBUTE_TEMPORARY /* Files going to be deleted on close */
			| FILE_ATTRIBUTE_OFFLINE
			| FILE_ATTRIBUTE_REPARSE_POINT /* Symbolic links */
			)) != 0) {
		st->st_mode = S_IFCHR;
	} else if ((info->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
		st->st_mode = S_IFDIR;
	} else {
		st->st_mode = S_IFREG;
	}

	st->st_size = info->nFileSizeHigh;
	st->st_size <<= 32;
	st->st_size |= info->nFileSizeLow;

	st->st_mtime = info->ftLastWriteTime.dwHighDateTime;
	st->st_mtime <<= 32;
	st->st_mtime |= info->ftLastWriteTime.dwLowDateTime;

	/*
	 * Convert to unix time
	 *
	 * How To Convert a UNIX time_t to a Win32 FILETIME or SYSTEMTIME
	 * http://support.microsoft.com/kb/167296
	 */
	st->st_mtime = (st->st_mtime - 116444736000000000LL) / 10000000;

	/* No inode information available */
	st->st_ino = 0;

	/* No link information available */
	st->st_nlink = 0;
}

static void windows_finddata2dirent(const WIN32_FIND_DATAW* info, struct windows_dirent* dirent)
{
	const char* name;
	size_t len;

	name = u16tou8(info->cFileName);
	
	len = strlen(name);
	
	if (len + 1 >= sizeof(dirent->d_name)) {
		fprintf(stderr, "Name too long\n");
		exit(EXIT_FAILURE);
	}

	memcpy(dirent->d_name, name, len + 1);
}

/**
 * Converts Windows error to errno.
 */
static void windows_errno(DWORD error)
{
	switch (error) {
	case ERROR_INVALID_HANDLE :
		errno = EBADF;
		break;
	case ERROR_FILE_NOT_FOUND :
		errno = ENOENT;
		break;
	case ERROR_ACCESS_DENIED :
		errno = EACCES;
		break;
	default:
		errno = EIO;
		break;
	}
}

int windows_fstat(int fd, struct windows_stat* st)
{
	BY_HANDLE_FILE_INFORMATION info;
	HANDLE h;

	h = (HANDLE)_get_osfhandle(fd);
	if (h == INVALID_HANDLE_VALUE) {
		errno = EBADF;
		return -1;
	}

	if (!GetFileInformationByHandle(h, &info))  {
		windows_errno(GetLastError());
		return -1;
	}

	windows_info2stat(&info, st);

	return 0;
}

int windows_lstat(const char* file, struct windows_stat* st)
{
	HANDLE h;
	WIN32_FIND_DATAW data;

	h = FindFirstFileW(u8tou16(file),  &data);
	if (h == INVALID_HANDLE_VALUE) {
		windows_errno(GetLastError());
		return -1;
	}

	if (!FindClose(h)) {
		windows_errno(GetLastError());
		return -1;
	}

	windows_finddata2stat(&data, st);

	return 0;
}

int windows_access(const char* file, int mode)
{
	return _waccess(u8tou16(file), mode);
}

int windows_mkdir(const char* file)
{
	return _wmkdir(u8tou16(file));
}

int lstat_inode(const char* file, struct windows_stat* st)
{
	BY_HANDLE_FILE_INFORMATION info;
	HANDLE h;

	/*
	 * Open the handle of the file.
	 *
	 * Use FILE_FLAG_BACKUP_SEMANTICS to open directories and to override the file security checks.
	 * Use FILE_FLAG_OPEN_REPARSE_POINT to open symbolic links and not the their target.
	 */
	h = CreateFileW(u8tou16(file), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, 0);
	if (h == INVALID_HANDLE_VALUE) {
		windows_errno(GetLastError());
		return -1;
	}

	if (!GetFileInformationByHandle(h, &info))  {
		DWORD error = GetLastError();
		CloseHandle(h);
		windows_errno(error);
		return -1;
	}

	if (!CloseHandle(h)) {
		windows_errno(GetLastError());
		return -1;
	}

	windows_info2stat(&info, st);

	return 0;
}

int windows_ftruncate(int fd, off64_t off)
{
	HANDLE h;
	LARGE_INTEGER pos;

	if (fd == -1) {
		errno = EBADF;
		return -1;
	}

	h = (HANDLE)_get_osfhandle(fd);
	if (h == INVALID_HANDLE_VALUE) {
		errno = EBADF;
		return -1;
	}

	pos.QuadPart = off;
	if (!SetFilePointerEx(h, pos, 0, FILE_BEGIN)) {
		windows_errno(GetLastError());
		return -1;
	}

	if (!SetEndOfFile(h)) {
		windows_errno(GetLastError());
		return -1;
	}

	return 0;
}

int windows_futimes(int fd, struct timeval tv[2])
{
	HANDLE h;
	FILETIME ft;
	uint64_t mtime;

	if (fd == -1) {
		errno = EBADF;
		return -1;
	}

	h = (HANDLE)_get_osfhandle(fd);
	if (h == INVALID_HANDLE_VALUE) {
		errno = EBADF;
		return -1;
	}

	/*
	 * Convert to windows time
	 *
	 * How To Convert a UNIX time_t to a Win32 FILETIME or SYSTEMTIME
	 * http://support.microsoft.com/kb/167296
	 */
	mtime = tv[0].tv_sec;
	mtime *= 10000000;
	mtime += tv[0].tv_usec * 10;
	mtime += 116444736000000000;

	ft.dwHighDateTime = mtime >> 32;
	ft.dwLowDateTime = mtime;

	if (!SetFileTime(h, 0, 0, &ft)) {
		windows_errno(GetLastError());
		return -1;
	}

	return 0;
}

int windows_rename(const char* a, const char* b)
{
	/*
	 * Implements an atomic rename in Windows.
	 * Not really atomic at now to support XP.
	 *
	 * Is an atomic file rename (with overwrite) possible on Windows?
	 * http://stackoverflow.com/questions/167414/is-an-atomic-file-rename-with-overwrite-possible-on-windows
	 */
	if (!MoveFileExW(u8tou16(a), u8tou16(b), MOVEFILE_REPLACE_EXISTING)) {
		windows_errno(GetLastError());
		return -1;
	}

	return 0;
}

int windows_remove(const char* a)
{
	if (_wremove(u8tou16(a)) != 0) {
		windows_errno(GetLastError());
		return -1;
	}

	return 0;
}

FILE* windows_fopen(const char* file, const char* mode)
{
	return _wfopen(u8tou16(file), u8tou16(mode));
}

int windows_open(const char* file, int flags, ...)
{
	va_list args;
	int ret;

	va_start(args, flags);
	if ((flags & O_CREAT) != 0)
		ret = _wopen(u8tou16(file), flags, va_arg(args, int));
	else
		ret = _wopen(u8tou16(file), flags);
	va_end(args);

	return ret;
}

windows_dir* windows_opendir(const char* dir)
{
	wchar_t* wdir;
	windows_dir* dirstream;
	size_t len;

	dirstream = malloc(sizeof(windows_dir));
	if (!dirstream) {
		fprintf(stderr, "Low memory\n");
		exit(EXIT_FAILURE);
	}

	wdir = u8tou16(dir);

	/* add final / and * */
	len = wcslen(wdir);
	if (len!= 0 && wdir[len-1] != '/')
		wdir[len++] = L'/';
	wdir[len++] = L'*';
	wdir[len++] = 0;

	dirstream->h = FindFirstFileW(wdir, &dirstream->data);
	if (dirstream->h == INVALID_HANDLE_VALUE) {
		DWORD error = GetLastError();

		if (error == ERROR_FILE_NOT_FOUND) {
			dirstream->flags = -1; /* empty dir */
			return dirstream;
		}

		free(dirstream);
		windows_errno(error);
		return 0;
	}

	dirstream->flags = 1;

	windows_finddata2dirent(&dirstream->data, &dirstream->buffer);

	return dirstream;
}

struct windows_dirent* windows_readdir(windows_dir* dirstream)
{
	if (dirstream->flags == -1) {
		errno = 0; /* end of stream */
		return 0;
	}

	if (dirstream->flags == 1) {
		dirstream->flags = 0;
		return &dirstream->buffer;
	}

	if (!FindNextFileW(dirstream->h, &dirstream->data)) {
		DWORD error = GetLastError();

		if (error == ERROR_NO_MORE_FILES) {
			errno = 0; /* end of stream */
			return 0;
		}

		windows_errno(error);
		return 0;
	}

	windows_finddata2dirent(&dirstream->data, &dirstream->buffer);

	return &dirstream->buffer;
}

int windows_closedir(windows_dir* dirstream)
{
	if (dirstream->h != INVALID_HANDLE_VALUE) {
		if (!FindClose(dirstream->h)) {
			DWORD error = GetLastError();
			free(dirstream);

			windows_errno(error);
			return -1;
		}
	}

	free(dirstream);

	return 0;
}

#endif

