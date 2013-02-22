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

/* Adds missing Windows declaration */
typedef struct _FILE_ATTRIBUTE_TAG_INFO {
	DWORD FileAttributes;
	DWORD ReparseTag;
} FILE_ATTRIBUTE_TAG_INFO;
#define FileAttributeTagInfo 9

#ifndef IO_REPARSE_TAG_DEDUP
#define IO_REPARSE_TAG_DEDUP (0x80000013)
#endif
#ifndef IO_REPARSE_TAG_NFS
#define IO_REPARSE_TAG_NFS (0x80000014)
#endif
#ifndef IO_REPARSE_TAG_MOUNT_POINT
#define IO_REPARSE_TAG_MOUNT_POINT (0xA0000003)
#endif
#ifndef IO_REPARSE_TAG_SYMLINK
#define IO_REPARSE_TAG_SYMLINK (0xA000000C)
#endif

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
 * Converts a generic string from UTF8 to UTF16.
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
 * Converts a generic string from UTF16 to UTF8.
 */
static char* u16tou8(const wchar_t* src)
{
	int ret;

	if (++conv_utf8 == CONV_ROLL)
		conv_utf8 = 0;

	ret = WideCharToMultiByte(CP_UTF8, 0, src, -1, conv_utf8_buffer[conv_utf8], sizeof(conv_utf8_buffer[0]), 0, 0);
	if (ret <= 0) {
		fwprintf(stderr, L"Error converting name %s from UTF-16 to UTF-8\n", src);
		exit(EXIT_FAILURE);
	}

	return conv_utf8_buffer[conv_utf8];
}

/**
 * Converts a generic string from UTF16 to UTF8.
 */
static char* u16tou8n(const wchar_t* src, size_t number_of_wchar, size_t* result_length_without_terminator)
{
	int ret;

	if (++conv_utf8 == CONV_ROLL)
		conv_utf8 = 0;

	ret = WideCharToMultiByte(CP_UTF8, 0, src, number_of_wchar, conv_utf8_buffer[conv_utf8], sizeof(conv_utf8_buffer[0]), 0, 0);
	if (ret <= 0) {
		fwprintf(stderr, L"Error converting from UTF-16 to UTF-8\n");
		exit(EXIT_FAILURE);
	}

	*result_length_without_terminator = ret;

	return conv_utf8_buffer[conv_utf8];
}

/**
 * Check if the char is a forward or back slash.
 */
static int is_slash(char c)
{
	return c == '/' || c == '\\';
}

/**
 * Converts a path to the Windows format.
 *
 * If only_is_required is 1, the extended-length format is used only if required.
 *
 * The exact operation done is:
 * - If it's a '\\?\' path, convert any '/' to '\'.
 * - If it's a disk designator path, like 'D:\' or 'D:/', it prepends '\\?\' to the path and convert any '/' to '\'.
 * - If it's a UNC path, like ''\\server'', it prepends '\\?\UNC\' to the path and convert any '/' to '\'.
 * - Otherwise, only the UTF conversion is done. In this case Windows imposes a limit of 260 chars, and automatically convert any '/' to '\'.
 *
 * For more details see:
 * Naming Files, Paths, and Namespaces
 * http://msdn.microsoft.com/en-us/library/windows/desktop/aa365247%28v=vs.85%29.aspx#maxpath
 */
static wchar_t* convert_arg(const char* src, int only_if_required)
{
	int ret;
	wchar_t* dst;
	int count;

	if (++conv_utf16 == CONV_ROLL)
		conv_utf16 = 0;

	dst = conv_utf16_buffer[conv_utf16];

	/* note that we always check for both / and \ because the path is blindly */
	/* converted to unix format by path_import() */

	if (only_if_required && strlen(src) < 260 - 12) {
		/* it's a short path */
		/* 260 is the MAX_PATH, note that it includes the space for the terminating NUL */
		/* 12 is an additional space for filename, required when creating directory */

		/* do nothing */
	} else if (is_slash(src[0]) && is_slash(src[1]) && src[2] == '?' && is_slash(src[3])) {
		/* if it's already a '\\?\' path */

		/* do nothing */
	} else if (is_slash(src[0]) && is_slash(src[1])) {
		/* if it is a UNC path, like '\\server' */

		/* prefix with '\\?\UNC\' */
		*dst++ = L'\\';
		*dst++ = L'\\';
		*dst++ = L'?';
		*dst++ = L'\\';
		*dst++ = L'U';
		*dst++ = L'N';
		*dst++ = L'C';
		*dst++ = L'\\';

		/* skip initial '\\' */
		src += 2;
	} else if (src[0] != 0 && src[1] == ':' && is_slash(src[2])) {
		/* if it is a disk designator path, like 'D:\' or 'D:/' */

		/* prefix with '\\?\' */
		*dst++ = L'\\';
		*dst++ = L'\\';
		*dst++ = L'?';
		*dst++ = L'\\';
	}

	/* chars already used */
	count = dst - conv_utf16_buffer[conv_utf16];

	ret = MultiByteToWideChar(CP_UTF8, 0, src, -1, dst, sizeof(conv_utf16_buffer[0]) / sizeof(wchar_t) - count);

	if (ret <= 0) {
		fprintf(stderr, "Error converting name '%s' from UTF-8 to UTF-16\n", src);
		exit(EXIT_FAILURE);
	}

	/* convert any / to \ */
	/* note that in UTF-16, it's not possible to have '/' used as part */
	/* of a pair of codes representing a single UNICODE char */
	/* See: http://en.wikipedia.org/wiki/UTF-16 */
	while (*dst) {
		if (*dst == L'/')
			*dst = L'\\';
		++dst;
	}

	return conv_utf16_buffer[conv_utf16];
}

#define convert(a) convert_arg(a, 0)
#define convert_if_required(a) convert_arg(a, 1)

/**
 * Portable implementation of GetFileInformationByHandleEx.
 * This function is not available in Windows XP.
 */
static int flag_GetFileInformationByHandleEx = 0;
static BOOL (WINAPI* ptr_GetFileInformationByHandleEx)(HANDLE, DWORD, LPVOID, DWORD) = 0;

static BOOL GetReparseTagInfoByHandle(HANDLE hFile, FILE_ATTRIBUTE_TAG_INFO* lpFileAttributeTagInfo, DWORD dwFileAttributes)
{
	/* if not yet initialized, do it now */
	if (!flag_GetFileInformationByHandleEx) {
		HMODULE h = GetModuleHandleA("KERNEL32.DLL");
		if (h) {
			ptr_GetFileInformationByHandleEx = (void*)GetProcAddress(h, "GetFileInformationByHandleEx");
		}
		flag_GetFileInformationByHandleEx = 1;
	}

	/* if not reparse point, return no info */
	if ((dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
		lpFileAttributeTagInfo->FileAttributes = dwFileAttributes;
		lpFileAttributeTagInfo->ReparseTag = 0;
		return TRUE;
	}

	/* if not available, return no info */
	if (!ptr_GetFileInformationByHandleEx) {
		lpFileAttributeTagInfo->FileAttributes = dwFileAttributes;
		lpFileAttributeTagInfo->ReparseTag = 0;
		return TRUE;
	}

	/* do the real call */
	return ptr_GetFileInformationByHandleEx(hFile, FileAttributeTagInfo, lpFileAttributeTagInfo, sizeof(FILE_ATTRIBUTE_TAG_INFO));
}

/**
 * Converts Windows info to the Unix stat format.
 */
static void windows_info2stat(const BY_HANDLE_FILE_INFORMATION* info, const FILE_ATTRIBUTE_TAG_INFO* tag, struct windows_stat* st)
{
	uint64_t mtime;

	/* Convert special attributes */
	if ((info->dwFileAttributes & FILE_ATTRIBUTE_DEVICE) != 0) {
		st->st_mode = S_IFBLK;
		st->st_desc = "device";
	} else if ((info->dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) != 0) { /* System */
		st->st_mode = S_IFCHR;
		st->st_desc = "system";
	} else if ((info->dwFileAttributes & FILE_ATTRIBUTE_OFFLINE) != 0) { /* Offline */
		st->st_mode = S_IFCHR;
		st->st_desc = "offline";
	} else if ((info->dwFileAttributes & FILE_ATTRIBUTE_TEMPORARY) != 0) { /* Files going to be deleted on close */
		st->st_mode = S_IFCHR;
		st->st_desc = "temporary";
	} else if ((info->dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) { /* Reparse point */
		switch (tag->ReparseTag) {
		/* For deduplicated files, assume that they are regular ones */
		case IO_REPARSE_TAG_DEDUP :
			if ((info->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
				st->st_mode = S_IFDIR;
				st->st_desc = "directory-dedup";
			} else {
				st->st_mode = S_IFREG;
				st->st_desc = "regular-dedup";
			}
			break;
		/* All the other are skipped as reparse-point */
		case IO_REPARSE_TAG_MOUNT_POINT :
			st->st_mode = S_IFCHR;
			st->st_desc = "reparse-point-mount";
			break;
		case IO_REPARSE_TAG_NFS :
			st->st_mode = S_IFCHR;
			st->st_desc = "reparse-point-nfs";
			break;
		case IO_REPARSE_TAG_SYMLINK :
			st->st_mode = S_IFLNK;
			st->st_desc = "reparse-point-symlink";
			break;
		default:
			st->st_mode = S_IFCHR;
			st->st_desc = "reparse-point";
			break;
		}
	} else if ((info->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
		st->st_mode = S_IFDIR;
		st->st_desc = "directory";
	} else {
		st->st_mode = S_IFREG;
		st->st_desc = "regular";
	}

	/* Store the HIDDEN attribute in a separate field */
	st->st_hidden = (info->dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0;

	st->st_size = info->nFileSizeHigh;
	st->st_size <<= 32;
	st->st_size |= info->nFileSizeLow;

	mtime = info->ftLastWriteTime.dwHighDateTime;
	mtime <<= 32;
	mtime |= info->ftLastWriteTime.dwLowDateTime;

	/*
	 * Convert to unix time
	 *
	 * How To Convert a UNIX time_t to a Win32 FILETIME or SYSTEMTIME
	 * http://support.microsoft.com/kb/167296
	 */
	mtime -= 116444736000000000LL;
	st->st_mtime = mtime / 10000000;
	st->st_mtimensec = (mtime % 10000000) * 100;

	st->st_ino = info->nFileIndexHigh;
	st->st_ino <<= 32;
	st->st_ino |= info->nFileIndexLow;

	st->st_nlink = info->nNumberOfLinks;

	st->st_dev = info->dwVolumeSerialNumber;
}

/**
 * Converts Windows findfirst info to the Unix stat format.
 */
static void windows_finddata2stat(const WIN32_FIND_DATAW* info, struct windows_stat* st)
{
	uint64_t mtime;

	/* Convert special attributes */
	if ((info->dwFileAttributes & FILE_ATTRIBUTE_DEVICE) != 0) {
		st->st_mode = S_IFBLK;
		st->st_desc = "device";
	} else if ((info->dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) != 0) { /* System */
		st->st_mode = S_IFCHR;
		st->st_desc = "system";
	} else if ((info->dwFileAttributes & FILE_ATTRIBUTE_OFFLINE) != 0) { /* Offline */
		st->st_mode = S_IFCHR;
		st->st_desc = "offline";
	} else if ((info->dwFileAttributes & FILE_ATTRIBUTE_TEMPORARY) != 0) { /* Files going to be deleted on close */
		st->st_mode = S_IFCHR;
		st->st_desc = "temporary";
	} else if ((info->dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) { /* Reparse point */
		switch (info->dwReserved0) {
		/* For deduplicated files, assume that they are regular ones */
		case IO_REPARSE_TAG_DEDUP :
			if ((info->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
				st->st_mode = S_IFDIR;
				st->st_desc = "directory-dedup";
			} else {
				st->st_mode = S_IFREG;
				st->st_desc = "regular-dedup";
			}
			break;
		/* All the other are skipped as reparse-point */
		case IO_REPARSE_TAG_MOUNT_POINT :
			st->st_mode = S_IFCHR;
			st->st_desc = "reparse-point-mount";
			break;
		case IO_REPARSE_TAG_NFS :
			st->st_mode = S_IFCHR;
			st->st_desc = "reparse-point-nfs";
			break;
		case IO_REPARSE_TAG_SYMLINK :
			st->st_mode = S_IFLNK;
			st->st_desc = "reparse-point-symlink";
			break;
		default:
			st->st_mode = S_IFCHR;
			st->st_desc = "reparse-point";
			break;
		}
	} else if ((info->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
		st->st_mode = S_IFDIR;
		st->st_desc = "directory";
	} else {
		st->st_mode = S_IFREG;
		st->st_desc = "regular";
	}

	/* Store the HIDDEN attribute in a separate field */
	st->st_hidden = (info->dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0;

	st->st_size = info->nFileSizeHigh;
	st->st_size <<= 32;
	st->st_size |= info->nFileSizeLow;

	mtime = info->ftLastWriteTime.dwHighDateTime;
	mtime <<= 32;
	mtime |= info->ftLastWriteTime.dwLowDateTime;

	/*
	 * Convert to unix time
	 *
	 * How To Convert a UNIX time_t to a Win32 FILETIME or SYSTEMTIME
	 * http://support.microsoft.com/kb/167296
	 */
	mtime -= 116444736000000000LL;
	st->st_mtime = mtime / 10000000;
	st->st_mtimensec = (mtime % 10000000) * 100;

	/* No inode information available */
	st->st_ino = 0;

	/* No link information available */
	st->st_nlink = 0;

	/* No device information available */
	st->st_dev = 0;
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

	/* Store the HIDDEN attribute in a separate field */
	dirent->d_hidden = (info->dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0;
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
	case ERROR_PATH_NOT_FOUND : /* in GetFileAttributeW() if internal path not found */
		errno = ENOENT;
		break;
	case ERROR_ACCESS_DENIED : /* in CreateDirectoryW() if dir is scheduled for deletion */
	case ERROR_CURRENT_DIRECTORY : /* in RemoveDirectoryW() if removing the current directory */
	case ERROR_SHARING_VIOLATION : /* in RemoveDirectoryW() if in use */
		errno = EACCES;
		break;
	case ERROR_ALREADY_EXISTS : /* in CreateDirectoryW() if already exists */
		errno = EEXIST;
		break;
	case ERROR_DISK_FULL :
		errno = ENOSPC;
		break;
	case ERROR_BUFFER_OVERFLOW :
		errno = ENAMETOOLONG;
		break;
	case ERROR_NOT_ENOUGH_MEMORY :
		errno = ENOMEM;
		break;
	case ERROR_NOT_SUPPORTED : /* in CreateSymlinkW() if not present in kernel32 */
		errno = ENOSYS;
		break;
	case ERROR_PRIVILEGE_NOT_HELD : /* in CreateSymlinkW if no SeCreateSymbolicLinkPrivilige permission */
		errno = EPERM;
		break;
	default:
		fprintf(stderr, "Unexpected Windows error %d.\n", (int)error);
		errno = EIO;
		break;
	}
}

int windows_fstat(int fd, struct windows_stat* st)
{
	BY_HANDLE_FILE_INFORMATION info;
	FILE_ATTRIBUTE_TAG_INFO tag;
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

	if (!GetReparseTagInfoByHandle(h, &tag, info.dwFileAttributes)) {
		windows_errno(GetLastError());
		return -1;
	}

	windows_info2stat(&info, &tag, st);

	return 0;
}

int windows_lstat(const char* file, struct windows_stat* st)
{
	HANDLE h;
	WIN32_FIND_DATAW data;

	/* FindFirstFileW by default gets information of symbolic links and not of their targets */
	h = FindFirstFileW(convert(file),  &data);
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

void windows_dirent_lstat(const struct windows_dirent* dd, struct windows_stat* st)
{
	windows_finddata2stat(&dd->d_data, st);
}

int windows_access(const char* file, int mode)
{
	DWORD attr;

	/* Check only for existence */
	if (mode != F_OK) {
		errno = ENOSYS;
		return -1;
	}

	attr = GetFileAttributesW(convert(file));
	if (attr == 0xFFFFFFFF) {
		windows_errno(GetLastError());
		return -1;
	}

	return 0;
}

int windows_mkdir(const char* file)
{
	if (!CreateDirectoryW(convert(file), 0)) {
		windows_errno(GetLastError());
		return -1;
	}

	return 0;
}

int windows_rmdir(const char* file)
{
	if (!RemoveDirectoryW(convert(file))) {
		windows_errno(GetLastError());
		return -1;
	}

	return 0;
}

int lstat_ex(const char* file, struct windows_stat* st)
{
	BY_HANDLE_FILE_INFORMATION info;
	FILE_ATTRIBUTE_TAG_INFO tag;
	HANDLE h;

	/*
	 * Open the handle of the file.
	 *
	 * Use FILE_FLAG_BACKUP_SEMANTICS to open directories and to override the file security checks.
	 * Use FILE_FLAG_OPEN_REPARSE_POINT to open symbolic links and not the their target.
	 */
	h = CreateFileW(convert(file), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, 0);
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

	if (!GetReparseTagInfoByHandle(h, &tag, info.dwFileAttributes)) {
		DWORD error = GetLastError();
		CloseHandle(h);
		windows_errno(error);
		return -1;
	}

	if (!CloseHandle(h)) {
		windows_errno(GetLastError());
		return -1;
	}

	windows_info2stat(&info, &tag, st);

	return 0;
}

int windows_stat(const char* file, struct windows_stat* st)
{
	BY_HANDLE_FILE_INFORMATION info;
	FILE_ATTRIBUTE_TAG_INFO tag;
	HANDLE h;

	/*
	 * Open the handle of the file.
	 *
	 * Use FILE_FLAG_BACKUP_SEMANTICS to open directories and to override the file security checks.
	 */
	h = CreateFileW(convert(file), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, 0);
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

	if (!GetReparseTagInfoByHandle(h, &tag, info.dwFileAttributes)) {
		DWORD error = GetLastError();
		CloseHandle(h);
		windows_errno(error);
		return -1;
	}

	if (!CloseHandle(h)) {
		windows_errno(GetLastError());
		return -1;
	}

	windows_info2stat(&info, &tag, st);

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

int windows_futimens(int fd, struct windows_timespec tv[2])
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
	mtime += tv[0].tv_nsec / 100;
	mtime += 116444736000000000;

	ft.dwHighDateTime = mtime >> 32;
	ft.dwLowDateTime = mtime;

	if (!SetFileTime(h, 0, 0, &ft)) {
		windows_errno(GetLastError());
		return -1;
	}

	return 0;
}

int windows_rename(const char* from, const char* to)
{
	/*
	 * Implements an atomic rename in Windows.
	 * Not really atomic at now to support XP.
	 *
	 * Is an atomic file rename (with overwrite) possible on Windows?
	 * http://stackoverflow.com/questions/167414/is-an-atomic-file-rename-with-overwrite-possible-on-windows
	 */
	if (!MoveFileExW(convert(from), convert(to), MOVEFILE_REPLACE_EXISTING)) {
		windows_errno(GetLastError());
		return -1;
	}

	return 0;
}

int windows_remove(const char* file)
{
	if (!DeleteFileW(convert(file))) {
		windows_errno(GetLastError());
		return -1;
	}

	return 0;
}

FILE* windows_fopen(const char* file, const char* mode)
{
	return _wfopen(convert(file), u8tou16(mode));
}

int windows_open(const char* file, int flags, ...)
{
	va_list args;
	int ret;

	va_start(args, flags);
	if ((flags & O_CREAT) != 0)
		ret = _wopen(convert(file), flags, va_arg(args, int));
	else
		ret = _wopen(convert(file), flags);
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

	wdir = convert(dir);

	/* add final / and * */
	len = wcslen(wdir);
	if (len!= 0 && wdir[len-1] != '\\')
		wdir[len++] = L'\\';
	wdir[len++] = L'*';
	wdir[len++] = 0;

	dirstream->h = FindFirstFileW(wdir, &dirstream->buffer.d_data);
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

	windows_finddata2dirent(&dirstream->buffer.d_data, &dirstream->buffer);

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

	if (!FindNextFileW(dirstream->h, &dirstream->buffer.d_data)) {
		DWORD error = GetLastError();

		if (error == ERROR_NO_MORE_FILES) {
			errno = 0; /* end of stream */
			return 0;
		}

		windows_errno(error);
		return 0;
	}

	windows_finddata2dirent(&dirstream->buffer.d_data, &dirstream->buffer);

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

int windows_dirent_hidden(struct dirent* dd)
{
	return dd->d_hidden;
}

const char* windows_stat_desc(struct stat* st)
{
	return st->st_desc;
}

void windows_sleep(unsigned seconds)
{
	Sleep(seconds * 1000);
}

int windows_link(const char* existing, const char* file)
{
	if (!CreateHardLinkW(convert(file), convert(existing), 0)) {
		windows_errno(GetLastError());
		return -1;
	}

	return 0;
}

/**
 * Portable implementation of CreateSymbolicLinkW.
 * This function is not available in Windows XP.
 */
static int flag_CreateSymbolicLinkW = 0;
static BOOLEAN (WINAPI* ptr_CreateSymbolicLinkW)(LPWSTR, LPWSTR, DWORD);

int windows_symlink(const char* existing, const char* file)
{
	/* if not yet initialized, do it now */
	if (!flag_CreateSymbolicLinkW) {
		HMODULE h = GetModuleHandleA("KERNEL32.DLL");
		if (h) {
			ptr_CreateSymbolicLinkW = (void*)GetProcAddress(h, "CreateSymbolicLinkW");
		}
		flag_CreateSymbolicLinkW = 1;
	}

	if (!ptr_CreateSymbolicLinkW) {
		windows_errno(ERROR_NOT_SUPPORTED);
		return -1;
	}

	/* We must convert to the extended-length \\?\ format if the path is too long */
	/* otherwise the link creation fails. */
	/* But we don't want to always convert it, to avoid to recreate */
	/* user symlinks different than they were before */
	if (!ptr_CreateSymbolicLinkW(convert(file), convert_if_required(existing), 0)) {
		windows_errno(GetLastError());
		return -1;
	}

	return 0;
}

/* Adds missing defition in MingW winnt.h */
#ifndef FSCTL_GET_REPARSE_POINT
#define FSCTL_GET_REPARSE_POINT 0x000900a8
#endif
#ifndef REPARSE_DATA_BUFFER_HEADER_SIZE
typedef struct _REPARSE_DATA_BUFFER {
	DWORD  ReparseTag;
	WORD   ReparseDataLength;
	WORD   Reserved;
	_ANONYMOUS_UNION union {
		struct {
			WORD   SubstituteNameOffset;
			WORD   SubstituteNameLength;
			WORD   PrintNameOffset;
			WORD   PrintNameLength;
			ULONG  Flags;
			WCHAR PathBuffer[1];
		} SymbolicLinkReparseBuffer;
		struct {
			WORD   SubstituteNameOffset;
			WORD   SubstituteNameLength;
			WORD   PrintNameOffset;
			WORD   PrintNameLength;
			WCHAR PathBuffer[1];
		} MountPointReparseBuffer;
		struct {
			BYTE   DataBuffer[1];
		} GenericReparseBuffer;
	} DUMMYUNIONNAME;
} REPARSE_DATA_BUFFER, *PREPARSE_DATA_BUFFER;
#endif

int windows_readlink(const char* file, char* buffer, size_t size)
{
	HANDLE h;
	const char* name;
	size_t len;
	unsigned char rdb_buffer[MAXIMUM_REPARSE_DATA_BUFFER_SIZE];
	REPARSE_DATA_BUFFER* rdb = (REPARSE_DATA_BUFFER*)rdb_buffer;
	DWORD ret;
	DWORD n;

	/*
	 * Open the handle of the file.
	 *
	 * Use FILE_FLAG_BACKUP_SEMANTICS to open directories and to override the file security checks.
	 * Use FILE_FLAG_OPEN_REPARSE_POINT to open symbolic links and not the their target.
	 */
	h = CreateFileW(convert(file), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, 0);
	if (h == INVALID_HANDLE_VALUE) {
		windows_errno(GetLastError());
		return -1;
	}

	/* read the reparse point */
	ret = DeviceIoControl(h, FSCTL_GET_REPARSE_POINT, 0, 0, rdb_buffer, sizeof(rdb_buffer), &n, 0);
	if (ret == 0) {
		windows_errno(GetLastError());
		CloseHandle(h);
		return -1;
	}

	CloseHandle(h);

	/* check if it's really a symbolic link */
	if (rdb->ReparseTag != IO_REPARSE_TAG_SYMLINK) {
		errno = EINVAL;
		return -1;
	}

	/* convert the name to UTF-8 */
	name = u16tou8n(rdb->SymbolicLinkReparseBuffer.PathBuffer + rdb->SymbolicLinkReparseBuffer.PrintNameOffset,
		rdb->SymbolicLinkReparseBuffer.PrintNameLength / 2, &len);

	/* check for overflow */
	if (len > size) {
		len = size;
	}

	memcpy(buffer, name, len);

	return len;
}

#endif

