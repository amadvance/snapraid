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

#include "support.h"

/**
 * Exit codes.
 */
int exit_success = 0;
int exit_failure = 1;
int exit_sync_needed = 2;

/* Add missing Windows declaration */

/* For SetThreadExecutionState */
#define WIN32_ES_SYSTEM_REQUIRED      0x00000001L
#define WIN32_ES_DISPLAY_REQUIRED     0x00000002L
#define WIN32_ES_USER_PRESENT         0x00000004L
#define WIN32_ES_AWAYMODE_REQUIRED    0x00000040L
#define WIN32_ES_CONTINUOUS           0x80000000L

/* File Index */
#define FILE_INVALID_FILE_ID          ((ULONGLONG)-1LL)

/**
 * Direct access to RtlGenRandom().
 * This function is accessible only with LoadLibrary() and it's available from Windows XP.
 */
static BOOLEAN (WINAPI* ptr_RtlGenRandom)(PVOID, ULONG);

/**
 * Direct access to GetTickCount64().
 * This function is available only from Windows Vista.
 */
static ULONGLONG (WINAPI* ptr_GetTickCount64)(void);

/**
 * Description of the last error.
 * It's stored in the thread local storage.
 */
static windows_key_t last_error;

/**
 * Monotone tick counter
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
static WCHAR exedir[MAX_PATH];

/**
 * Set the executable dir.
 */
static void exedir_init(void)
{
	DWORD size;
	WCHAR* slash;

	size = GetModuleFileNameW(0, exedir, MAX_PATH);
	if (size == 0 || size == MAX_PATH) {
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

void os_init(int opt)
{
	HMODULE ntdll, kernel32;

	is_scan_winfind = opt != 0;

	/* initialize the thread local storage for strerror(), using free() as destructor */
	if (windows_key_create(&last_error, free) != 0) {
		log_fatal("Error calling windows_key_create().\n");
		exit(EXIT_FAILURE);
	}

	tick_last = 0;
	if (windows_mutex_init(&tick_lock, 0) != 0) {
		log_fatal("Error calling windows_mutex_init().\n");
		exit(EXIT_FAILURE);
	}

	ntdll = GetModuleHandle("NTDLL.DLL");
	if (!ntdll) {
		log_fatal("Error loading the NTDLL module.\n");
		exit(EXIT_FAILURE);
	}

	kernel32 = GetModuleHandle("KERNEL32.DLL");
	if (!kernel32) {
		log_fatal("Error loading the KERNEL32 module.\n");
		exit(EXIT_FAILURE);
	}

	dll_advapi32 = LoadLibrary("ADVAPI32.DLL");
	if (!dll_advapi32) {
		log_fatal("Error loading the ADVAPI32 module.\n");
		exit(EXIT_FAILURE);
	}

	/* check for Wine presence */
	is_wine = GetProcAddress(ntdll, "wine_get_version") != 0;

	/* setup the standard random generator used as fallback */
	srand(GetTickCount());

	/* get pointer to RtlGenRandom, note that it was reported missing in some cases */
	ptr_RtlGenRandom = (void*)GetProcAddress(dll_advapi32, "SystemFunction036");

	/* get pointer to RtlGenRandom, note that it was reported missing in some cases */
	ptr_GetTickCount64 = (void*)GetProcAddress(kernel32, "GetTickCount64");

	/* set the thread execution level to avoid sleep */
	/* first try for Windows 7 */
	if (SetThreadExecutionState(WIN32_ES_CONTINUOUS | WIN32_ES_SYSTEM_REQUIRED | WIN32_ES_AWAYMODE_REQUIRED) == 0) {
		/* retry with the XP variant */
		SetThreadExecutionState(WIN32_ES_CONTINUOUS | WIN32_ES_SYSTEM_REQUIRED);
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
	printf(", %d-bit", (int)sizeof(void *) * 8);
	printf(", PATH_MAX=%d", PATH_MAX);
	printf("\n");

	/* get stackstrace, but without symbols */
	size = CaptureStackBackTrace(0, 32, stack, NULL);

	for (i = 0; i < size; ++i)
		printf("[bt] %02u: %p\n", i, stack[i]);

	printf("Please report this error to the SnapRAID Forum:\n");
	printf("https://sourceforge.net/p/snapraid/discussion/1677233/\n");

	/* use exit() and not abort to avoid the Windows abort dialog */
	exit(EXIT_FAILURE);
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

/**
 * Size in chars of conversion buffers for u8to16() and u16to8().
 */
#define CONV_MAX PATH_MAX

/**
 * Convert a generic string from UTF8 to UTF16.
 */
static wchar_t* u8tou16(wchar_t* conv_buf, const char* src)
{
	int ret;

	ret = MultiByteToWideChar(CP_UTF8, 0, src, -1, conv_buf, CONV_MAX);

	if (ret <= 0) {
		log_fatal("Error converting name '%s' from UTF-8 to UTF-16\n", src);
		exit(EXIT_FAILURE);
	}

	return conv_buf;
}

/**
 * Convert a generic string from UTF16 to UTF8.
 */
static char* u16tou8ex(char* conv_buf, const wchar_t* src, size_t number_of_wchar, size_t* result_length_without_terminator)
{
	int ret;

	ret = WideCharToMultiByte(CP_UTF8, 0, src, number_of_wchar, conv_buf, CONV_MAX, 0, 0);
	if (ret <= 0) {
		log_fatal("Error converting from UTF-16 to UTF-8\n");
		exit(EXIT_FAILURE);
	}

	*result_length_without_terminator = ret;

	return conv_buf;
}

static char* u16tou8(char* conv_buf, const wchar_t* src)
{
	size_t len;

	/* convert also the 0 terminator */
	return u16tou8ex(conv_buf, src, wcslen(src) + 1, &len);
}

/**
 * Check if the char is a forward or back slash.
 */
static int is_slash(char c)
{
	return c == '/' || c == '\\';
}

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
static wchar_t* convert_arg(wchar_t* conv_buf, const char* src, int only_if_required)
{
	int ret;
	wchar_t* dst;
	int count;

	dst = conv_buf;

	/* note that we always check for both / and \ because the path is blindly */
	/* converted to unix format by path_import() */

	if (only_if_required && strlen(src) < 260 - 12) {
		/* it's a short path */
		/* 260 is the MAX_PATH, note that it includes the space for the terminating NUL */
		/* 12 is an additional space for filename, required when creating directory */

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
		log_fatal("Error converting name '%s' from UTF-8 to UTF-16\n", src);
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

	return conv_buf;
}

#define convert(buf, a) convert_arg(buf, a, 0)
#define convert_if_required(buf, a) convert_arg(buf, a, 1)

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
	uint64_t mtime;

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
	if (st->st_ino == FILE_INVALID_FILE_ID) {
		log_fatal("Invalid inode number! Is this ReFS?\n");
		errno = EINVAL;
		return -1;
	}

	return 0;
}

/**
 * Convert Windows info to the Unix stat format.
 */
static int windows_stream2stat(const BY_HANDLE_FILE_INFORMATION* info, const FILE_ID_BOTH_DIR_INFO* stream, struct windows_stat* st)
{
	uint64_t mtime;

	/* The FILE_ID_BOTH_DIR_INFO doesn't have the ReparseTag information */
	/* we could use instead FILE_ID_EXTD_DIR_INFO, but it's available only */
	/* from Windows Server 2012 */
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
	if (st->st_ino == FILE_INVALID_FILE_ID) {
		log_fatal("Invalid inode number! Is this ReFS?\n");
		errno = EINVAL;
		return -1;
	}

	return 0;
}

/**
 * Convert Windows findfirst info to the Unix stat format.
 */
static void windows_finddata2stat(const WIN32_FIND_DATAW* info, struct windows_stat* st)
{
	uint64_t mtime;

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

	name = u16tou8ex(conv_buf, info->cFileName, wcslen(info->cFileName), &len);

	if (len + 1 >= sizeof(dirent->d_name)) {
		log_fatal("Name too long\n");
		exit(EXIT_FAILURE);
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

	name = u16tou8ex(conv_buf, stream->FileName, stream->FileNameLength / 2, &len);

	if (len + 1 >= sizeof(dirent->d_name)) {
		log_fatal("Name too long\n");
		exit(EXIT_FAILURE);
	}

	memcpy(dirent->d_name, name, len);
	dirent->d_name[len] = 0;

	return windows_stream2stat(info, stream, &dirent->d_stat);
}

/**
 * Convert Windows error to errno.
 */
static void windows_errno(DWORD error)
{
	switch (error) {
	case ERROR_INVALID_HANDLE :
		/* we check for a bad handle calling _get_osfhandle() */
		/* and in such case we return EBADF */
		/* Other cases are here identified with EINVAL */
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
	default :
		log_fatal("Unexpected Windows error %d.\n", (int)error);
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

static BOOL GetFilePhysicalOffset(HANDLE h, uint64_t* physical)
{
	STARTING_VCN_INPUT_BUFFER svib;
	unsigned char rpb_buffer[sizeof(RETRIEVAL_POINTERS_BUFFER)];
	RETRIEVAL_POINTERS_BUFFER* rpb = (RETRIEVAL_POINTERS_BUFFER*)&rpb_buffer;
	BOOL ret;
	DWORD n;

	/* in Wine FSCTL_GET_RETRIVIAL_POINTERS is not supported */
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
			/* we ignore ERROR_MODE_DATA because we are interested only at the first entry */
			/* and this is the expected error if the files has more entries */
		} else if (error == ERROR_HANDLE_EOF) {
			/* if the file is small, it can be stored in the Master File Table (MFT) */
			/* and then it doesn't have a physical address */
			/* In such case we report a specific fake address, to report this special condition */
			/* that it's different from the 0 offset reported by the underline file system */
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
			log_fatal("Unexpected Windows INVALID_HANDLE error in FlushFileBuffers().\n");
			log_fatal("Are you using ATA-over-Ethernet ? Please report it.\n");

			/* normal error processing */
			windows_errno(error);
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

int windows_utimensat(int fd, const char* file, struct windows_timespec tv[2], int flags)
{
	wchar_t conv_buf[CONV_MAX];
	HANDLE h;
	FILETIME ft;
	uint64_t mtime;
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
	mtime += 116444736000000000;

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
	if (!MoveFileExW(convert(conv_buf_from, from), convert(conv_buf_to, to), MOVEFILE_REPLACE_EXISTING)) {
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
	default:
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
	default:
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
		log_fatal("Low memory\n");
		exit(EXIT_FAILURE);
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
	WCHAR* wdir;

	dirstream = malloc(sizeof(windows_dir));
	if (!dirstream) {
		log_fatal("Low memory\n");
		exit(EXIT_FAILURE);
	}

	wdir = convert(conv_buf, dir);

	/* uses a 64 kB buffer for reading directory */
	dirstream->buffer_size = 64 * 1024;
	dirstream->buffer = malloc(dirstream->buffer_size);
	if (!dirstream->buffer) {
		log_fatal("Low memory\n");
		exit(EXIT_FAILURE);
	}

	dirstream->h = CreateFileW(wdir, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, 0);
	if (dirstream->h == INVALID_HANDLE_VALUE) {
		DWORD error = GetLastError();
		free(dirstream->buffer);
		free(dirstream);
		windows_errno(error);
		return 0;
	}

	/* get dir information for the VolumeSerialNumber */
	/* this value is used for all the files in the dir */
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
		if (windows_next_stream(dirstream) != 0) {
			free(dirstream->buffer);
			free(dirstream);
			return 0;
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

void windows_sleep(unsigned seconds)
{
	Sleep(seconds * 1000);
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
 * In Windows 10 allow creation of symblink by not privileged user.
 *
 * See: Symlinks in Windows 10!
 * https://blogs.windows.com/buildingapps/2016/12/02/symlinks-windows-10/#cQG7cx48oGH86lkI.97
 * "Specify this flag to allow creation of symbolic links when the process is not elevated"
 */
#ifndef SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
#define SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE 0x2
#endif

int windows_symlink(const char* existing, const char* file)
{
	wchar_t conv_buf_file[CONV_MAX];
	wchar_t conv_buf_existing[CONV_MAX];

	/* We must convert to the extended-length \\?\ format if the path is too long */
	/* otherwise the link creation fails. */
	/* But we don't want to always convert it, to avoid to recreate */
	/* user symlinks different than they were before */
	if (!CreateSymbolicLinkW(convert(conv_buf_file, file), convert_if_required(conv_buf_existing, existing),
		SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)
	) {
		DWORD error = GetLastError();
		if (GetLastError() != ERROR_INVALID_PARAMETER) {
			windows_errno(error);
			return -1;
		}

		/* retry without the new flag SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE */
		if (!CreateSymbolicLinkW(convert(conv_buf_file, file), convert_if_required(conv_buf_existing, existing), 0)) {
			windows_errno(GetLastError());
			return -1;
		}
	}

	return 0;
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
	name = u16tou8ex(conv_buf_name,
		rdb->SymbolicLinkReparseBuffer.PathBuffer + rdb->SymbolicLinkReparseBuffer.PrintNameOffset,
		rdb->SymbolicLinkReparseBuffer.PrintNameLength / 2, &len);

	/* check for overflow */
	if (len > size) {
		len = size;
	}

	memcpy(buffer, name, len);

	return len;
}

int devuuid(uint64_t device, char* uuid, size_t uuid_size)
{
	/* just use the volume serial number returned in the device parameter */
	snprintf(uuid, uuid_size, "%08x", (unsigned)device);

	log_tag("uuid:windows:%u:%s:\n", (unsigned)device, uuid);

	return 0;
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

int fsinfo(const char* path, int* has_persistent_inode, int* has_syncronized_hardlinks, uint64_t* total_space, uint64_t* free_space)
{
	wchar_t conv_buf[CONV_MAX];

	/* all FAT/exFAT/NTFS when managed from Windows have persistent inodes */
	if (has_persistent_inode)
		*has_persistent_inode = 1;

	/* NTFS doesn't synchronize hardlinks metadata */
	if (has_syncronized_hardlinks)
		*has_syncronized_hardlinks = 0;

	if (free_space || total_space) {
		ULARGE_INTEGER total_bytes;
		ULARGE_INTEGER total_free_bytes;
		DWORD attr;
		char dir[PATH_MAX];

		if (strlen(path) + 1 > sizeof(dir)) {
			windows_errno(ERROR_BUFFER_OVERFLOW);
			return -1;
		}

		strcpy(dir, path);

		/* get the file attributes */
		attr = GetFileAttributesW(convert(conv_buf, dir));
		if (attr == INVALID_FILE_ATTRIBUTES) {
			DWORD error = GetLastError();

			if (error != ERROR_FILE_NOT_FOUND) {
				windows_errno(error);
				return -1;
			}

			/* if it doesn't exist, we assume a file */
			/* and we check for the containing dir */
			attr = 0;
		}

		/* if it's not a directory, truncate the file name */
		if ((attr & FILE_ATTRIBUTE_DIRECTORY) == 0) {
			char* slash = strrchr(dir, '/');

			/**
			 * Cut the file name, but leave the last slash.
			 *
			 * This is done because a MSDN comment about using of UNC paths.
			 *
			 * MSDN 'GetDiskFreeSpaceEx function'
			 * http://msdn.microsoft.com/en-us/library/windows/desktop/aa364937%28v=vs.85%29.aspx
			 * If this parameter is a UNC name, it must include a trailing backslash,
			 * for example, "\\MyServer\MyShare\".
			 */
			if (slash)
				slash[1] = 0;
		}

		/* get the free space of the directory */
		/* note that it must be a directory */
		if (!GetDiskFreeSpaceExW(convert(conv_buf, dir), 0, &total_bytes, &total_free_bytes)) {
			windows_errno(GetLastError());
			return -1;
		}

		if (total_space)
			*total_space = total_bytes.QuadPart;
		if (free_space)
			*free_space = total_free_bytes.QuadPart;
	}

	return 0;
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
			log_fatal("Unexpected Windows ERROR_NO_SYSTEM_RESOURCES in pread(), retrying...\n");
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
			log_fatal("Unexpected Windows ERROR_NO_SYSTEM_RESOURCES in pwrite(), retrying...\n");
			Sleep(50);
			goto retry;
		}

		windows_errno(err);
		return -1;
	}

	return count;
}

size_t windows_direct_size(void)
{
	SYSTEM_INFO si;

	GetSystemInfo(&si);

	/*
	 * MSDN 'File Buffering'
	 * https://msdn.microsoft.com/en-us/library/windows/desktop/cc644950%28v=vs.85%29.aspx
	 *
	 * "Therefore, in most situations, page-aligned memory will also be sector-aligned,"
	 * "because the case where the sector size is larger than the page size is rare."
	 */
	return si.dwPageSize;
}

uint64_t tick(void)
{
	LARGE_INTEGER t;
	uint64_t r;

	/*
	 * Ensure to return a strict monotone tick counter.
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

uint64_t tick_ms(void)
{
	/* GetTickCount64() isn't supported in Windows XP */
	if (ptr_GetTickCount64 != 0)
		return ptr_GetTickCount64();

	return GetTickCount();
}

int randomize(void* void_ptr, size_t size)
{
	size_t i;
	unsigned char* ptr = void_ptr;

	/* try RtlGenRandom */
	if (ptr_RtlGenRandom != 0 && ptr_RtlGenRandom(ptr, size) != 0)
		return 0;

	/* try rand_s */
	for (i = 0; i < size; ++i) {
		unsigned v = 0;

		if (rand_s(&v) != 0)
			break;

		ptr[i] = v;
	}
	if (i == size)
		return 0;

	/* fallback to standard rand */
	for (i = 0; i < size; ++i)
		ptr[i] = rand();

	return 0;
}

/**
 * Get the device file from a path inside the device.
 */
static int devresolve(const char* mount, char* file, size_t file_size, char* wfile, size_t wfile_size)
{
	wchar_t conv_buf_mount[CONV_MAX];
	char conv_buf_volume_guid[CONV_MAX];
	WCHAR volume_mount[MAX_PATH];
	WCHAR volume_guid[MAX_PATH];
	DWORD i;
	char* p;

	/* get the volume mount point from the disk path */
	if (!GetVolumePathNameW(convert(conv_buf_mount, mount), volume_mount, sizeof(volume_mount) / sizeof(WCHAR))) {
		windows_errno(GetLastError());
		return -1;
	}

	/* get the volume GUID path from the mount point */
	if (!GetVolumeNameForVolumeMountPointW(volume_mount, volume_guid, sizeof(volume_guid) / sizeof(WCHAR))) {
		windows_errno(GetLastError());
		return -1;
	}

	/* remove the final slash, otherwise CreateFile() opens the file-system */
	/* and not the volume */
	i = 0;
	while (volume_guid[i] != 0)
		++i;
	if (i != 0 && volume_guid[i - 1] == '\\')
		volume_guid[i - 1] = 0;

	pathcpy(wfile, wfile_size, u16tou8(conv_buf_volume_guid, volume_guid));

	/* get the GUID start { */
	p = strchr(wfile, '{');
	if (!p)
		p = wfile;
	else
		++p;

	pathprint(file, file_size, "/dev/vol%s", p);

	/* cut GUID end } */
	p = strrchr(file, '}');
	if (p)
		*p = 0;

	return 0;
}

/**
 * Read a device tree filling the specified list of disk_t entries.
 */
static int devtree(const char* name, const char* custom, const char* wfile, devinfo_t* parent, tommy_list* list)
{
	wchar_t conv_buf[CONV_MAX];
	HANDLE h;
	unsigned char vde_buffer[sizeof(VOLUME_DISK_EXTENTS)];
	VOLUME_DISK_EXTENTS* vde = (VOLUME_DISK_EXTENTS*)&vde_buffer;
	unsigned vde_size = sizeof(vde_buffer);
	void* vde_alloc = 0;
	BOOL ret;
	DWORD n;
	DWORD i;

	/* open the volume */
	h = CreateFileW(convert(conv_buf, wfile), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, 0);
	if (h == INVALID_HANDLE_VALUE) {
		windows_errno(GetLastError());
		return -1;
	}

	/* get the physical extents of the volume */
	ret = DeviceIoControl(h, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, 0, 0, vde, vde_size, &n, 0);
	if (!ret) {
		DWORD error = GetLastError();
		if (error != ERROR_MORE_DATA) {
			CloseHandle(h);
			windows_errno(error);
		}

		/* more than one extends, allocate more space */
		vde_size = sizeof(VOLUME_DISK_EXTENTS) + vde->NumberOfDiskExtents * sizeof(DISK_EXTENT);
		vde_alloc = malloc_nofail(vde_size);
		vde = vde_alloc;

		/* retry with more space */
		ret = DeviceIoControl(h, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, 0, 0, vde, vde_size, &n, 0);
	}
	if (!ret) {
		DWORD error = GetLastError();
		CloseHandle(h);
		windows_errno(error);
		return -1;
	}

	for (i = 0; i < vde->NumberOfDiskExtents; ++i) {
		devinfo_t* devinfo;

		devinfo = calloc_nofail(1, sizeof(devinfo_t));

		pathcpy(devinfo->name, sizeof(devinfo->name), name);
		pathcpy(devinfo->smartctl, sizeof(devinfo->smartctl), custom);
		devinfo->device = vde->Extents[i].DiskNumber;
		pathprint(devinfo->file, sizeof(devinfo->file), "/dev/pd%" PRIu64, devinfo->device);
		pathprint(devinfo->wfile, sizeof(devinfo->wfile), "\\\\.\\PhysicalDrive%" PRIu64, devinfo->device);
		devinfo->parent = parent;

		/* insert in the list */
		tommy_list_insert_tail(list, &devinfo->node, devinfo);
	}

	if (!CloseHandle(h)) {
		windows_errno(GetLastError());
		return -1;
	}

	free(vde_alloc);

	return 0;
}

/**
 * Read smartctl --scan from a stream.
 * Return 0 on success.
 */
static int smartctl_scan(FILE* f, tommy_list* list)
{
	while (1) {
		char buf[256];
		char* s;

		s = fgets(buf, sizeof(buf), f);
		if (s == 0)
			break;

		/* remove extraneous chars */
		s = strpolish(buf);

		log_tag("smartctl:scan::text: %s\n", s);

		if (*s == '/') {
			char* sep = strchr(s, ' ');
			if (sep) {
				tommy_node* i;
				const char* number;
				uint64_t device;

				/* clear everything after the first space */
				*sep = 0;

				/* get the device number from the device file */
				/* note that this is Windows specific */
				/* for the format /dev/pdX of smartmontools */
				number = s;
				while (*number != 0 && !isdigit(*number))
					++number;
				device = atoi(number);

				/* check if already present */
				/* comparing the device file */
				for (i = tommy_list_head(list); i != 0; i = i->next) {
					devinfo_t* devinfo = i->data;
					if (devinfo->device == device)
						break;
				}

				/* if not found */
				if (i == 0) {
					devinfo_t* devinfo;

					devinfo = calloc_nofail(1, sizeof(devinfo_t));
					devinfo->device = device;
					pathprint(devinfo->file, sizeof(devinfo->file), "/dev/pd%" PRIu64, devinfo->device);
					pathprint(devinfo->wfile, sizeof(devinfo->wfile), "\\\\.\\PhysicalDrive%" PRIu64, devinfo->device);

					/* insert in the list */
					tommy_list_insert_tail(list, &devinfo->node, devinfo);
				}
			}
		}
	}

	return 0;
}

/**
 * Scan all the devices.
 */
static int devscan(tommy_list* list)
{
	char conv_buf[CONV_MAX];
	WCHAR cmd[MAX_PATH + 128];
	FILE* f;
	int ret;

	snwprintf(cmd, sizeof(cmd), L"\"%lssmartctl.exe\" --scan-open -d pd", exedir);

	log_tag("smartctl:scan::run: %s\n", u16tou8(conv_buf, cmd));

	f = _wpopen(cmd, L"rt");
	if (!f) {
		/* LCOV_EXCL_START */
		log_fatal("Failed to run '%s' (from popen).\n", u16tou8(conv_buf, cmd));
		return -1;
		/* LCOV_EXCL_STOP */
	}

	if (smartctl_scan(f, list) != 0) {
		/* LCOV_EXCL_START */
		pclose(f);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	ret = pclose(f);

	log_tag("smartctl:scan::ret: %x\n", ret);

	if (ret == -1) {
		/* LCOV_EXCL_START */
		log_fatal("Failed to run '%s' (from pclose).\n", u16tou8(conv_buf, cmd));
		return -1;
		/* LCOV_EXCL_STOP */
	}
	if (ret != 0) {
		/* LCOV_EXCL_START */
		log_fatal("Failed to run '%s' with return code %xh.\n", u16tou8(conv_buf, cmd), ret);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	return 0;
}

/**
 * Get SMART attributes.
 */
static int devsmart(uint64_t device, const char* name, const char* custom, uint64_t* smart, char* serial, char* vendor, char* model)
{
	char conv_buf[CONV_MAX];
	WCHAR cmd[MAX_PATH + 128];
	char file[128];
	FILE* f;
	int ret;
	int count;

	snprintf(file, sizeof(file), "/dev/pd%" PRIu64, device);

	/* if there is a custom command */
	if (custom[0]) {
		char option[128];
		snprintf(option, sizeof(option), custom, file);
		snwprintf(cmd, sizeof(cmd), L"\"%lssmartctl.exe\" -a %s", exedir, option);
	} else {
		snwprintf(cmd, sizeof(cmd), L"\"%lssmartctl.exe\" -a %s", exedir, file);
	}

	count = 0;

retry:
	log_tag("smartctl:%s:%s:run: %s\n", file, name, u16tou8(conv_buf, cmd));

	f = _wpopen(cmd, L"rt");
	if (!f) {
		/* LCOV_EXCL_START */
		log_fatal("Failed to run '%s' (from popen).\n", u16tou8(conv_buf, cmd));
		return -1;
		/* LCOV_EXCL_STOP */
	}

	if (smartctl_attribute(f, file, name, smart, serial, vendor, model) != 0) {
		/* LCOV_EXCL_START */
		pclose(f);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	ret = pclose(f);

	log_tag("smartctl:%s:%s:ret: %x\n", file, name, ret);

	if (ret == -1) {
		/* LCOV_EXCL_START */
		log_fatal("Failed to run '%s' (from pclose).\n", u16tou8(conv_buf, cmd));
		return -1;
		/* LCOV_EXCL_STOP */
	}

	/* if first try without custom command */
	if (count == 0 && custom[0] == 0) {
		/*
		 * Handle some common cases in Windows.
		 *
		 * Sometimes the "type" autodetection is wrong, and the command fails at identification
		 * stage, returning with error 2, or even with error 0, and with no info at all.
		 * We detect this condition checking the PowerOnHours, Size and RotationRate attributes.
		 *
		 * In such conditions we retry using the "sat" type, that often allows to proceed.
		 *
		 * Note that getting error 4 is instead very common, even with full info gathering.
		 */
		if ((ret == 0 || ret == 2)
			&& smart[9] == SMART_UNASSIGNED
			&& smart[SMART_SIZE] == SMART_UNASSIGNED
			&& smart[SMART_ROTATION_RATE] == SMART_UNASSIGNED
		) {
			/* retry using the "sat" type */
			snwprintf(cmd, sizeof(cmd), L"\"%lssmartctl.exe\" -a -d sat %s", exedir, file);

			++count;
			goto retry;
		}
	}

	/* store the smartctl return value */
	smart[SMART_FLAGS] = ret;

	return 0;
}

/**
 * Spin down a specific device.
 */
static int devdown(uint64_t device, const char* name, const char* custom)
{
	char conv_buf[CONV_MAX];
	WCHAR cmd[MAX_PATH + 128];
	char file[128];
	FILE* f;
	int ret;
	int count;

	snprintf(file, sizeof(file), "/dev/pd%" PRIu64, device);

	/* if there is a custom command */
	if (custom[0]) {
		char option[128];
		snprintf(option, sizeof(option), custom, file);
		snwprintf(cmd, sizeof(cmd), L"\"%lssmartctl.exe\" -s standby,now %s", exedir, option);
	} else {
		snwprintf(cmd, sizeof(cmd), L"\"%lssmartctl.exe\" -s standby,now %s", exedir, file);
	}

	count = 0;

retry:
	log_tag("smartctl:%s:%s:run: %s\n", file, name, u16tou8(conv_buf, cmd));

	f = _wpopen(cmd, L"rt");
	if (!f) {
		/* LCOV_EXCL_START */
		log_fatal("Failed to run '%s' (from popen).\n", u16tou8(conv_buf, cmd));
		return -1;
		/* LCOV_EXCL_STOP */
	}

	if (smartctl_flush(f, file, name) != 0) {
		/* LCOV_EXCL_START */
		pclose(f);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	ret = pclose(f);

	log_tag("smartctl:%s:%s:ret: %x\n", file, name, ret);

	if (ret == -1) {
		/* LCOV_EXCL_START */
		log_fatal("Failed to run '%s' (from pclose).\n", u16tou8(conv_buf, cmd));
		return -1;
		/* LCOV_EXCL_STOP */
	}

	/* if first try without custom command */
	if (count == 0 && custom[0] == 0) {
		/*
		 * Handle some common cases in Windows.
		 *
		 * Sometimes the "type" autodetection is wrong, and the command fails at identification
		 * stage, returning with error 2.
		 *
		 * In such conditions we retry using the "sat" type, that often allows to proceed.
		 */
		if (ret == 2) {
			/* retry using the "sat" type */
			snwprintf(cmd, sizeof(cmd), L"\"%lssmartctl.exe\" -s standby,now -d sat %s", exedir, file);

			++count;
			goto retry;
		}
	}

	if (ret != 0) {
		/* LCOV_EXCL_START */
		log_fatal("Failed to run '%s' with return code %xh.\n", u16tou8(conv_buf, cmd), ret);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	return 0;
}

/**
 * Spin up a device.
 *
 * There isn't a defined way to spin up a device,
 * so we just do a generic write.
 */
static int devup(const char* mount)
{
	wchar_t conv_buf[CONV_MAX];
	int f;
	char path[PATH_MAX];

	/* add a temporary name used for writing */
	pathprint(path, sizeof(path), "%s.snapraid-spinup.tmp", mount);

	/* create a temporary file, automatically deleted on close */
	f = _wopen(convert(conv_buf, path), _O_CREAT | _O_TEMPORARY | _O_RDWR, _S_IREAD | _S_IWRITE);
	if (f != -1)
		close(f);

	return 0;
}

/**
 * Thread for spinning up.
 *
 * Note that filling up the devinfo object is done inside this thread,
 * to avoid to block the main thread if the device need to be spin up
 * to handle stat/resolve requests.
 */
static void* thread_spinup(void* arg)
{
	devinfo_t* devinfo = arg;
	struct stat st;
	uint64_t start;

	start = tick_ms();

	/* uses lstat_sync() that maps to CreateFile */
	/* we cannot use FindFirstFile because it doesn't allow to open the root dir */
	if (lstat_sync(devinfo->mount, &st, 0) != 0) {
		/* LCOV_EXCL_START */
		log_fatal("Failed to stat path '%s'. %s.\n", devinfo->mount, strerror(errno));
		return (void*)-1;
		/* LCOV_EXCL_STOP */
	}

	/* set the device number */
	devinfo->device = st.st_dev;

	if (devresolve(devinfo->mount, devinfo->file, sizeof(devinfo->file), devinfo->wfile, sizeof(devinfo->wfile)) != 0) {
		/* LCOV_EXCL_START */
		log_fatal("Failed to resolve path '%s'.\n", devinfo->mount);
		return (void*)-1;
		/* LCOV_EXCL_STOP */
	}

	if (devup(devinfo->mount) != 0) {
		/* LCOV_EXCL_START */
		return (void*)-1;
		/* LCOV_EXCL_STOP */
	}

	msg_status("Spunup device '%s' for disk '%s' in %" PRIu64 " ms.\n", devinfo->file, devinfo->name, tick_ms() - start);

	return 0;
}

/**
 * Thread for spinning down.
 */
static void* thread_spindown(void* arg)
{
	devinfo_t* devinfo = arg;
	uint64_t start;

	start = tick_ms();

	if (devdown(devinfo->device, devinfo->name, devinfo->smartctl) != 0) {
		/* LCOV_EXCL_START */
		return (void*)-1;
		/* LCOV_EXCL_STOP */
	}

	msg_status("Spundown device '%s' for disk '%s' in %" PRIu64 " ms.\n", devinfo->file, devinfo->name, tick_ms() - start);

	return 0;
}

/**
 * Thread for getting smart info.
 */
static void* thread_smart(void* arg)
{
	devinfo_t* devinfo = arg;

	if (devsmart(devinfo->device, devinfo->name, devinfo->smartctl, devinfo->smart, devinfo->smart_serial, devinfo->smart_vendor, devinfo->smart_model) != 0) {
		/* LCOV_EXCL_START */
		return (void*)-1;
		/* LCOV_EXCL_STOP */
	}

	return 0;
}

static int device_thread(tommy_list* list, void* (*func)(void* arg))
{
	int fail = 0;
	tommy_node* i;

	/* starts all threads */
	for (i = tommy_list_head(list); i != 0; i = i->next) {
		devinfo_t* devinfo = i->data;

		thread_create(&devinfo->thread, func, devinfo);
	}

	/* joins all threads */
	for (i = tommy_list_head(list); i != 0; i = i->next) {
		devinfo_t* devinfo = i->data;
		void* retval;

		thread_join(devinfo->thread, &retval);

		if (retval != 0)
			++fail;
	}

	if (fail != 0) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	return 0;
}

int devquery(tommy_list* high, tommy_list* low, int operation, int others)
{
	tommy_node* i;
	void* (*func)(void* arg) = 0;

	if (operation != DEVICE_UP) {
		/* for each device */
		for (i = tommy_list_head(high); i != 0; i = i->next) {
			devinfo_t* devinfo = i->data;

			if (devresolve(devinfo->mount, devinfo->file, sizeof(devinfo->file), devinfo->wfile, sizeof(devinfo->wfile)) != 0) {
				/* LCOV_EXCL_START */
				log_fatal("Failed to resolve path '%s'.\n", devinfo->mount);
				return -1;
				/* LCOV_EXCL_STOP */
			}

			/* expand the tree of devices */
			if (devtree(devinfo->name, devinfo->smartctl, devinfo->wfile, devinfo, low) != 0) {
				/* LCOV_EXCL_START */
				log_fatal("Failed to expand device '%s'.\n", devinfo->file);
				return -1;
				/* LCOV_EXCL_STOP */
			}
		}
	}

	if (operation == DEVICE_UP) {
		/* duplicate the high */
		for (i = tommy_list_head(high); i != 0; i = i->next) {
			devinfo_t* devinfo = i->data;
			devinfo_t* entry;

			entry = calloc_nofail(1, sizeof(devinfo_t));

			entry->device = devinfo->device;
			pathcpy(entry->name, sizeof(entry->name), devinfo->name);
			pathcpy(entry->mount, sizeof(entry->mount), devinfo->mount);

			/* insert in the high */
			tommy_list_insert_tail(low, &entry->node, entry);
		}
	}

	/* add other devices */
	if (others) {
		if (devscan(low) != 0) {
			/* LCOV_EXCL_START */
			log_fatal("Failed to list other devices.\n");
			return -1;
			/* LCOV_EXCL_STOP */
		}
	}

	switch (operation) {
	case DEVICE_UP : func = thread_spinup; break;
	case DEVICE_DOWN : func = thread_spindown; break;
	case DEVICE_SMART : func = thread_smart; break;
	}

	if (!func)
		return 0;

	return device_thread(low, func);
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

struct windows_key_context {
	void (* func)(void *);
	DWORD key;
	tommy_node node;
};

/* list of all keys with destructor */
static tommy_list windows_key_list = { 0 };

int windows_key_create(windows_key_t* key, void(* destructor)(void*))
{
	struct windows_key_context* context;

	context = malloc(sizeof(struct windows_key_context));
	if (!context)
		return -1;

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

	/* remove from the list of destructors */
	if (context->func)
		tommy_list_remove_existing(&windows_key_list, &context->node);

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
	void* (* func)(void *);
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

int windows_create(thread_id_t* thread, void* attr, void* (* func)(void *), void* arg)
{
	struct windows_thread_context* context;

	(void)attr;

	context = malloc(sizeof(struct windows_thread_context));
	if (!context)
		return -1;

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
		return -1;
	}

	if (!CloseHandle(context->h)) {
		windows_errno(GetLastError());
		return -1;
	}

	*retval = context->ret;

	free(context);

	return 0;
}

#endif

