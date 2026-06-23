// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Andrea Mazzoleni

#include "os/portable.h"

#ifdef __MINGW32__ /* Only for MingW */

#include "os.h"

#include "tommyds/tommylist.h"

#include <userenv.h> /* CreateEnvironmentBlock */

/****************************************************************************/
/* global */

/* Add missing Windows declaration */

/* For SetThreadExecutionState */
#ifndef WIN32_ES_SYSTEM_REQUIRED
#define WIN32_ES_SYSTEM_REQUIRED      0x00000001L
#endif
#ifndef WIN32_ES_DISPLAY_REQUIRED
#define WIN32_ES_DISPLAY_REQUIRED     0x00000002L
#endif
#ifndef WIN32_ES_USER_PRESENT
#define WIN32_ES_USER_PRESENT         0x00000004L
#endif
#ifndef WIN32_ES_AWAYMODE_REQUIRED
#define WIN32_ES_AWAYMODE_REQUIRED    0x00000040L
#endif
#ifndef WIN32_ES_CONTINUOUS
#define WIN32_ES_CONTINUOUS           0x80000000L
#endif

/* File Index */
#ifndef FILE_INVALID_FILE_ID
#define FILE_INVALID_FILE_ID          ((ULONGLONG)-1LL)
#endif

/**
 * Direct access to RtlGenRandom().
 * This function is accessible only with LoadLibrary() and it's available from Windows XP.
 */
static BOOLEAN(WINAPI * ptr_RtlGenRandom)(PVOID, ULONG);

/**
 * Description of the last error.
 * It's stored in the thread local storage.
 */
static windows_key_t last_error;

/**
 * Monotone os_tick counter
 */
static windows_mutex_t tick_lock;
static uint64_t tick_last;

/**
 * If we are running in Wine.
 */
static int is_wine;

/**
 * If we should use the legacy FindFirst/Next way to list directories.
 */
static int is_scan_winfind;

/**
 * Loaded ADVAPI32.DLL.
 */
static HMODULE dll_advapi32;

/**
 * Executable dir.
 *
 * Or empty or terminating with '\'.
 */
static WCHAR exedir[PATH_MAX];

/****************************************************************************/
/* signal */

void os_signal_init(void (*handler_term)(int sig), void (*handler_hup)(int sig))
{
	{
		(void)handler_hup;

		signal(SIGTERM, handler_term);
		signal(SIGINT, handler_term);
	}
}

const char* os_signal_name(int sig)
{
	switch (sig) {
#ifdef SIGHUP
	case SIGHUP : return "SIGHUP";
#endif
#ifdef SIGINT
	case SIGINT : return "SIGINT";
#endif
#ifdef SIGQUIT
	case SIGQUIT : return "SIGQUIT";
#endif
#ifdef SIGILL
	case SIGILL : return "SIGILL";
#endif
#ifdef SIGTRAP
	case SIGTRAP : return "SIGTRAP";
#endif
#ifdef SIGABRT
	case SIGABRT : return "SIGABRT";
#endif
#ifdef SIGBUS
	case SIGBUS : return "SIGBUS";
#endif
#ifdef SIGFPE
	case SIGFPE : return "SIGFPE";
#endif
#ifdef SIGKILL
	case SIGKILL : return "SIGKILL";
#endif
#ifdef SIGUSR1
	case SIGUSR1 : return "SIGUSR1";
#endif
#ifdef SIGSEGV
	case SIGSEGV : return "SIGSEGV";
#endif
#ifdef SIGUSR2
	case SIGUSR2 : return "SIGUSR2";
#endif
#ifdef SIGPIPE
	case SIGPIPE : return "SIGPIPE";
#endif
#ifdef SIGALRM
	case SIGALRM : return "SIGALRM";
#endif
#ifdef SIGTERM
	case SIGTERM : return "SIGTERM";
#endif
	}

	return "UNKNOWN";
}

/****************************************************************************/
/* convert */

wchar_t* u8tou16_mayfail(wchar_t* conv_buf, size_t number_of_wchar, const char* src, size_t number_of_char, size_t* result_length_without_terminator)
{
	/* MB_ERR_INVALID_CHARS forces the API to fail on malformed UTF-8 sequences */
	int ret = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, src, number_of_char, conv_buf, number_of_wchar);
	if (ret <= 0)
		return 0;

	if (result_length_without_terminator)
		*result_length_without_terminator = ret;

	return conv_buf;
}

char* u16tou8_mayfail(char* conv_buf, size_t number_of_char, const wchar_t* src, size_t number_of_wchar, size_t* result_length_without_terminator)
{
	/* WC_ERR_INVALID_CHARS forces the API to fail on malformed UTF-16 sequences */
	int ret = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, src, number_of_wchar, conv_buf, number_of_char, 0, 0);
	if (ret <= 0)
		return 0;

	if (result_length_without_terminator)
		*result_length_without_terminator = ret;

	return conv_buf;
}

static wchar_t* u8tou16_force(wchar_t* conv_buf, size_t number_of_wchar, const char* src, size_t number_of_char, size_t* result_length_without_terminator)
{
	/* flags at 0 forces the API to never fail on malformed UTF-8 sequences */
	int ret = MultiByteToWideChar(CP_UTF8, 0, src, number_of_char, conv_buf, number_of_wchar);
	if (ret <= 0) {
		DWORD error = GetLastError();
		if (error == ERROR_INSUFFICIENT_BUFFER) {
			os_syslog(OS_LVL_CRITICAL, "path too long converting '%s' from UTF-8 to UTF-16", src);
		} else {
			os_syslog(OS_LVL_CRITICAL, "error %u converting '%s' from UTF-8 to UTF-16", (unsigned)error, src);
		}
		os_abort();
	}

	if (result_length_without_terminator)
		*result_length_without_terminator = ret;

	return conv_buf;
}

static char* u16tou8_force(char* conv_buf, size_t number_of_char, const wchar_t* src, size_t number_of_wchar, size_t* result_length_without_terminator)
{
	/* flags at 0 forces the API to never fail on malformed UTF-16 sequences */
	int ret = WideCharToMultiByte(CP_UTF8, 0, src, number_of_wchar, conv_buf, number_of_char, 0, 0);
	if (ret <= 0) {
		DWORD error = GetLastError();
		if (error == ERROR_INSUFFICIENT_BUFFER) {
			os_syslog(OS_LVL_CRITICAL, "path too long converting from UTF-16 to UTF-8 with len %u", (unsigned)number_of_wchar);
		} else {
			os_syslog(OS_LVL_CRITICAL, "error %u converting from UTF-16 to UTF-8 with len %u", (unsigned)error, (unsigned)number_of_wchar);
			if (src != 0) {
				for (size_t i = 0; i < number_of_wchar; ++i) {
					os_syslog(OS_LVL_CRITICAL, "%4u: %04x", (unsigned)i, src[i]);
				}
			}
		}
		os_abort();
	}

	if (result_length_without_terminator)
		*result_length_without_terminator = ret;

	return conv_buf;
}

wchar_t* u8tou16(wchar_t* conv_buf, const char* src)
{
	/* convert also the 0 terminator */
	return u8tou16_force(conv_buf, CONV_MAX, src, strlen(src) + 1, 0);
}

char* u16tou8(char* conv_buf, const wchar_t* src)
{
	/* convert also the 0 terminator */
	return u16tou8_force(conv_buf, CONV_MAX, src, wcslen(src) + 1, 0);
}

/**
 * Check if the char is a forward or back slash.
 */
static int is_slash(char c)
{
	return c == '/' || c == '\\';
}

wchar_t* convert_arg(wchar_t* conv_buf, const char* src, int only_if_required)
{
	int ret;
	wchar_t* dst;
	int count;

	dst = conv_buf;

	/*
	 * Note that we always check for both / and \ because the path is blindly
	 * converted to unix format by path_import()
	 */

	if (only_if_required && strlen(src) < 260 - 12) {
		/*
		 * It's a short path
		 * 260 is the MAX_PATH, note that it includes the space for the terminating NUL
		 * 12 is an additional space for filename, required when creating directory
		 */

		/* do nothing */
	} else if (is_slash(src[0]) && is_slash(src[1]) && (src[2] == '?' || src[2] == '.') && is_slash(src[3])) {
		/* if it's already a '\\?\' or '\\.\' path */

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
	count = dst - conv_buf;

	ret = MultiByteToWideChar(CP_UTF8, 0, src, -1, dst, CONV_MAX - count);
	if (ret <= 0) {
		DWORD error = GetLastError();
		if (error == ERROR_INSUFFICIENT_BUFFER) {
			os_syslog(OS_LVL_CRITICAL, "oath too long converting '%s' from UTF-8 to UTF-16", src);
		} else {
			os_syslog(OS_LVL_CRITICAL, "error %u converting '%s' from UTF-8 to UTF-16", (unsigned)error, src);
		}
		os_abort();
	}

	/*
	 * Convert any / to \
	 * note that in UTF-16, it's not possible to have '/' used as part
	 * of a pair of codes representing a single UNICODE char
	 * See: http://en.wikipedia.org/wiki/UTF-16
	 */
	while (*dst) {
		if (*dst == L'/')
			*dst = L'\\';
		++dst;
	}

	return conv_buf;
}

/****************************************************************************/
/* windows */

const wchar_t* windows_exedir(void)
{
	return exedir;
}

int windows_is_wine(void)
{
	return is_wine;
}

static BOOL GetReparseTagInfoByHandle(HANDLE hFile, FILE_ATTRIBUTE_TAG_INFO* lpFileAttributeTagInfo, DWORD dwFileAttributes)
{
	/* if not a reparse point, return no info */
	if ((dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
		lpFileAttributeTagInfo->FileAttributes = dwFileAttributes;
		lpFileAttributeTagInfo->ReparseTag = 0;
		return TRUE;
	}

	/* do the real call */
	return GetFileInformationByHandleEx(hFile, FileAttributeTagInfo, lpFileAttributeTagInfo, sizeof(FILE_ATTRIBUTE_TAG_INFO));
}

/**
 * Convert Windows attr to the Unix stat format.
 */
static void windows_attr2stat(DWORD FileAttributes, DWORD ReparseTag, struct windows_stat* st)
{
	/* Convert special attributes */
	if ((FileAttributes & FILE_ATTRIBUTE_DEVICE) != 0) {
		st->st_mode = S_IFBLK;
		st->st_desc = "device";
	} else if ((FileAttributes & FILE_ATTRIBUTE_OFFLINE) != 0) { /* Offline */
		st->st_mode = S_IFCHR;
		st->st_desc = "offline";
	} else if ((FileAttributes & FILE_ATTRIBUTE_TEMPORARY) != 0) { /* Temporary */
		st->st_mode = S_IFCHR;
		st->st_desc = "temporary";
	} else if ((FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) { /* Reparse point */
		switch (ReparseTag) {
		/* if we don't have the ReparseTag information */
		case 0 :
			/* don't set the st_mode, to set it later calling lstat_sync() */
			st->st_mode = 0;
			st->st_desc = "unknown";
			break;
		/* for deduplicated files, assume that they are regular ones */
		case IO_REPARSE_TAG_DEDUP :
			if ((FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
				st->st_mode = S_IFDIR;
				st->st_desc = "directory-dedup";
			} else {
				st->st_mode = S_IFREG;
				st->st_desc = "regular-dedup";
			}
			break;
		case IO_REPARSE_TAG_SYMLINK :
			if ((FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
				st->st_mode = S_IFLNKDIR;
				st->st_desc = "reparse-point-symlink-dir";
			} else {
				st->st_mode = S_IFLNK;
				st->st_desc = "reparse-point-symlink-file";
			}
			break;
		/* all the other are skipped as reparse-point */
		case IO_REPARSE_TAG_MOUNT_POINT :
			st->st_mode = S_IFCHR;
			st->st_desc = "reparse-point-mount";
			break;
		case IO_REPARSE_TAG_NFS :
			st->st_mode = S_IFCHR;
			st->st_desc = "reparse-point-nfs";
			break;
		default :
			st->st_mode = S_IFCHR;
			st->st_desc = "reparse-point";
			break;
		}
	} else if ((FileAttributes & FILE_ATTRIBUTE_SYSTEM) != 0) { /* System */
		if ((FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
			st->st_mode = S_IFCHR;
			st->st_desc = "system-directory";
		} else {
			st->st_mode = S_IFREG;
			st->st_desc = "system-file";
		}
	} else {
		if ((FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
			st->st_mode = S_IFDIR;
			st->st_desc = "directory";
		} else {
			st->st_mode = S_IFREG;
			st->st_desc = "regular";
		}
	}

	/* store the HIDDEN attribute in a separate field */
	st->st_hidden = (FileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0;
}

/**
 * Convert Windows info to the Unix stat format.
 */
static int windows_info2stat(const BY_HANDLE_FILE_INFORMATION* info, const FILE_ATTRIBUTE_TAG_INFO* tag, struct windows_stat* st)
{
	int64_t mtime;

	windows_attr2stat(info->dwFileAttributes, tag->ReparseTag, st);

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

	/* GetFileInformationByHandle() ensures to return synced information */
	st->st_sync = 1;

	/**
	 * In ReFS the IDs are 128 bit, and the 64 bit interface may fail.
	 *
	 * From Microsoft "Application Compatibility with ReFS"
	 * http://download.microsoft.com/download/C/B/3/CB3561DC-6BF6-443D-B5B9-9676ACDF7F75/Application%20Compatibility%20with%20ReFS.docx
	 * "64-bit file identifier can be obtained from GetFileInformationByHandle in"
	 * "the nFileIndexHigh and nFileIndexLow members. This API is an extended version"
	 * "that includes 128-bit file identifiers.  If GetFileInformationByHandle returns"
	 * "FILE_INVALID_FILE_ID, the identifier may only be described in 128 bit form."
	 */
	if (st->st_ino == (uint64_t)FILE_INVALID_FILE_ID) {
		errno = EINVAL;
		os_syslog(OS_LVL_INFO, "invalid inode number! Is this ReFS?");
		return -1;
	}

	return 0;
}

/**
 * Convert Windows info to the Unix stat format.
 */
static int windows_stream2stat(const BY_HANDLE_FILE_INFORMATION* info, const FILE_ID_BOTH_DIR_INFO* stream, struct windows_stat* st)
{
	int64_t mtime;

	/*
	 * The FILE_ID_BOTH_DIR_INFO doesn't have the ReparseTag information
	 * we could use instead FILE_ID_EXTD_DIR_INFO, but it's available only
	 * from Windows Server 2012
	 */
	windows_attr2stat(stream->FileAttributes, 0, st);

	st->st_size = stream->EndOfFile.QuadPart;

	mtime = stream->LastWriteTime.QuadPart;

	/*
	 * Convert to unix time
	 *
	 * How To Convert a UNIX time_t to a Win32 FILETIME or SYSTEMTIME
	 * http://support.microsoft.com/kb/167296
	 */
	mtime -= 116444736000000000LL;
	st->st_mtime = mtime / 10000000;
	st->st_mtimensec = (mtime % 10000000) * 100;

	st->st_ino = stream->FileId.QuadPart;

	st->st_nlink = info->nNumberOfLinks;

	st->st_dev = info->dwVolumeSerialNumber;

	/* directory listing doesn't ensure to return synced information */
	st->st_sync = 0;

	/* in ReFS the IDs are 128 bit, and the 64 bit interface may fail */
	if (st->st_ino == (uint64_t)FILE_INVALID_FILE_ID) {
		errno = EINVAL;
		os_syslog(OS_LVL_INFO, "Invalid inode number! Is this ReFS?");
		return -1;
	}

	return 0;
}

/**
 * Convert Windows findfirst info to the Unix stat format.
 */
static void windows_finddata2stat(const WIN32_FIND_DATAW* info, struct windows_stat* st)
{
	int64_t mtime;

	windows_attr2stat(info->dwFileAttributes, info->dwReserved0, st);

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

	/* directory listing doesn't ensure to return synced information */
	st->st_sync = 0;
}

static void windows_finddata2dirent(const WIN32_FIND_DATAW* info, struct windows_dirent* dirent)
{
	char conv_buf[CONV_MAX];
	const char* name;
	size_t len;

	name = u16tou8_force(conv_buf, CONV_MAX, info->cFileName, wcslen(info->cFileName), &len);

	if (len + 1 >= sizeof(dirent->d_name)) {
		os_syslog(OS_LVL_CRITICAL, "name too long");
		os_exit();
	}

	memcpy(dirent->d_name, name, len);
	dirent->d_name[len] = 0;

	windows_finddata2stat(info, &dirent->d_stat);
}

static int windows_stream2dirent(const BY_HANDLE_FILE_INFORMATION* info, const FILE_ID_BOTH_DIR_INFO* stream, struct windows_dirent* dirent)
{
	char conv_buf[CONV_MAX];
	const char* name;
	size_t len;

	name = u16tou8_force(conv_buf, CONV_MAX, stream->FileName, stream->FileNameLength / 2, &len);

	if (len + 1 >= sizeof(dirent->d_name)) {
		os_syslog(OS_LVL_CRITICAL, "name too long");
		os_exit();
	}

	memcpy(dirent->d_name, name, len);
	dirent->d_name[len] = 0;

	return windows_stream2stat(info, stream, &dirent->d_stat);
}

/**
 * Convert Windows error to errno.
 */
void windows_errno(DWORD error)
{
	switch (error) {
	case ERROR_INVALID_HANDLE :
		/*
		 * We check for a bad handle calling _get_osfhandle()
		 * and in such case we return EBADF
		 * Other cases are here identified with EINVAL
		 */
		errno = EINVAL;
		break;
	case ERROR_HANDLE_EOF : /* in ReadFile() over the end of the file */
		errno = EINVAL;
		break;
	case ERROR_FILE_NOT_FOUND :
	case ERROR_PATH_NOT_FOUND : /* in GetFileAttributeW() if internal path not found */
		errno = ENOENT;
		break;
	case ERROR_ACCESS_DENIED : /* in CreateDirectoryW() if dir is scheduled for deletion */
	case ERROR_CURRENT_DIRECTORY : /* in RemoveDirectoryW() if removing the current directory */
	case ERROR_SHARING_VIOLATION : /* in RemoveDirectoryW() if in use */
	case ERROR_WRITE_PROTECT : /* when dealing with read-only media/snapshot and trying to write to them */
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
	case ERROR_PRIVILEGE_NOT_HELD : /* in CreateSymlinkW() if no SeCreateSymbolicLinkPrivilige permission */
		errno = EPERM;
		break;
	case ERROR_IO_DEVICE : /* in ReadFile() and WriteFile() */
	case ERROR_CRC : /* in ReadFile() */
		errno = EIO;
		break;
	case WSAEADDRINUSE :
		errno = EADDRINUSE;
		break;
	default :
		errno = ENXIO;
		os_syslog(OS_LVL_INFO, "unexpected Windows error %lu", error);
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

	if (!GetFileInformationByHandle(h, &info)) {
		windows_errno(GetLastError());
		return -1;
	}

	if (!GetReparseTagInfoByHandle(h, &tag, info.dwFileAttributes)) {
		windows_errno(GetLastError());
		return -1;
	}

	return windows_info2stat(&info, &tag, st);
}

int windows_get_file_attributes(const char* file)
{
	wchar_t conv_buf[CONV_MAX];

	DWORD ret = GetFileAttributesW(convert(conv_buf, file));
	if (ret == INVALID_FILE_ATTRIBUTES) {
		windows_errno(GetLastError());
		return -1;
	}

	return ret;
}

int windows_set_file_attributes(const char* file, int attributes)
{
	wchar_t conv_buf[CONV_MAX];

	if (!SetFileAttributesW(convert(conv_buf, file), attributes)) {
		windows_errno(GetLastError());
		return -1;
	}

	return 0;
}

int windows_lstat(const char* file, struct windows_stat* st)
{
	wchar_t conv_buf[CONV_MAX];
	HANDLE h;
	WIN32_FIND_DATAW data;

	/* FindFirstFileW by default gets information of symbolic links and not of their targets */
	h = FindFirstFileW(convert(conv_buf, file), &data);
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
	memcpy(st, &dd->d_stat, sizeof(struct windows_stat));
}

int windows_mkdir(const char* file)
{
	wchar_t conv_buf[CONV_MAX];

	if (!CreateDirectoryW(convert(conv_buf, file), 0)) {
		windows_errno(GetLastError());
		return -1;
	}

	return 0;
}

int windows_rmdir(const char* file)
{
	wchar_t conv_buf[CONV_MAX];

	if (!RemoveDirectoryW(convert(conv_buf, file))) {
		windows_errno(GetLastError());
		return -1;
	}

	return 0;
}

int windows_stat(const char* file, struct windows_stat* st)
{
	wchar_t conv_buf[CONV_MAX];
	BY_HANDLE_FILE_INFORMATION info;
	FILE_ATTRIBUTE_TAG_INFO tag;
	HANDLE h;

	/*
	 * Open the handle of the file.
	 *
	 * Use FILE_FLAG_BACKUP_SEMANTICS to open directories (it's just ignored for files).
	 */
	h = CreateFileW(convert(conv_buf, file), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, 0);
	if (h == INVALID_HANDLE_VALUE) {
		windows_errno(GetLastError());
		return -1;
	}

	if (!GetFileInformationByHandle(h, &info)) {
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

	return windows_info2stat(&info, &tag, st);
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

	/*
	 * Windows effectively reserves space, but it doesn't initialize it.
	 * It's then important to write starting from the begin to the end,
	 * to avoid to have Windows to fill the holes writing zeros.
	 *
	 * See:
	 * "Why does my single-byte write take forever?"
	 * http://blogs.msdn.com/b/oldnewthing/archive/2011/09/22/10215053.aspx
	 */
	if (!SetEndOfFile(h)) {
		windows_errno(GetLastError());
		return -1;
	}

	return 0;
}

int windows_fallocate(int fd, int mode, off64_t off, off64_t len)
{
	if (mode != 0)
		return -1;

	/* no difference with ftruncate because Windows doesn't use sparse files */
	return windows_ftruncate(fd, off + len);
}

int windows_fsync(int fd)
{
	HANDLE h;

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
	 * "The FlushFileBuffers API can be used to flush all the outstanding data
	 * and metadata on a single file or a whole volume. However, frequent use
	 * of this API can cause reduced throughput. Internally, Windows uses the
	 * SCSI Synchronize Cache or the IDE/ATAPI Flush cache commands."
	 *
	 * From:
	 * "Windows Write Caching - Part 2 An overview for Application Developers"
	 * http://winntfs.com/2012/11/29/windows-write-caching-part-2-an-overview-for-application-developers/
	 */
	if (!FlushFileBuffers(h)) {
		DWORD error = GetLastError();

		switch (error) {
		case ERROR_INVALID_HANDLE :
			/*
			 * FlushFileBuffers returns this error if the handle
			 * doesn't support buffering, like the console output.
			 *
			 * We had a report that also ATA-over-Ethernet returns
			 * this error, but not enough sure to ignore it.
			 * So, we use now an extended error reporting.
			 */
			windows_errno(error);
			os_syslog(OS_LVL_INFO, "unexpected Windows INVALID_HANDLE error in FlushFileBuffers()");
			os_syslog(OS_LVL_INFO, "are you using ATA-over-Ethernet? Please report it.");
			return -1;

		case ERROR_ACCESS_DENIED :
			/*
			 * FlushFileBuffers returns this error for read-only
			 * data, that cannot have to be flushed.
			 */
			return 0;

		default :
			windows_errno(error);
			return -1;
		}
	}

	return 0;
}

static int windows_vsync(const wchar_t* volume)
{
	HANDLE h;
	DWORD bytes;

	/* open the volume (volumeName already in \\?\Volume{GUID} format) */
	h = CreateFileW(volume, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	if (h == INVALID_HANDLE_VALUE) {
		windows_errno(GetLastError());
		return -1;
	}

	/*
	 * "The FlushFileBuffers API can be used to flush all the outstanding data
	 * and metadata on a single file or a whole volume. However, frequent use
	 * of this API can cause reduced throughput. Internally, Windows uses the
	 * SCSI Synchronize Cache or the IDE/ATAPI Flush cache commands."
	 *
	 * From:
	 * "Windows Write Caching - Part 2 An overview for Application Developers"
	 * http://winntfs.com/2012/11/29/windows-write-caching-part-2-an-overview-for-application-developers/
	 */
	if (!FlushFileBuffers(h)) {
		DWORD error = GetLastError();
		CloseHandle(h);

		switch (error) {
		case ERROR_INVALID_HANDLE :
			/*
			 * FlushFileBuffers returns this error if the handle
			 * doesn't support buffering, like the console output.
			 *
			 * We had a report that also ATA-over-Ethernet returns
			 * this error, but not enough sure to ignore it.
			 * So, we use now an extended error reporting.
			 */
			windows_errno(error);
			os_syslog(OS_LVL_INFO, "unexpected Windows INVALID_HANDLE error in FlushFileBuffers()");
			os_syslog(OS_LVL_INFO, "are you using ATA-over-Ethernet? Please report it.");
			return -1;

		case ERROR_ACCESS_DENIED :
			/*
			 * FlushFileBuffers returns this error for read-only
			 * data, that cannot have to be flushed.
			 */
			return 0;

		default :
			windows_errno(error);
			return -1;
		}
	}

	/* lock the volume (may fail if volume is in use, which is fine) */
	if (!DeviceIoControl(h, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &bytes, NULL)) {
		DWORD error = GetLastError();
		CloseHandle(h);

		switch (error) {
		case ERROR_ACCESS_DENIED :
			/*
			 * ERROR_ACCESS_DENIED (5) means volume is in use - this is expected and OK
			 */
			return 0;

		default :
			windows_errno(error);
			return -1;
		}
	}

	/* unlock immediately - the lock/unlock cycle helps ensure consistency */
	if (!DeviceIoControl(h, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, &bytes, NULL)) {
		DWORD error = GetLastError();
		CloseHandle(h);
		windows_errno(error);
		return -1;
	}

	if (!CloseHandle(h)) {
		windows_errno(GetLastError());
		return -1;
	}

	return 0;
}

static int windows_async(void)
{
	HANDLE h;
	wchar_t volume[PATH_MAX];
	DWORD error = 0;
	DWORD count = 0;
	DWORD success = 0;

	h = FindFirstVolumeW(volume, sizeof(volume) / sizeof(wchar_t));
	if (h == INVALID_HANDLE_VALUE) {
		windows_errno(GetLastError());
		return -1;
	}

	do {
		/* remove trailing backslash */
		size_t len = wcslen(volume);
		if (len > 0 && volume[len - 1] == L'\\')
			volume[len - 1] = L'\0';

		++count;

		if (windows_vsync(volume) == 0) {
			++success;
		} else {
			/* save the first error */
			if (error == 0)
				error = GetLastError();
		}
	} while (FindNextVolumeW(h, volume, sizeof(volume) / sizeof(wchar_t)));

	/* if at least one failed */
	if (count != success) {
		FindVolumeClose(h);
		windows_errno(error);
		return -1;
	}

	error = GetLastError();
	if (error != ERROR_NO_MORE_FILES) {
		FindVolumeClose(h);
		windows_errno(error);
		return -1;
	}

	if (!FindVolumeClose(h)) {
		windows_errno(GetLastError());
		return -1;
	}

	return 0;
}

int windows_sync(void)
{
	if (windows_async() != 0)
		return -1;

	return 0;
}

int windows_futimens(int fd, struct windows_timespec tv[2])
{
	HANDLE h;
	FILETIME ft;
	int64_t mtime;

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
	mtime += 116444736000000000LL;

	ft.dwHighDateTime = mtime >> 32;
	ft.dwLowDateTime = mtime;

	if (!SetFileTime(h, 0, 0, &ft)) {
		windows_errno(GetLastError());
		return -1;
	}

	return 0;
}

int windows_utimensat(int fd, const char* file, struct windows_timespec tv[2], int flags)
{
	wchar_t conv_buf[CONV_MAX];
	HANDLE h;
	FILETIME ft;
	int64_t mtime;
	DWORD wflags;

	/*
	 * Support only the absolute paths
	 */
	if (fd != AT_FDCWD) {
		errno = EBADF;
		return -1;
	}

	/*
	 * Open the handle of the file.
	 *
	 * Use FILE_FLAG_BACKUP_SEMANTICS to open directories (it's just ignored for files).
	 * Use FILE_FLAG_OPEN_REPARSE_POINT to open symbolic links and not the their target.
	 *
	 * Note that even with FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
	 * and FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT some paths
	 * cannot be opened like "C:\System Volume Information" resulting
	 * in error ERROR_ACCESS_DENIED.
	 */
	wflags = FILE_FLAG_BACKUP_SEMANTICS;
	if ((flags & AT_SYMLINK_NOFOLLOW) != 0)
		wflags |= FILE_FLAG_OPEN_REPARSE_POINT;
	h = CreateFileW(convert(conv_buf, file), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0, OPEN_EXISTING, wflags, 0);
	if (h == INVALID_HANDLE_VALUE) {
		windows_errno(GetLastError());
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
	mtime += 116444736000000000LL;

	ft.dwHighDateTime = mtime >> 32;
	ft.dwLowDateTime = mtime;

	if (!SetFileTime(h, 0, 0, &ft)) {
		windows_errno(GetLastError());
		CloseHandle(h);
		return -1;
	}

	if (!CloseHandle(h)) {
		windows_errno(GetLastError());
		return -1;
	}

	return 0;
}

int windows_rename(const char* from, const char* to)
{
	wchar_t conv_buf_from[CONV_MAX];
	wchar_t conv_buf_to[CONV_MAX];

	/*
	 * Implements an atomic rename in Windows.
	 * Not really atomic at now to support XP.
	 *
	 * Is an atomic file rename (with overwrite) possible on Windows?
	 * http://stackoverflow.com/questions/167414/is-an-atomic-file-rename-with-overwrite-possible-on-windows
	 */
	if (!MoveFileExW(convert(conv_buf_from, from), convert(conv_buf_to, to), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
		windows_errno(GetLastError());
		return -1;
	}

	return 0;
}

int windows_remove(const char* file)
{
	wchar_t conv_buf[CONV_MAX];

	if (!DeleteFileW(convert(conv_buf, file))) {
		windows_errno(GetLastError());
		return -1;
	}

	return 0;
}

FILE* windows_fopen(const char* file, const char* mode)
{
	wchar_t conv_buf_file[CONV_MAX];
	wchar_t conv_buf_mode[CONV_MAX];

	return _wfopen(convert(conv_buf_file, file), u8tou16(conv_buf_mode, mode));
}

int windows_open(const char* file, int flags, ...)
{
	wchar_t conv_buf[CONV_MAX];
	HANDLE h;
	int f;
	DWORD access;
	DWORD share;
	DWORD create;
	DWORD attr;

	switch (flags & O_ACCMODE) {
	case O_RDONLY :
		access = GENERIC_READ;
		break;
	case O_WRONLY :
		access = GENERIC_WRITE;
		break;
	case O_RDWR :
		access = GENERIC_READ | GENERIC_WRITE;
		break;
	default :
		errno = EINVAL;
		return -1;
	}

	share = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;

	switch (flags & (O_CREAT | O_EXCL | O_TRUNC)) {
	case 0 :
		create = OPEN_EXISTING;
		break;
	case O_CREAT :
		create = OPEN_ALWAYS;
		break;
	case O_CREAT | O_EXCL :
	case O_CREAT | O_EXCL | O_TRUNC :
		create = CREATE_NEW;
		break;
	case O_CREAT | O_TRUNC :
		create = CREATE_ALWAYS;
		break;
	case O_TRUNC :
		create = TRUNCATE_EXISTING;
		break;
	default :
		errno = EINVAL;
		return -1;
	}

	attr = FILE_ATTRIBUTE_NORMAL;
	if ((flags & O_DIRECT) != 0)
		attr |= FILE_FLAG_NO_BUFFERING;
	if ((flags & O_DSYNC) != 0)
		attr |= FILE_FLAG_WRITE_THROUGH;
	if ((flags & O_RANDOM) != 0)
		attr |= FILE_FLAG_RANDOM_ACCESS;
	if ((flags & O_SEQUENTIAL) != 0)
		attr |= FILE_FLAG_SEQUENTIAL_SCAN;
	if ((flags & _O_SHORT_LIVED) != 0)
		attr |= FILE_ATTRIBUTE_TEMPORARY;
	if ((flags & O_TEMPORARY) != 0)
		attr |= FILE_FLAG_DELETE_ON_CLOSE;

	h = CreateFileW(convert(conv_buf, file), access, share, 0, create, attr, 0);
	if (h == INVALID_HANDLE_VALUE) {
		windows_errno(GetLastError());
		return -1;
	}

	/* mask out flags unknown by Windows */
	flags &= ~(O_DIRECT | O_DSYNC);

	f = _open_osfhandle((intptr_t)h, flags);
	if (f == -1) {
		CloseHandle(h);
		return -1;
	}

	return f;
}

struct windows_dir_struct {
	BY_HANDLE_FILE_INFORMATION info;
	WIN32_FIND_DATAW find;
	HANDLE h;
	struct windows_dirent entry;
	unsigned char* buffer;
	unsigned buffer_size;
	unsigned buffer_pos;
	int state;
};

#define DIR_STATE_EOF -1 /**< End of the dir stream */
#define DIR_STATE_EMPTY 0 /**< The entry is empty. */
#define DIR_STATE_FILLED 1 /**< The entry is valid. */

static windows_dir* windows_opendir_find(const char* dir)
{
	wchar_t conv_buf[CONV_MAX];
	wchar_t* wdir;
	windows_dir* dirstream;
	size_t len;

	dirstream = malloc(sizeof(windows_dir));
	if (!dirstream) {
		errno = ENOMEM;
		return 0;
	}

	wdir = convert(conv_buf, dir);

	/* add final / and * */
	len = wcslen(wdir);
	if (len != 0 && wdir[len - 1] != '\\')
		wdir[len++] = L'\\';
	wdir[len++] = L'*';
	wdir[len++] = 0;

	dirstream->h = FindFirstFileW(wdir, &dirstream->find);
	if (dirstream->h == INVALID_HANDLE_VALUE) {
		DWORD error = GetLastError();

		if (error == ERROR_FILE_NOT_FOUND) {
			dirstream->state = DIR_STATE_EOF;
			return dirstream;
		}

		free(dirstream);
		windows_errno(error);
		return 0;
	}

	windows_finddata2dirent(&dirstream->find, &dirstream->entry);
	dirstream->state = DIR_STATE_FILLED;

	return dirstream;
}

static struct windows_dirent* windows_readdir_find(windows_dir* dirstream)
{
	if (dirstream->state == DIR_STATE_EMPTY) {
		if (!FindNextFileW(dirstream->h, &dirstream->find)) {
			DWORD error = GetLastError();

			if (error != ERROR_NO_MORE_FILES) {
				windows_errno(error);
				return 0;
			}

			dirstream->state = DIR_STATE_EOF;
		} else {
			windows_finddata2dirent(&dirstream->find, &dirstream->entry);
			dirstream->state = DIR_STATE_FILLED;
		}
	}

	if (dirstream->state == DIR_STATE_FILLED) {
		dirstream->state = DIR_STATE_EMPTY;
		return &dirstream->entry;
	}

	/* otherwise it's the end of stream */
	assert(dirstream->state == DIR_STATE_EOF);
	errno = 0;

	return 0;
}

static int windows_closedir_find(windows_dir* dirstream)
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

static int windows_first_stream(windows_dir* dirstream)
{
	FILE_ID_BOTH_DIR_INFO* fd;

	if (!GetFileInformationByHandleEx(dirstream->h, FileIdBothDirectoryInfo, dirstream->buffer, dirstream->buffer_size)) {
		DWORD error = GetLastError();

		if (error == ERROR_NO_MORE_FILES) {
			dirstream->state = DIR_STATE_EOF;
			return 0;
		}

		windows_errno(error);
		return -1;
	}

	/* get the first entry */
	dirstream->state = DIR_STATE_FILLED;
	dirstream->buffer_pos = 0;
	fd = (FILE_ID_BOTH_DIR_INFO*)dirstream->buffer;
	return windows_stream2dirent(&dirstream->info, fd, &dirstream->entry);
}

static int windows_next_stream(windows_dir* dirstream)
{
	FILE_ID_BOTH_DIR_INFO* fd;

	/* last entry read */
	fd = (FILE_ID_BOTH_DIR_INFO*)(dirstream->buffer + dirstream->buffer_pos);

	/* check if there is a next one */
	if (fd->NextEntryOffset == 0) {
		/* if not, fill it up again */
		if (windows_first_stream(dirstream) != 0)
			return -1;
		return 0;
	}

	/* go to the next one */
	dirstream->state = DIR_STATE_FILLED;
	dirstream->buffer_pos += fd->NextEntryOffset;
	fd = (FILE_ID_BOTH_DIR_INFO*)(dirstream->buffer + dirstream->buffer_pos);
	return windows_stream2dirent(&dirstream->info, fd, &dirstream->entry);
}

static windows_dir* windows_opendir_stream(const char* dir)
{
	wchar_t conv_buf[CONV_MAX];
	windows_dir* dirstream;

	dirstream = malloc(sizeof(windows_dir));
	if (!dirstream) {
		errno = ENOMEM;
		return 0;
	}

	/* uses a 64 kB buffer for reading directory */
	dirstream->buffer_size = 64 * 1024;
	dirstream->buffer = malloc(dirstream->buffer_size);
	if (!dirstream->buffer) {
		free(dirstream);
		errno = ENOMEM;
		return 0;
	}

	dirstream->h = CreateFileW(convert(conv_buf, dir), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, 0);
	if (dirstream->h == INVALID_HANDLE_VALUE) {
		DWORD error = GetLastError();
		free(dirstream->buffer);
		free(dirstream);
		windows_errno(error);
		return 0;
	}

	/*
	 * Get dir information for the VolumeSerialNumber
	 * this value is used for all the files in the dir
	 */
	if (!GetFileInformationByHandle(dirstream->h, &dirstream->info)) {
		DWORD error = GetLastError();
		CloseHandle(dirstream->h);
		free(dirstream->buffer);
		free(dirstream);
		windows_errno(error);
		return 0;
	}

	if (windows_first_stream(dirstream) != 0) {
		CloseHandle(dirstream->h);
		free(dirstream->buffer);
		free(dirstream);
		return 0;
	}

	return dirstream;
}

static struct windows_dirent* windows_readdir_stream(windows_dir* dirstream)
{
	if (dirstream->state == DIR_STATE_EMPTY) {
		if (windows_next_stream(dirstream) != 0)
			return 0;
	}

	if (dirstream->state == DIR_STATE_FILLED) {
		dirstream->state = DIR_STATE_EMPTY;
		return &dirstream->entry;
	}

	/* otherwise it's the end of stream */
	assert(dirstream->state == DIR_STATE_EOF);
	errno = 0;

	return 0;
}

static int windows_closedir_stream(windows_dir* dirstream)
{
	if (dirstream->h != INVALID_HANDLE_VALUE) {
		if (!CloseHandle(dirstream->h)) {
			DWORD error = GetLastError();

			free(dirstream->buffer);
			free(dirstream);

			windows_errno(error);
			return -1;
		}
	}

	free(dirstream->buffer);
	free(dirstream);

	return 0;
}

windows_dir* windows_opendir(const char* dir)
{
	if (!is_scan_winfind)
		return windows_opendir_stream(dir);
	else
		return windows_opendir_find(dir);
}

struct windows_dirent* windows_readdir(windows_dir* dirstream)
{
	if (!is_scan_winfind)
		return windows_readdir_stream(dirstream);
	else
		return windows_readdir_find(dirstream);
}

int windows_closedir(windows_dir* dirstream)
{
	if (!is_scan_winfind)
		return windows_closedir_stream(dirstream);
	else
		return windows_closedir_find(dirstream);
}

int windows_dirent_hidden(struct dirent* dd)
{
	return dd->d_stat.st_hidden;
}

const char* windows_stat_desc(struct stat* st)
{
	return st->st_desc;
}

unsigned windows_sleep(unsigned seconds)
{
	while (seconds) {
		if (os_signal_interrupt())         /* SIGINT should stop the sleep */
			break;
		Sleep(1000);
		--seconds;
	}

	return seconds;
}

void windows_usleep(uint64_t useconds)
{
	Sleep(useconds / 1000);
}

int windows_link(const char* existing, const char* file)
{
	wchar_t conv_buf_file[CONV_MAX];
	wchar_t conv_buf_existing[CONV_MAX];

	if (!CreateHardLinkW(convert(conv_buf_file, file), convert(conv_buf_existing, existing), 0)) {
		windows_errno(GetLastError());
		return -1;
	}

	return 0;
}

/**
 * In Windows 10 allow creation of symlink by not privileged user.
 *
 * See: Symlinks in Windows 10!
 * https://blogs.windows.com/buildingapps/2016/12/02/symlinks-windows-10/#cQG7cx48oGH86lkI.97
 * "Specify this flag to allow creation of symbolic links when the process is not elevated"
 */
#ifndef SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
#define SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE 0x2
#endif

static int windows_symlink_flags(const char* existing, const char* file, DWORD flags)
{
	wchar_t conv_buf_file[CONV_MAX];
	wchar_t conv_buf_existing[CONV_MAX];

	/*
	 * We must convert to the extended-length \\?\ format if the path is too long
	 * otherwise the link creation fails.
	 * But we don't want to always convert it, to avoid to recreate
	 * user symlinks different than they were before
	 */
	if (!CreateSymbolicLinkW(convert(conv_buf_file, file), convert_if_required(conv_buf_existing, existing),
		flags | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)
	) {
		DWORD error = GetLastError();
		if (GetLastError() != ERROR_INVALID_PARAMETER) {
			windows_errno(error);
			return -1;
		}

		/* retry without the new flag SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE */
		if (!CreateSymbolicLinkW(convert(conv_buf_file, file), convert_if_required(conv_buf_existing, existing), flags)) {
			windows_errno(GetLastError());
			return -1;
		}
	}

	return 0;
}

int windows_symlink(const char* existing, const char* file)
{
	return windows_symlink_flags(existing, file, 0);
}

int windows_symlink_directory(const char* existing, const char* file)
{
	return windows_symlink_flags(existing, file, SYMBOLIC_LINK_FLAG_DIRECTORY);
}

/* Adds missing definitions in MingW winnt.h */
#ifndef FSCTL_GET_REPARSE_POINT
#define FSCTL_GET_REPARSE_POINT 0x000900a8
#endif
#ifndef REPARSE_DATA_BUFFER_HEADER_SIZE
typedef struct _REPARSE_DATA_BUFFER {
	DWORD ReparseTag;
	WORD ReparseDataLength;
	WORD Reserved;
	_ANONYMOUS_UNION union {
		struct {
			WORD SubstituteNameOffset;
			WORD SubstituteNameLength;
			WORD PrintNameOffset;
			WORD PrintNameLength;
			ULONG Flags;
			WCHAR PathBuffer[1];
		} SymbolicLinkReparseBuffer;
		struct {
			WORD SubstituteNameOffset;
			WORD SubstituteNameLength;
			WORD PrintNameOffset;
			WORD PrintNameLength;
			WCHAR PathBuffer[1];
		} MountPointReparseBuffer;
		struct {
			BYTE DataBuffer[1];
		} GenericReparseBuffer;
	} DUMMYUNIONNAME;
} REPARSE_DATA_BUFFER, *PREPARSE_DATA_BUFFER;
#endif

int windows_readlink(const char* file, char* buffer, size_t size)
{
	wchar_t conv_buf_file[CONV_MAX];
	char conv_buf_name[CONV_MAX];
	HANDLE h;
	const char* name;
	size_t len;
	unsigned char rdb_buffer[MAXIMUM_REPARSE_DATA_BUFFER_SIZE];
	REPARSE_DATA_BUFFER* rdb = (REPARSE_DATA_BUFFER*)rdb_buffer;
	BOOL ret;
	DWORD n;

	/*
	 * Open the handle of the file.
	 *
	 * Use FILE_FLAG_BACKUP_SEMANTICS to open directories (it's just ignored for files).
	 * Use FILE_FLAG_OPEN_REPARSE_POINT to open symbolic links and not the their target.
	 */
	h = CreateFileW(convert(conv_buf_file, file), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, 0);
	if (h == INVALID_HANDLE_VALUE) {
		windows_errno(GetLastError());
		return -1;
	}

	/* read the reparse point */
	ret = DeviceIoControl(h, FSCTL_GET_REPARSE_POINT, 0, 0, rdb_buffer, sizeof(rdb_buffer), &n, 0);
	if (!ret) {
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
	name = u16tou8_force(conv_buf_name, CONV_MAX,
			rdb->SymbolicLinkReparseBuffer.PathBuffer + rdb->SymbolicLinkReparseBuffer.PrintNameOffset,
			rdb->SymbolicLinkReparseBuffer.PrintNameLength / 2, &len);

	/* check for overflow */
	if (len > size) {
		len = size;
	}

	memcpy(buffer, name, len);

	/* convert any \ to / */
	for (size_t i = 0; i < len; ++i) {
		if (buffer[i] == '\\')
			buffer[i] = '/';
	}

	return len;
}

/* ensure to call the real C strerror() */
#undef strerror

const char* windows_strerror(int err)
{
	/* get the normal C error from the specified err */
	char* error;
	char* previous;
	const char* str = strerror(err);
	size_t len = strlen(str);

	/* adds space for GetLastError() */
	len += 32;

	/* allocate a new one */
	error = malloc(len);
	if (!error)
		return str;
	snprintf(error, len, "%s [%d/%u]", str, err, (unsigned)GetLastError());

	/* get previous one, if any */
	previous = windows_getspecific(last_error);

	/* store in the thread local storage */
	if (windows_setspecific(last_error, error) != 0) {
		free(error);
		return str;
	}

	free(previous);
	return error;
}

/* restore the define used later */
#define  strerror windows_strerror

ssize_t windows_read(int fd, void* buffer, size_t size)
{
	HANDLE h;
	DWORD count;

	if (fd == -1) {
		errno = EBADF;
		return -1;
	}

	h = (HANDLE)_get_osfhandle(fd);
	if (h == INVALID_HANDLE_VALUE) {
		errno = EBADF;
		return -1;
	}

	if (!ReadFile(h, buffer, size, &count, 0)) {
		windows_errno(GetLastError());
		return -1;
	}

	return count;
}

ssize_t windows_write(int fd, const void* buffer, size_t size)
{
	HANDLE h;
	DWORD count;

	if (fd == -1) {
		errno = EBADF;
		return -1;
	}

	h = (HANDLE)_get_osfhandle(fd);
	if (h == INVALID_HANDLE_VALUE) {
		errno = EBADF;
		return -1;
	}

	if (!WriteFile(h, buffer, size, &count, 0)) {
		windows_errno(GetLastError());
		return -1;
	}

	return count;
}

off_t windows_lseek(int fd, off_t offset, int whence)
{
	HANDLE h;
	LARGE_INTEGER pos;
	LARGE_INTEGER ret;

	if (fd == -1) {
		errno = EBADF;
		return -1;
	}

	/* we support only SEEK_SET */
	if (whence != SEEK_SET) {
		errno = EINVAL;
		return -1;
	}

	h = (HANDLE)_get_osfhandle(fd);
	if (h == INVALID_HANDLE_VALUE) {
		errno = EBADF;
		return -1;
	}

	pos.QuadPart = offset;
	if (!SetFilePointerEx(h, pos, &ret, FILE_BEGIN)) {
		windows_errno(GetLastError());
		return -1;
	}

	return ret.QuadPart;
}

ssize_t windows_pread(int fd, void* buffer, size_t size, off_t offset)
{
	HANDLE h;
	OVERLAPPED overlapped;
	DWORD count;

	if (fd == -1) {
		errno = EBADF;
		return -1;
	}

	h = (HANDLE)_get_osfhandle(fd);
	if (h == INVALID_HANDLE_VALUE) {
		errno = EBADF;
		return -1;
	}

retry:
	memset(&overlapped, 0, sizeof(overlapped));
	overlapped.Offset = offset & 0xFFFFFFFF;
	overlapped.OffsetHigh = offset >> 32;

	if (!ReadFile(h, buffer, size, &count, &overlapped)) {
		DWORD err = GetLastError();
		/*
		 * If Windows is not able to allocate memory from the PagedPool for the disk cache
		 * it could return the ERROR_NO_SYSTEM_RESOURCES error.
		 * In this case, the only possibility is to retry after a wait of few milliseconds.
		 *
		 * SQL Server reports operating system error 1450 or 1452 or 665 (retries)
		 * http://blogs.msdn.com/b/psssql/archive/2008/07/10/sql-server-reports-operating-system-error-1450-or-1452-or-665-retries.aspx
		 *
		 * 03-12-09 - ERROR_NO_SYSTEM_RESOURCES
		 * http://cbloomrants.blogspot.it/2009/03/03-12-09-errornosystemresources.html
		 *
		 * From SnapRAID Discussion Forum:
		 *
		 * Error reading file
		 * https://sourceforge.net/p/snapraid/discussion/1677233/thread/6657fdbf/
		 *
		 * Unexpected Windows ERROR_NO_SYSTEM_RESOURCES in pwrite(), retrying...
		 * https://sourceforge.net/p/snapraid/discussion/1677233/thread/a7c25ba9/
		 */
		if (err == ERROR_NO_SYSTEM_RESOURCES) {
			os_syslog(OS_LVL_ERROR, "unexpected Windows ERROR_NO_SYSTEM_RESOURCES in pread(), retrying...");
			Sleep(50);
			goto retry;
		}

		windows_errno(err);
		return -1;
	}

	return count;
}

ssize_t windows_pwrite(int fd, const void* buffer, size_t size, off_t offset)
{
	HANDLE h;
	OVERLAPPED overlapped;
	DWORD count;

	if (fd == -1) {
		errno = EBADF;
		return -1;
	}

	h = (HANDLE)_get_osfhandle(fd);
	if (h == INVALID_HANDLE_VALUE) {
		errno = EBADF;
		return -1;
	}

retry:
	memset(&overlapped, 0, sizeof(overlapped));
	overlapped.Offset = offset & 0xFFFFFFFF;
	overlapped.OffsetHigh = offset >> 32;

	if (!WriteFile(h, buffer, size, &count, &overlapped)) {
		DWORD err = GetLastError();
		/* See windows_pread() for comments on this error management */
		if (err == ERROR_NO_SYSTEM_RESOURCES) {
			os_syslog(OS_LVL_ERROR, "unexpected Windows ERROR_NO_SYSTEM_RESOURCES in pwrite(), retrying...");
			Sleep(50);
			goto retry;
		}

		windows_errno(err);
		return -1;
	}

	return count;
}

struct tm* windows_gmtime_r(const time_t* timer, struct tm* result)
{
	/* Windows returns 0 on success, unlike POSIX which returns the pointer */
	if (gmtime_s(result, timer) == 0) {
		return result;
	}
	return NULL;
}

struct tm* windows_localtime_r(const time_t* timer, struct tm* result)
{
	/* Windows returns 0 on success, unlike POSIX which returns the pointer */
	if (localtime_s(result, timer) == 0) {
		return result;
	}
	return NULL;
}

char* windows_realpath(const char* path, char* resolved_path)
{
	wchar_t conv_buf[CONV_MAX];
	HANDLE h = CreateFileW(convert(conv_buf, path), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if (h == INVALID_HANDLE_VALUE) {
		windows_errno(GetLastError());
		return 0;
	}

	DWORD result = GetFinalPathNameByHandleW(h, conv_buf, CONV_MAX, FILE_NAME_NORMALIZED);

	CloseHandle(h);

	if (result == 0 || result >= CONV_MAX) {
		windows_errno(GetLastError());
		return 0;
	}

	resolved_path = u16tou8(resolved_path, conv_buf);

	/* Windows prefixes paths with "\\?\" (UNC). Skip it if you want a standard path */
	if (strncmp(resolved_path, "\\\\?\\", 4) == 0) {
		memmove(resolved_path, resolved_path + 4, strlen(resolved_path) - 3);
	}

	return resolved_path;
}

/****************************************************************************/
/* fs */

static BOOL GetFilePhysicalOffset(HANDLE h, uint64_t* physical)
{
	STARTING_VCN_INPUT_BUFFER svib;
	unsigned char rpb_buffer[sizeof(RETRIEVAL_POINTERS_BUFFER)];
	RETRIEVAL_POINTERS_BUFFER* rpb = (RETRIEVAL_POINTERS_BUFFER*)&rpb_buffer;
	BOOL ret;
	DWORD n;

	/* in Wine FSCTL_GET_RETRIEVAL_POINTERS is not supported */
	if (is_wine) {
		*physical = FILEPHY_UNREPORTED_OFFSET;
		return TRUE;
	}

	/* set the output variable, just to be safe */
	rpb->ExtentCount = 0;

	/* read the physical address */
	svib.StartingVcn.QuadPart = 0;
	ret = DeviceIoControl(h, FSCTL_GET_RETRIEVAL_POINTERS, &svib, sizeof(svib), rpb_buffer, sizeof(rpb_buffer), &n, 0);
	if (!ret) {
		DWORD error = GetLastError();
		if (error == ERROR_MORE_DATA) {
			/*
			 * We ignore ERROR_MODE_DATA because we are interested only at the first entry
			 * and this is the expected error if the files has more entries
			 */
		} else if (error == ERROR_HANDLE_EOF) {
			/*
			 * If the file is small, it can be stored in the Master File Table (MFT)
			 * and then it doesn't have a physical address
			 * In such case we report a specific fake address, to report this special condition
			 * that it's different from the 0 offset reported by the underline file system
			 */
			*physical = FILEPHY_WITHOUT_OFFSET;
			return TRUE;
		} else if (error == ERROR_NOT_SUPPORTED) {
			/* for disks shared on network this operation is not supported */
			*physical = FILEPHY_UNREPORTED_OFFSET;
			return TRUE;
		} else {
			return FALSE;
		}
	}

	if (rpb->ExtentCount < 1)
		*physical = FILEPHY_UNREPORTED_OFFSET;
	else
		*physical = rpb->Extents[0].Lcn.QuadPart + FILEPHY_REAL_OFFSET;

	return TRUE;
}

int lstat_sync(const char* file, struct windows_stat* st, uint64_t* physical)
{
	wchar_t conv_buf[CONV_MAX];
	BY_HANDLE_FILE_INFORMATION info;
	FILE_ATTRIBUTE_TAG_INFO tag;
	HANDLE h;

	/*
	 * Open the handle of the file.
	 *
	 * Use FILE_FLAG_BACKUP_SEMANTICS to open directories (it's just ignored for files).
	 * Use FILE_FLAG_OPEN_REPARSE_POINT to open symbolic links and not the their target.
	 *
	 * Note that even with FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
	 * and FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT some paths
	 * cannot be opened like "C:\System Volume Information" resulting
	 * in error ERROR_ACCESS_DENIED.
	 */
	h = CreateFileW(convert(conv_buf, file), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, 0);
	if (h == INVALID_HANDLE_VALUE) {
		windows_errno(GetLastError());
		return -1;
	}

	if (!GetFileInformationByHandle(h, &info)) {
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

	/* read the physical offset, only if a pointer is provided */
	if (physical != 0) {
		if (!GetFilePhysicalOffset(h, physical)) {
			DWORD error = GetLastError();
			CloseHandle(h);
			windows_errno(error);
			return -1;
		}
	}

	if (!CloseHandle(h)) {
		windows_errno(GetLastError());
		return -1;
	}

	return windows_info2stat(&info, &tag, st);
}

int filephy(const char* file, uint64_t size, uint64_t* physical)
{
	wchar_t conv_buf[CONV_MAX];
	HANDLE h;

	(void)size;

	/* open the handle of the file */
	h = CreateFileW(convert(conv_buf, file), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0, OPEN_EXISTING, 0, 0);
	if (h == INVALID_HANDLE_VALUE) {
		windows_errno(GetLastError());
		return -1;
	}

	if (!GetFilePhysicalOffset(h, physical)) {
		DWORD error = GetLastError();
		CloseHandle(h);
		windows_errno(error);
		return -1;
	}

	CloseHandle(h);
	return 0;
}

/****************************************************************************/
/* pthread like interface */

int windows_mutex_init(windows_mutex_t* mutex, void* attr)
{
	(void)attr;

	InitializeCriticalSection(mutex);

	return 0;
}

int windows_mutex_destroy(windows_mutex_t* mutex)
{
	DeleteCriticalSection(mutex);

	return 0;
}

int windows_mutex_lock(windows_mutex_t* mutex)
{
	EnterCriticalSection(mutex);

	return 0;
}

int windows_mutex_unlock(windows_mutex_t* mutex)
{
	LeaveCriticalSection(mutex);

	return 0;
}

int windows_cond_init(windows_cond_t* cond, void* attr)
{
	(void)attr;

	InitializeConditionVariable(cond);

	return 0;
}

int windows_cond_destroy(windows_cond_t* cond)
{
	/* note that in Windows there is no DeleteConditionVariable() to call */
	(void)cond;

	return 0;
}

int windows_cond_signal(windows_cond_t* cond)
{
	WakeConditionVariable(cond);

	return 0;
}

int windows_cond_broadcast(windows_cond_t* cond)
{
	WakeAllConditionVariable(cond);

	return 0;
}

int windows_cond_wait(windows_cond_t* cond, windows_mutex_t* mutex)
{
	if (!SleepConditionVariableCS(cond, mutex, INFINITE))
		return -1;

	return 0;
}

int windows_rwlock_init(windows_rwlock_t* rwlock, void* attr)
{
	(void)attr;
	InitializeSRWLock(&rwlock->srw);
	rwlock->exclusive = 0;

	return 0;
}

int windows_rwlock_destroy(windows_rwlock_t* rwlock)
{
	(void)rwlock;

	return 0;
}

int windows_rwlock_rdlock(windows_rwlock_t* rwlock)
{
	AcquireSRWLockShared(&rwlock->srw);

	return 0;
}

int windows_rwlock_wrlock(windows_rwlock_t* rwlock)
{
	AcquireSRWLockExclusive(&rwlock->srw);
	rwlock->exclusive = 1;

	return 0;
}

int windows_rwlock_unlock(windows_rwlock_t* rwlock)
{
	if (rwlock->exclusive) {
		rwlock->exclusive = 0;
		ReleaseSRWLockExclusive(&rwlock->srw);
	} else {
		ReleaseSRWLockShared(&rwlock->srw);
	}

	return 0;
}

struct windows_key_context {
	void (*func)(void*);
	DWORD key;
	tommy_node node;
};

/* list of all keys with destructor */
static tommy_list windows_key_list = { 0 };

int windows_key_create(windows_key_t* key, void (*destructor)(void*))
{
	struct windows_key_context* context;

	context = malloc(sizeof(struct windows_key_context));
	if (!context) {
		errno = ENOMEM;
		return -1;
	}

	context->func = destructor;
	context->key = TlsAlloc();
	if (context->key == 0xFFFFFFFF) {
		windows_errno(GetLastError());
		free(context);
		return -1;
	}

	/* insert in the list of destructors */
	if (context->func)
		tommy_list_insert_tail(&windows_key_list, &context->node, context);

	*key = context;

	return 0;
}

int windows_key_delete(windows_key_t key)
{
	struct windows_key_context* context = key;

	/* use the destructor for the local variable and remove from the list of destructors */
	if (context->func) {
		void* value = windows_getspecific(context);
		if (value)
			context->func(value);
		tommy_list_remove_existing(&windows_key_list, &context->node);
	}

	TlsFree(context->key);

	free(context);

	return 0;
}

void* windows_getspecific(windows_key_t key)
{
	struct windows_key_context* context = key;

	return TlsGetValue(context->key);
}

int windows_setspecific(windows_key_t key, void* value)
{
	struct windows_key_context* context = key;

	if (!TlsSetValue(context->key, value)) {
		windows_errno(GetLastError());
		return -1;
	}

	return 0;
}

struct windows_thread_context {
	HANDLE h;
	unsigned id;
	void* (*func)(void*);
	void* arg;
	void* ret;
};

/* forwarder to change the function declaration */
static unsigned __stdcall windows_thread_func(void* arg)
{
	struct windows_thread_context* context = arg;
	tommy_node* i;

	context->ret = context->func(context->arg);

	/* call the destructor of all the keys */
	i = tommy_list_head(&windows_key_list);
	while (i) {
		struct windows_key_context* key = i->data;
		if (key->func) {
			void* value = windows_getspecific(key);
			if (value)
				key->func(value);
		}
		i = i->next;
	}

	return 0;
}

int windows_create(thread_id_t* thread, void* attr, void* (*func)(void*), void* arg)
{
	struct windows_thread_context* context;

	(void)attr;

	context = malloc(sizeof(struct windows_thread_context));
	if (!context) {
		errno = ENOMEM;
		return -1;
	}

	context->func = func;
	context->arg = arg;
	context->ret = 0;
	context->h = (void*)_beginthreadex(0, 0, windows_thread_func, context, 0, &context->id);

	if (context->h == 0) {
		free(context);
		return -1;
	}

	*thread = context;

	return 0;
}

int windows_join(thread_id_t thread, void** retval)
{
	struct windows_thread_context* context = thread;

	if (WaitForSingleObject(context->h, INFINITE) != WAIT_OBJECT_0) {
		windows_errno(GetLastError());
		CloseHandle(context->h);
		free(context);
		return -1;
	}

	if (!CloseHandle(context->h)) {
		windows_errno(GetLastError());
		free(context);
		return -1;
	}

	*retval = context->ret;

	free(context);

	return 0;
}

/****************************************************************************/
/* exec */

#define COMMAND_LINE_MAX 32767

static int needs_quote(const WCHAR* arg)
{
	while (*arg) {
		if (*arg == L' ' || *arg == L'\t' || *arg == L'"')
			return 1;
		++arg;
	}

	return 0;
}

#define charcat(c) \
	do { \
		if (pos + 1 >= size) { \
			return -1; \
		} \
		cmd[pos++] = (c); \
	} while (0)

static int fixcat(WCHAR* cmd, int size, int pos, const WCHAR* arg)
{
	while (*arg)
		charcat(*arg++);

	return pos;
}

static int argcat(WCHAR* cmd, int size, int pos, const WCHAR* arg)
{
	int has_quote;

	/* space separator */
	if (pos != 0)
		charcat(L' ');

	has_quote = needs_quote(arg);

	if (!has_quote) {
		while (*arg)
			charcat(*arg++);
	} else {
		/* starting quote */
		charcat(L'"');

		while (*arg) {
			int bl = 0;
			while (*arg == L'\\') {
				++arg;
				++bl;
			}

			if (*arg == 0) {
				/* double backslashes before closing quote */
				bl = bl * 2;
				while (bl--)
					charcat(L'\\');
			} else if (*arg == '"') {
				/* double backslashes + escape the quote */
				bl = bl * 2 + 1;
				while (bl--)
					charcat(L'\\');
				charcat(L'"');
				++arg;
			} else {
				/* normal backslashes */
				while (bl--)
					charcat(L'\\');
				charcat(*arg);
				++arg;
			}
		}

		/* ending quote */
		charcat(L'"');
	}

	return pos;
}

pid_t os_spawn(char** argv, int* stdout_read_int, int* stderr_read_int, const char* run_as_user)
{
	wchar_t conv[CONV_MAX];
	HANDLE stdout_write_handle = INVALID_HANDLE_VALUE;
	HANDLE stdout_read_handle = INVALID_HANDLE_VALUE;
	HANDLE stderr_write_handle = INVALID_HANDLE_VALUE;
	HANDLE stderr_read_handle = INVALID_HANDLE_VALUE;
	SECURITY_ATTRIBUTES sa;
	PROCESS_INFORMATION pi;
	STARTUPINFOW si;
	BOOL ret;
	int has_out = (stdout_read_int != NULL);
	int has_err = (stderr_read_int != NULL);
	int out_f = -1;
	int err_f = -1;

	/* set the bInheritHandle flag so pipe handles are inherited */
	sa.nLength = sizeof(SECURITY_ATTRIBUTES);
	sa.bInheritHandle = TRUE;
	sa.lpSecurityDescriptor = NULL;

	if (has_out) {
		/* create a pipe for the child process's STDOUT */
		if (!CreatePipe(&stdout_read_handle, &stdout_write_handle, &sa, 0)) {
			windows_errno(GetLastError());
			os_syslog(OS_LVL_INFO, "failed to create pipe for spawn, errno=%s(%d)", strerror(errno), errno);
			return -1;
		}

		/* ensure the reading handle to the pipe is not inherited */
		if (!SetHandleInformation(stdout_read_handle, HANDLE_FLAG_INHERIT, 0)) {
			windows_errno(GetLastError());
			os_syslog(OS_LVL_INFO, "failed to handle information for spawn, errno=%s(%d)", strerror(errno), errno);
			CloseHandle(stdout_write_handle);
			CloseHandle(stdout_read_handle);
			return -1;
		}

		out_f = _open_osfhandle((intptr_t)stdout_read_handle, O_RDONLY | O_BINARY);
		if (out_f == -1) {
			windows_errno(GetLastError());
			os_syslog(OS_LVL_INFO, "failed to open osfhandle for spawn, errno=%s(%d)", strerror(errno), errno);
			CloseHandle(stdout_write_handle);
			CloseHandle(stdout_read_handle);
			return -1;
		}
	}

	if (has_err) {
		/* create a pipe for the child process's STDERR */
		if (!CreatePipe(&stderr_read_handle, &stderr_write_handle, &sa, 0)) {
			windows_errno(GetLastError());
			os_syslog(OS_LVL_INFO, "failed to create pipe for spawn, errno=%s(%d)", strerror(errno), errno);
			if (has_out) {
				CloseHandle(stdout_write_handle);
				close(out_f);
			}
			return -1;
		}

		/* ensure the reading handle to the pipe is not inherited */
		if (!SetHandleInformation(stderr_read_handle, HANDLE_FLAG_INHERIT, 0)) {
			windows_errno(GetLastError());
			os_syslog(OS_LVL_INFO, "failed to handle information for spawn, errno=%s(%d)", strerror(errno), errno);
			CloseHandle(stderr_write_handle);
			CloseHandle(stderr_read_handle);
			if (has_out) {
				CloseHandle(stdout_write_handle);
				close(out_f);
			}
			return -1;
		}

		err_f = _open_osfhandle((intptr_t)stderr_read_handle, O_RDONLY | O_BINARY);
		if (err_f == -1) {
			windows_errno(GetLastError());
			os_syslog(OS_LVL_INFO, "failed to open osfhandle for spawn, errno=%s(%d)", strerror(errno), errno);
			CloseHandle(stderr_write_handle);
			CloseHandle(stderr_read_handle);
			if (has_out) {
				CloseHandle(stdout_write_handle);
				close(out_f);
			}
			return -1;
		}
	}

	/* prepare command line string (Windows uses a single string, not an array) */
	WCHAR cmd_buffer[COMMAND_LINE_MAX];
	char cmd_buffer_conv[COMMAND_LINE_MAX * 3]; /* * 3 is needed because a single UTF-16 character can take up to 3 bytes in UTF-8 */
	int pos = 0;
	for (int i = 0; argv[i]; ++i) {
		pos = argcat(cmd_buffer, COMMAND_LINE_MAX, pos, u8tou16(conv, argv[i]));
		if (pos < 0) {
			os_syslog(OS_LVL_INFO, "command to long for spawn");
			if (has_out) {
				CloseHandle(stdout_write_handle);
				close(out_f);
			}
			if (has_err) {
				CloseHandle(stderr_write_handle);
				close(err_f);
			}
			return -1;
		}
	}
	cmd_buffer[pos] = 0;

	/* set up members of the STARTUPINFO structure */
	ZeroMemory(&pi, sizeof(pi));
	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);
	si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
	si.hStdOutput = has_out ? stdout_write_handle : GetStdHandle(STD_OUTPUT_HANDLE);
	si.hStdError = has_err ? stderr_write_handle : GetStdHandle(STD_ERROR_HANDLE);
	si.dwFlags |= STARTF_USESTDHANDLES;

	/*
	 * Set the Working Directory to the root of the C drive.
	 * For safety, we avoid defaulting to C:\Windows\System32.
	 */
	const wchar_t* cwd = L"C:\\";

	/* create the child process */
	if (run_as_user == 0 || run_as_user[0] == 0) {
		ret = CreateProcessW(
			NULL,
			cmd_buffer,
			NULL, NULL,
			TRUE, /* inherit pipe handles */
			CREATE_NEW_PROCESS_GROUP,
			NULL, cwd,
			&si, &pi
		);
	} else {
		/* Drop to restricted service account */
		HANDLE h_token = NULL;

		/* Validate that the requested user is actually a supported Service Account before attempting logon */
		if (_stricmp(run_as_user, "LocalService") != 0 && _stricmp(run_as_user, "NetworkService") != 0) {
			os_syslog(OS_LVL_INFO, "only supported users are LocalService and NetworkService");
			if (has_out) {
				CloseHandle(stdout_write_handle);
				close(out_f);
			}
			if (has_err) {
				CloseHandle(stderr_write_handle);
				close(err_f);
			}
			return -1;
		}

		if (!LogonUserW(u8tou16(conv, run_as_user), L"NT AUTHORITY", NULL, LOGON32_LOGON_SERVICE, LOGON32_PROVIDER_DEFAULT, &h_token)) {
			windows_errno(GetLastError());
			os_syslog(OS_LVL_INFO, "failed to logon user %s, errno=%s(%d)", run_as_user, strerror(errno), errno);
			if (has_out) {
				CloseHandle(stdout_write_handle);
				close(out_f);
			}
			if (has_err) {
				CloseHandle(stderr_write_handle);
				close(err_f);
			}
			return -1;
		}

		/* Create an environment block to ensure PATH is loaded */
		LPVOID env = NULL;
		if (!CreateEnvironmentBlock(&env, h_token, FALSE)) {
			windows_errno(GetLastError());
			os_syslog(OS_LVL_INFO, "failed to get user %s environment, errno=%s(%d)", run_as_user, strerror(errno), errno);
			CloseHandle(h_token);
			if (has_out) {
				CloseHandle(stdout_write_handle);
				close(out_f);
			}
			if (has_err) {
				CloseHandle(stderr_write_handle);
				close(err_f);
			}
			return -1;
		}

		ret = CreateProcessAsUserW(
			h_token,
			NULL,
			cmd_buffer,
			NULL, NULL,
			TRUE, /* inherit pipe handles */
			CREATE_NEW_PROCESS_GROUP | CREATE_UNICODE_ENVIRONMENT,
			env, cwd,
			&si, &pi
		);

		if (env)
			DestroyEnvironmentBlock(env);
		CloseHandle(h_token);
	}
	if (!ret) {
		windows_errno(GetLastError());
		os_syslog(OS_LVL_INFO, "failed to create process '%s' for spawn, errno=%s(%d)", u16tou8_force(cmd_buffer_conv, sizeof(cmd_buffer_conv), cmd_buffer, wcslen(cmd_buffer) + 1, 0), strerror(errno), errno);
		if (has_out) {
			CloseHandle(stdout_write_handle);
			close(out_f);
		}
		if (has_err) {
			CloseHandle(stderr_write_handle);
			close(err_f);
		}
		return -1;
	}

	/* close the write end of the pipes in the parent */
	if (has_out) {
		CloseHandle(stdout_write_handle);
	}
	if (has_err) {
		CloseHandle(stderr_write_handle);
	}

	/* close the handle to the primary thread, we don't need it */
	CloseHandle(pi.hThread);

	if (has_out) {
		*stdout_read_int = out_f;
	}
	if (has_err) {
		*stderr_read_int = err_f;
	}

	return (intptr_t)pi.hProcess;
}

pid_t os_wait(pid_t pid, int* status)
{
	HANDLE h = (void*)pid;
	DWORD exit_code;

	WaitForSingleObject(h, INFINITE);

	if (GetExitCodeProcess(h, &exit_code)) {
		CloseHandle(h);
		*status = exit_code;
		return pid;
	}

	CloseHandle(h);

	return -1;
}

int os_term(pid_t pid)
{
	HANDLE h = (void*)pid;
	DWORD id = GetProcessId(h);

	if (id == 0)
		return -1;

	/* detach from current console (if any) */
	FreeConsole();

	/* attach to the child's console */
	if (!AttachConsole(id))
		return -1;

	/* Disable Ctrl-C for the PARENT so we don't kill ourselves */
	SetConsoleCtrlHandler(0, TRUE);

	/* This will now reach the child's SetConsoleCtrlHandler */
	GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, id);

	/* Clean up */
	FreeConsole();

	return 0;
}

int os_command(const char* command, const char* run_as_user, const char* stdin_text)
{
	wchar_t conv[CONV_MAX];
	HANDLE stdin_read_handle;
	HANDLE stdin_write_handle;
	SECURITY_ATTRIBUTES sa;
	PROCESS_INFORMATION pi;
	STARTUPINFOW si;
	BOOL ret;
	int64_t start, stop;

	start = os_tick_sec();

	sa.nLength = sizeof(SECURITY_ATTRIBUTES);
	sa.bInheritHandle = TRUE;
	sa.lpSecurityDescriptor = NULL;

	/* create pipe for child's STDIN */
	if (!CreatePipe(&stdin_read_handle, &stdin_write_handle, &sa, 0)) {
		windows_errno(GetLastError());
		os_syslog(OS_LVL_INFO, "failed to create pipe for command, errno=%s(%d)", strerror(errno), errno);
		return -1;
	}

	/* ensure the parent's write end is NOT inherited */
	if (!SetHandleInformation(stdin_write_handle, HANDLE_FLAG_INHERIT, 0)) {
		windows_errno(GetLastError());
		os_syslog(OS_LVL_INFO, "failed to handle information for spawn, errno=%s(%d)", strerror(errno), errno);
		CloseHandle(stdin_read_handle);
		CloseHandle(stdin_write_handle);
		return -1;
	}

	/* create a handle to the NUL device */
	HANDLE nul = CreateFileW(
		L"NUL",
		GENERIC_WRITE,
		FILE_SHARE_WRITE | FILE_SHARE_READ,
		&sa,
		OPEN_EXISTING,
		0,
		NULL);
	if (nul == INVALID_HANDLE_VALUE) {
		windows_errno(GetLastError());
		os_syslog(OS_LVL_INFO, "failed to create nul device, errno=%s(%d)", strerror(errno), errno);
		CloseHandle(stdin_read_handle);
		CloseHandle(stdin_write_handle);
		return -1;
	}

	ZeroMemory(&pi, sizeof(pi));
	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);
	si.hStdInput = stdin_read_handle;
	si.hStdOutput = nul;
	si.hStdError = nul;
	si.dwFlags |= STARTF_USESTDHANDLES;

	/*
	 * Set the Working Directory to the root of the C drive.
	 * For safety, we avoid defaulting to C:\Windows\System32.
	 */
	const wchar_t* cwd = L"C:\\";

	/*
	 * No drop of privilege requested
	 * Run exactly as the parent daemon
	 */
	if (run_as_user == 0 || run_as_user[0] == 0) {
		/* create the child process */
		ret = CreateProcessW(
			NULL,
			u8tou16(conv, command),
			NULL, NULL,
			TRUE, /* inherit pipe handles */
			CREATE_NO_WINDOW,
			NULL, cwd,
			&si, &pi
		);
	} else {
		/*
		 * Drop to restricted service account.
		 */
		HANDLE h_token = NULL;

		/*
		 * Validate that the requested user is actually a supported
		 * Service Account before attempting logon.
		 */
		if (_stricmp(run_as_user, "LocalService") != 0 && _stricmp(run_as_user, "NetworkService") != 0) {
			os_syslog(OS_LVL_INFO, "only supported users are LocalService and NetworkService");
			CloseHandle(stdin_read_handle);
			CloseHandle(stdin_write_handle);
			CloseHandle(nul);
			return -1;
		}

		if (!LogonUserW(u8tou16(conv, run_as_user), L"NT AUTHORITY", NULL, LOGON32_LOGON_SERVICE, LOGON32_PROVIDER_DEFAULT, &h_token)) {
			windows_errno(GetLastError());
			os_syslog(OS_LVL_INFO, "failed to logon user %s, errno=%s(%d)", run_as_user, strerror(errno), errno);
			CloseHandle(stdin_read_handle);
			CloseHandle(stdin_write_handle);
			CloseHandle(nul);
			return -1;
		}

		/*
		 * Create an environment block to ensure PATH is loaded.
		 */
		LPVOID env = NULL;
		if (!CreateEnvironmentBlock(&env, h_token, FALSE)) {
			windows_errno(GetLastError());
			os_syslog(OS_LVL_INFO, "failed to get user %s environment, errno=%s(%d)", run_as_user, strerror(errno), errno);
			CloseHandle(stdin_read_handle);
			CloseHandle(stdin_write_handle);
			CloseHandle(nul);
			CloseHandle(h_token);
			return -1;
		}

		ret = CreateProcessAsUserW(
			h_token,
			NULL,
			u8tou16(conv, command),
			NULL, NULL,
			TRUE, /* inherit pipe handles */
			CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
			env, cwd,
			&si, &pi
		);

		if (env)
			DestroyEnvironmentBlock(env);
		CloseHandle(h_token);
	}
	if (!ret) {
		windows_errno(GetLastError());
		os_syslog(OS_LVL_INFO, "failed to create process '%s' for command, errno=%s(%d)", command, strerror(errno), errno);
		CloseHandle(stdin_read_handle);
		CloseHandle(stdin_write_handle);
		CloseHandle(nul);
		return -1;
	}

	/* close nul device */
	CloseHandle(nul);

	/* close the read end in the parent immediately */
	CloseHandle(stdin_read_handle);

	/* write the string to the child's STDIN */
	if (stdin_text && strlen(stdin_text) > 0) {
		DWORD written;
		WriteFile(stdin_write_handle, stdin_text, (DWORD)strlen(stdin_text), &written, NULL);
	}

	/* closing the write handle sends EOF to the child */
	CloseHandle(stdin_write_handle);

	/* wait for completion and get exit code */
	WaitForSingleObject(pi.hProcess, INFINITE);

	DWORD status;
	GetExitCodeProcess(pi.hProcess, &status);

	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);

	stop = os_tick_sec();
	int64_t execution_time = stop - start;
	if (execution_time > 30)
		os_syslog(OS_LVL_WARNING, "command %s ran for %" PRId64 " seconds that is unexpectedly long", command, execution_time);

	if (WIFEXITED(status)) {
		int exit_code = WEXITSTATUS(status);
		if (exit_code == 0)
			os_syslog(OS_LVL_INFO, "command %s terminated in %" PRId64 " seconds with success", command, execution_time);
		else
			os_syslog(OS_LVL_INFO, "command %s terminated in %" PRId64 " seconds with exit code %d", command, execution_time, exit_code);
		return exit_code;
	} else if (WIFSIGNALED(status)) {
		/* child died from a signal */
		int sig = WTERMSIG(status);
		os_syslog(OS_LVL_INFO, "command %s terminated in %" PRId64 " seconds with signal %s(%d)", command, execution_time, os_signal_name(sig), sig);
		return 128 + sig;
	} else {
		/* in Windows it can happen */
		os_syslog(OS_LVL_INFO, "command %s terminated in %" PRId64 " seconds for unknown reason, status=0x%08x", command, execution_time, (unsigned)status);
		return -1;
	}
}

static WCHAR* env_combine(const WCHAR* base_env, char** envp)
{
	size_t base_len = 0;
	if (base_env) {
		const WCHAR* p = base_env;
		while (*p != 0) {
			p += wcslen(p) + 1;
		}
		base_len = p - base_env + 1;
	} else {
		base_len = 1;
	}

	size_t envv_w_total_len = 0;
	int envv_count = 0;
	if (envp) {
		while (envp[envv_count] != NULL) {
			envv_count++;
		}
	}

	WCHAR** envv_w = NULL;
	WCHAR* new_env = NULL;

	if (envv_count > 0) {
		envv_w = calloc(envv_count, sizeof(WCHAR*));
		if (!envv_w) {
			goto bail;
		}
		for (int i = 0; i < envv_count; ++i) {
			wchar_t conv[CONV_MAX];
			u8tou16(conv, envp[i]);
			size_t len = wcslen(conv);
			envv_w[i] = malloc((len + 1) * sizeof(WCHAR));
			if (!envv_w[i]) {
				errno = ENOMEM;
				goto bail;
			}
			memcpy(envv_w[i], conv, (len + 1) * sizeof(WCHAR));
			envv_w_total_len += len + 1;
		}
	}

	size_t total_len = base_len + envv_w_total_len + 1;
	new_env = malloc(total_len * sizeof(WCHAR));
	if (!new_env) {
		errno = ENOMEM;
		goto bail;
	}

	WCHAR* dst = new_env;
	if (base_len > 1 && base_env) {
		memcpy(dst, base_env, (base_len - 1) * sizeof(WCHAR));
		dst += base_len - 1;
	}
	for (int i = 0; i < envv_count; ++i) {
		if (envv_w && envv_w[i]) {
			size_t len = wcslen(envv_w[i]);
			memcpy(dst, envv_w[i], (len + 1) * sizeof(WCHAR));
			dst += len + 1;
		}
	}
	*dst = 0;

bail:
	if (envv_w) {
		for (int i = 0; i < envv_count; ++i) {
			free(envv_w[i]);
		}
		free(envv_w);
	}

	return new_env;
}

int os_script(char** argv, char** envp, const char* run_as_user)
{
	wchar_t conv[CONV_MAX];
	PROCESS_INFORMATION pi;
	STARTUPINFOW si;
	BOOL ret;
	char resolved_path[PATH_MAX];
	int64_t start, stop;
	const char* script_path = argv[0];

	/* resolve the script path to prevent symlink attacks */
	if (!realpath(script_path, resolved_path)) {
		os_syslog(OS_LVL_INFO, "failed to resolve script, path=%s, errno=%s(%d)", script_path, strerror(errno), errno);
		return -1;
	}

	start = os_tick_sec();

	ZeroMemory(&pi, sizeof(pi));
	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);

	/* prepare command line string (Windows uses a single string, not an array) */
	WCHAR cmd_buffer[COMMAND_LINE_MAX];
	char cmd_buffer_conv[COMMAND_LINE_MAX * 3]; /* * 3 is needed because a single UTF-16 character can take up to 3 bytes in UTF-8 */
	int pos = 0;

	/*
	 * We add an extra set of quotes: cmd /c " "path with spaces" arg1 "arg 2" "
	 * This ensures cmd.exe parses the internal quotes correctly.
	 */
	pos = fixcat(cmd_buffer, COMMAND_LINE_MAX, pos, L"cmd.exe /c \" ");
	pos = argcat(cmd_buffer, COMMAND_LINE_MAX, pos, u8tou16(conv, resolved_path));
	if (pos < 0) {
		os_syslog(OS_LVL_INFO, "command to long for script");
		return -1;
	}
	for (int i = 1; argv[i]; ++i) {
		pos = argcat(cmd_buffer, COMMAND_LINE_MAX, pos, u8tou16(conv, argv[i]));
		if (pos < 0) {
			os_syslog(OS_LVL_INFO, "command to long for script");
			return -1;
		}
	}
	pos = fixcat(cmd_buffer, COMMAND_LINE_MAX, pos, L" \"");
	if (pos < 0) {
		os_syslog(OS_LVL_INFO, "command to long for script");
		return -1;
	}
	cmd_buffer[pos] = 0;

	/*
	 * Set the Working Directory to the root of the C drive.
	 * For safety, we avoid defaulting to C:\Windows\System32.
	 */
	const wchar_t* cwd = L"C:\\";

	/*
	 * No drop of privilege requested
	 * Run exactly as the parent daemon
	 */
	if (run_as_user == 0 || run_as_user[0] == 0) {
		WCHAR* base_env = NULL;
		WCHAR* combined_env = NULL;
		DWORD creation_flags = CREATE_NO_WINDOW;

		if (envp != NULL) {
			base_env = GetEnvironmentStringsW();
			if (!base_env) {
				windows_errno(GetLastError());
				os_syslog(OS_LVL_INFO, "failed to get environment strings, errno=%s(%d)", strerror(errno), errno);
				return -1;
			}
			combined_env = env_combine(base_env, envp);
			FreeEnvironmentStringsW(base_env);
			if (!combined_env) {
				errno = ENOMEM;
				os_syslog(OS_LVL_INFO, "failed to combine environment strings (out of memory)");
				return -1;
			}
			creation_flags |= CREATE_UNICODE_ENVIRONMENT;
		}

		/* create the child process */
		ret = CreateProcessW(
			NULL,
			cmd_buffer,
			NULL, NULL,
			FALSE, /* no need to inherit handles */
			creation_flags,
			combined_env, cwd,
			&si, &pi
		);

		if (combined_env)
			free(combined_env);
	} else {
		/*
		 * Drop to restricted service account.
		 */
		HANDLE h_token = NULL;

		/*
		 * Validate that the requested user is actually a supported
		 * Service Account before attempting logon.
		 */
		if (_stricmp(run_as_user, "LocalService") != 0 && _stricmp(run_as_user, "NetworkService") != 0) {
			os_syslog(OS_LVL_INFO, "only supported users are LocalService and NetworkService");
			return -1;
		}

		if (!LogonUserW(u8tou16(conv, run_as_user), L"NT AUTHORITY", NULL, LOGON32_LOGON_SERVICE, LOGON32_PROVIDER_DEFAULT, &h_token)) {
			windows_errno(GetLastError());
			os_syslog(OS_LVL_INFO, "failed to logon user %s, errno=%s(%d)", run_as_user, strerror(errno), errno);
			return -1;
		}

		/*
		 * Create an environment block to ensure PATH is loaded.
		 */
		LPVOID env = NULL;
		if (!CreateEnvironmentBlock(&env, h_token, FALSE)) {
			windows_errno(GetLastError());
			os_syslog(OS_LVL_INFO, "failed to get user %s environment, errno=%s(%d)", run_as_user, strerror(errno), errno);
			CloseHandle(h_token);
			return -1;
		}

		WCHAR* combined_env = NULL;
		if (envp != NULL) {
			combined_env = env_combine((const WCHAR*)env, envp);
			if (!combined_env) {
				errno = ENOMEM;
				os_syslog(OS_LVL_INFO, "failed to combine environment strings (out of memory)");
				DestroyEnvironmentBlock(env);
				CloseHandle(h_token);
				return -1;
			}
		}

		ret = CreateProcessAsUserW(
			h_token,
			NULL,
			cmd_buffer,
			NULL, NULL,
			FALSE, /* no need to inherit handles */
			CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
			combined_env ? combined_env : env, cwd,
			&si, &pi
		);

		if (combined_env)
			free(combined_env);
		if (env)
			DestroyEnvironmentBlock(env);
		CloseHandle(h_token);
	}
	if (!ret) {
		windows_errno(GetLastError());
		os_syslog(OS_LVL_INFO, "failed to create process '%s' for script, errno=%s(%d)", u16tou8_force(cmd_buffer_conv, sizeof(cmd_buffer_conv), cmd_buffer, wcslen(cmd_buffer) + 1, 0), strerror(errno), errno);
		return -1;
	}

	WaitForSingleObject(pi.hProcess, INFINITE);

	DWORD status;
	GetExitCodeProcess(pi.hProcess, &status);

	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);

	stop = os_tick_sec();
	int64_t execution_time = stop - start;
	if (execution_time > 30)
		os_syslog(OS_LVL_WARNING, "script %s took %" PRId64 " seconds", resolved_path, execution_time);

	if (WIFEXITED(status)) {
		int exit_code = WEXITSTATUS(status);
		if (exit_code == 0)
			os_syslog(OS_LVL_INFO, "script %s terminated in %" PRId64 " seconds with success", resolved_path, execution_time);
		else
			os_syslog(OS_LVL_INFO, "script %s terminated in %" PRId64 " seconds with exit code %d", resolved_path, execution_time, exit_code);
		return exit_code;
	} else if (WIFSIGNALED(status)) {
		/* child died from a signal */
		int sig = WTERMSIG(status);
		os_syslog(OS_LVL_INFO, "script %s terminated in %" PRId64 " seconds with signal %s(%d)", resolved_path, execution_time, os_signal_name(sig), sig);
		return 128 + sig;
	} else {
		/* in Windows it can happen */
		os_syslog(OS_LVL_INFO, "script %s terminated in %" PRId64 " seconds for unknown reason, status=0x%08x", resolved_path, execution_time, (unsigned)status);
		return -1;
	}
}

/****************************************************************************/
/* os */

int os_randomize(void* void_ptr, size_t size)
{
	unsigned char* ptr = void_ptr;

	/* try RtlGenRandom */
	if (ptr_RtlGenRandom != 0 && ptr_RtlGenRandom(ptr, size) != 0)
		return 0;

	return -1;
}

char* os_fgets(char* s, int size, OS_FILE* stream)
{
	return fgets(s, size, stream);
}

void os_clear(void)
{
	HANDLE console;
	CONSOLE_SCREEN_BUFFER_INFO screen;
	COORD coord;
	DWORD written;

	/* get the console */
	console = GetStdHandle(STD_OUTPUT_HANDLE);
	if (console == INVALID_HANDLE_VALUE)
		return;

	/* get the screen size */
	if (!GetConsoleScreenBufferInfo(console, &screen))
		return;

	/* fill the screen with spaces */
	coord.X = 0;
	coord.Y = 0;
	FillConsoleOutputCharacterA(console, ' ', screen.dwSize.X * screen.dwSize.Y, coord, &written);

	/* set the cursor at the top left corner */
	SetConsoleCursorPosition(console, coord);
}

uint64_t os_tick(void)
{
	LARGE_INTEGER t;
	uint64_t r;

	/*
	 * Ensure to return a strict monotone os_tick counter.
	 *
	 * We had reports of invalid stats due faulty High Precision Event Timer.
	 * See: https://sourceforge.net/p/snapraid/discussion/1677233/thread/a2122fd6/
	 */
	windows_mutex_lock(&tick_lock);

	/*
	 * MSDN 'QueryPerformanceCounter'
	 * "On systems that run Windows XP or later, the function"
	 * "will always succeed and will thus never return zero."
	 */
	r = 0;
	if (QueryPerformanceCounter(&t))
		r = t.QuadPart;

	if (r < tick_last)
		r = tick_last;
	tick_last = r;

	windows_mutex_unlock(&tick_lock);

	return r;
}

uint64_t os_tick_sec(void)
{
	return GetTickCount64() / 1000;
}

uint64_t os_tick_ms(void)
{
	return GetTickCount64();
}

void os_privileges_acquire(void)
{
}

void os_privileges_release(void)
{
}

void os_privileges_drop(void)
{
}

void os_abort(void)
{
	void* stack[32];
	size_t size;
	unsigned i;

	printf("Stacktrace of " PACKAGE " v" VERSION);
	printf(", mingw");
#ifdef __GNUC__
	printf(", gcc " __VERSION__);
#endif
	printf(", %d-bit", (int)sizeof(void*) * 8);
	printf(", PATH_MAX=%d", PATH_MAX);
	printf("\n");

	/* get stackstrace, but without symbols */
	size = CaptureStackBackTrace(0, 32, stack, NULL);

	for (i = 0; i < size; ++i)
		printf("[bt] %02u: %p\n", i, stack[i]);

	printf("Please report this error to the SnapRAID Issues:\n");
	printf("https://github.com/amadvance/snapraid/issues\n");

	/* use exit() and not abort to avoid the Windows abort dialog */
	os_exit();
}

void os_exit(void)
{
	exit(EXIT_FAILURE);
}

/**
 * Set the executable dir.
 */
static void exedir_init(void)
{
	DWORD size;
	WCHAR* slash;

	size = GetModuleFileNameW(0, exedir, PATH_MAX);
	if (size == 0 || size == PATH_MAX) {
		/* use empty dir */
		exedir[0] = 0;
		return;
	}

	slash = wcsrchr(exedir, L'\\');
	if (!slash) {
		/* use empty dir */
		exedir[0] = 0;
		return;
	}

	/* cut exe name */
	slash[1] = 0;
}

void os_init(unsigned opt)
{
	HMODULE ntdll, kernel32;

	is_scan_winfind = (opt & OS_INIT_OPT_WINFIND) != 0;

	/* initialize the thread local storage for strerror(), using free() as destructor */
	if (windows_key_create(&last_error, free) != 0) {
		os_syslog(OS_LVL_CRITICAL, "error calling windows_key_create()");
		os_exit();
	}

	tick_last = 0;
	if (windows_mutex_init(&tick_lock, 0) != 0) {
		os_syslog(OS_LVL_CRITICAL, "error calling windows_mutex_init()");
		os_exit();
	}

	ntdll = GetModuleHandle("NTDLL.DLL");
	if (!ntdll) {
		os_syslog(OS_LVL_CRITICAL, "error loading the NTDLL module");
		os_exit();
	}

	kernel32 = GetModuleHandle("KERNEL32.DLL");
	if (!kernel32) {
		os_syslog(OS_LVL_CRITICAL, "error loading the KERNEL32 module");
		os_exit();
	}

	dll_advapi32 = LoadLibrary("ADVAPI32.DLL");
	if (!dll_advapi32) {
		os_syslog(OS_LVL_CRITICAL, "error loading the ADVAPI32 module");
		os_exit();
	}

	/* check for Wine presence */
	is_wine = GetProcAddress(ntdll, "wine_get_version") != 0;

	/* get pointer to RtlGenRandom, note that it was reported missing in some cases */
	ptr_RtlGenRandom = (void*)GetProcAddress(dll_advapi32, "SystemFunction036");

	if ((opt & OS_INIT_OPT_AVOID_SLEEP) != 0) {
		/*
		 * Set the thread execution level to avoid sleep
		 * first try for Windows 7
		 */
		if (SetThreadExecutionState(WIN32_ES_CONTINUOUS | WIN32_ES_SYSTEM_REQUIRED | WIN32_ES_AWAYMODE_REQUIRED) == 0) {
			/* retry with the XP variant */
			SetThreadExecutionState(WIN32_ES_CONTINUOUS | WIN32_ES_SYSTEM_REQUIRED);
		}
	}

	exedir_init();
}

void os_done(void)
{
	/* delete the thread local storage for strerror() */
	windows_key_delete(last_error);

	windows_mutex_destroy(&tick_lock);

	/* restore the normal execution level */
	SetThreadExecutionState(WIN32_ES_CONTINUOUS);

	FreeLibrary(dll_advapi32);
}

#endif

