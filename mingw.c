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
	if (ret < 0) {
		fwprintf(stderr, L"Error converting name %s from UTF-16 to UTF-8\n", src);
		exit(EXIT_FAILURE);
	}

	return conv_utf8_buffer[conv_utf8];
}

/**
 * Converts a path to the Windows format.
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
static wchar_t* convert(const char* src)
{
	int ret;
	wchar_t* dst;
	int count;

	if (++conv_utf16 == CONV_ROLL)
		conv_utf16 = 0;

	dst = conv_utf16_buffer[conv_utf16];

	if (src[0] == '\\' && src[1] == '\\' && src[2] == '?' && src[3] == '\\') {
		/* if it's already a '\\?\' path */

		/* do nothing */
	} else if (src[0] == '\\' && src[1] == '\\') {
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
	} else if (src[0] != 0 && src[1] == ':' && (src[2] == '\\' || src[2] == '/')) {
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
			st->st_mode = S_IFCHR;
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
			st->st_mode = S_IFCHR;
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
	case ERROR_BUFFER_OVERFLOW :
		errno = ENAMETOOLONG;
		break;
	case ERROR_NOT_ENOUGH_MEMORY :
		errno = ENOMEM;
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

int windows_access(const char* file, int mode)
{
	return _waccess(convert(file), mode);
}

int windows_mkdir(const char* file)
{
	return _wmkdir(convert(file));
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

int windows_rename(const char* a, const char* b)
{
	/*
	 * Implements an atomic rename in Windows.
	 * Not really atomic at now to support XP.
	 *
	 * Is an atomic file rename (with overwrite) possible on Windows?
	 * http://stackoverflow.com/questions/167414/is-an-atomic-file-rename-with-overwrite-possible-on-windows
	 */
	if (!MoveFileExW(convert(a), u8tou16(b), MOVEFILE_REPLACE_EXISTING)) {
		windows_errno(GetLastError());
		return -1;
	}

	return 0;
}

int windows_remove(const char* a)
{
	if (_wremove(convert(a)) != 0) {
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

int windows_stat_hidden(struct dirent* dd, struct windows_stat* st)
{
	(void)dd;
	return st->st_hidden;
}

const char* windows_stat_desc(struct stat* st)
{
	return st->st_desc;
}

void windows_sleep(unsigned seconds)
{
	Sleep(seconds * 1000);
}

#endif

