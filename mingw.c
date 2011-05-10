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

#include "mingw.h"

#ifdef __MINGW32__ /* Only for MingW */

/**
 * Converts Windows info to the Unix stat format.
 */
static void windows_info2stat(const BY_HANDLE_FILE_INFORMATION* info, struct windows_stat* st)
{
	/* Convert special attributes to a char device */
	if ((info->dwFileAttributes & (FILE_ATTRIBUTE_DEVICE | FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_OFFLINE | FILE_ATTRIBUTE_REPARSE_POINT)) != 0) {
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
static void windows_finddata2stat(const WIN32_FIND_DATA* info, struct windows_stat* st)
{
	/* Convert special attributes to a char device */
	if ((info->dwFileAttributes & (FILE_ATTRIBUTE_DEVICE | FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_OFFLINE | FILE_ATTRIBUTE_REPARSE_POINT)) != 0) {
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

	/* No inode information available  */
	st->st_ino = 0;

	/* No link information available  */
	st->st_nlink = 0;
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

int windows_stat(const char* file, struct windows_stat* st)
{
	HANDLE h;
	WIN32_FIND_DATA data;

	h = FindFirstFile(file,  &data);
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

int stat_inode(const char* file, struct windows_stat* st)
{
	BY_HANDLE_FILE_INFORMATION info;
	HANDLE h;

	h = CreateFile(file, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, 0);
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

int windows_rename(const char* a, const char* b)
{
	/*
	 * Implements an atomic rename in Windows.
	 * Not really atomic at now to support XP.
	 *
	 * Is an atomic file rename (with overwrite) possible on Windows?
	 * http://stackoverflow.com/questions/167414/is-an-atomic-file-rename-with-overwrite-possible-on-windows
	 */
	if (!MoveFileEx(a, b, MOVEFILE_REPLACE_EXISTING)) {
		windows_errno(GetLastError());
		return -1;
	}

	return 0;
}

#endif
