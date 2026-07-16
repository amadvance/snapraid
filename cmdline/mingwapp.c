// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2011 Andrea Mazzoleni

#include "os/portable.h"

#ifdef __MINGW32__ /* Only for MingW */

#include "support.h"

/****************************************************************************/
/* global */

/**
 * Exit codes.
 */
int exit_success = 0;
int exit_failure = 1;
int exit_sync_needed = 2;

/****************************************************************************/
/* signal */

volatile sig_atomic_t global_interrupt = 0;

int os_signal_interrupt(void)
{
	return global_interrupt;
}

void app_signal_handler(int signum)
{
	/* report the request of interruption with the signal received */
	global_interrupt = signum;
}

/****************************************************************************/
/* fs */

int fsinfo(const char* path, int* has_persistent_inode, int* has_syncronized_hardlinks, uint64_t* total_space, uint64_t* free_space, char* fstype, size_t fstype_size, char* fslabel, size_t fslabel_size)
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

			/*
			 * If it doesn't exist, we assume a file
			 * and we check for the containing dir
			 */
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

		/*
		 * Get the free space of the directory
		 * note that it must be a directory
		 */
		if (!GetDiskFreeSpaceExW(convert(conv_buf, dir), 0, &total_bytes, &total_free_bytes)) {
			windows_errno(GetLastError());
			return -1;
		}

		if (total_space)
			*total_space = total_bytes.QuadPart;
		if (free_space)
			*free_space = total_free_bytes.QuadPart;
	}

	if (fstype && fslabel) {
		wchar_t volume_root[PATH_MAX];
		wchar_t fs_name[PATH_MAX];
		wchar_t vol_name[PATH_MAX];

		fstype[0] = 0;
		fslabel[0] = 0;
		if (GetVolumePathNameW(convert(conv_buf, path), volume_root, PATH_MAX)) {
			if (GetVolumeInformationW(volume_root, vol_name, PATH_MAX, 0, 0, 0, fs_name, PATH_MAX)) {
				char u8[CONV_MAX];
				size_t len;
				char* ret;

				ret = u16tou8_mayfail(u8, sizeof(u8), fs_name, wcslen(fs_name), &len);
				if (ret != 0 && len + 1 <= fstype_size) {
					memcpy(fstype, u8, len);
					fstype[len] = 0;
				}

				ret = u16tou8_mayfail(u8, sizeof(u8), vol_name, wcslen(vol_name), &len);
				if (ret != 0 && len + 1 <= fslabel_size) {
					memcpy(fslabel, u8, len);
					fslabel[len] = 0;
				}
			}
		}
	}

	return 0;
}

/****************************************************************************/
/* snapshot */

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

#define WINDOWS_NTFS_MAGIC  0x5346544E   /* 'N','T','F','S' */

#define PS_CMD_MAX 16384 /* needs a x4 factor to handle -EncodedCommand */

#define SNAPSHOT_GUID ".guid"

static const char b64chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int base64_encode(const unsigned char* in, size_t in_len, char* out, size_t out_max)
{
	size_t i, j = 0;
	for (i = 0; i < in_len; i += 3) {
		uint32_t val = 0;
		int count = 0;
		int k;

		for (k = 0; k < 3; ++k) {
			val <<= 8;
			if (i + k < in_len) {
				val |= in[i + k];
				++count;
			}
		}

		if (j + 4 >= out_max)
			return -1;

		out[j++] = b64chars[(val >> 18) & 0x3F];
		out[j++] = b64chars[(val >> 12) & 0x3F];
		out[j++] = (count > 1) ? b64chars[(val >> 6) & 0x3F] : '=';
		out[j++] = (count > 2) ? b64chars[val & 0x3F] : '=';
	}
	out[j] = 0;
	return 0;
}

/*
 * PowerShell helper
 *
 * Runs a PowerShell one-liner via _popen() and captures the first line
 * of stdout into `out` as UTF-8.
 * `out` may be NULL when output is not needed.
 *
 * The command is executed using -EncodedCommand (Base64 UTF-16LE) to safely
 * handle any shell metacharacters or quotes in paths and volume names.
 *
 * Returns 0 on success, -1 on failure.
 */
static int windows_ps(const char* ps_command, char* out, size_t out_size)
{
	char cmd[PS_CMD_MAX];
	WCHAR wcmd[PS_CMD_MAX];
	char b64cmd[PS_CMD_MAX];
	FILE* fp;
	int ret;
	int wlen;

	/* convert UTF-8 command to UTF-16LE WCHARs */
	wlen = MultiByteToWideChar(CP_UTF8, 0, ps_command, -1, wcmd, PS_CMD_MAX);
	if (wlen <= 0) {
		errno = EINVAL;
		return -1;
	}

	/* base64 encode the UTF-16LE command bytes (excluding the null terminator) */
	if (base64_encode((const unsigned char*)wcmd, (wlen - 1) * sizeof(WCHAR), b64cmd, sizeof(b64cmd)) != 0) {
		errno = E2BIG;
		log_error(errno, "Failed to run PowerShell command '%s' (too big to encode).\n", ps_command);
		return -1;
	}

	ret = snprintf(cmd, sizeof(cmd), "powershell.exe -NoProfile -NonInteractive -EncodedCommand \"%s\" 2>nul", b64cmd);
	if (ret < 0 || ret >= (int)sizeof(cmd)) {
		errno = EINVAL;
		return -1;
	}

	fp = _popen(cmd, "r");
	if (!fp) {
		windows_errno(GetLastError());
		log_error(errno, "Failed to run PowerShell command '%s' (from popen).\n", ps_command);
		return -1;
	}

	if (out && out_size > 0) {
		/* get the first line */
		if (!fgets(out, (int)out_size, fp)) {
			out[0] = 0;
		} else {
			/* trim spaces and newlines */
			strtrim(out);
		}
	}

	ret = _pclose(fp);
	if (ret == -1) {
		errno = EINVAL;
		log_error(errno, "Failed to run PowerShell command '%s' (from pclose).\n", ps_command);
		return -1;
	}
	if (ret != 0) {
		errno = EINVAL;
		log_error(errno, "PowerShell command '%s' failed with '%d' (from pclose).\n", ps_command, ret);
		return -1;
	}

	return 0;
}

/**
 * Validates a GUID string of the form: {xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}
 *
 * Returns 0 if valid, -1 otherwise.
 */
int windows_guid_is_valid(const char* guid, int len)
{
	if (!guid || len != 38)
		return -1;

	if (guid[0] != '{' || guid[37] != '}')
		return -1;

	const char* inner = guid + 1;

	for (int i = 0; i < 36; ++i) {
		if (i == 8 || i == 13 || i == 18 || i == 23) {
			if (inner[i] != '-')
				return -1;
		} else {
			char c = inner[i];
			int is_digit = (c >= '0' && c <= '9');
			int is_upper_hex = (c >= 'A' && c <= 'F');
			int is_lower_hex = (c >= 'a' && c <= 'f');

			if (!is_digit && !is_upper_hex && !is_lower_hex)
				return -1;
		}
	}

	return 0;
}

static int windows_read_guid(const char* guid_link, char* guid, size_t guid_size)
{
	int len = windows_readlink(guid_link, guid, guid_size);
	if (len < 0 && errno == ENOENT) {
		guid[0] = 0;
		return 0;
	}
	if (len < 0) {
		log_error(errno, "Error readlink '%s'. %s.\n", guid_link, strerror(errno));
		return -1;
	}

	if ((size_t)len >= guid_size) {
		errno = ENAMETOOLONG;
		log_error(errno, "GUID too long in '%s'.\n", guid_link);
		return -1;
	}

	guid[len] = 0;

	/* validate that the GUID contains only safe characters: [A-Za-z0-9-{}] */
	if (windows_guid_is_valid(guid, len) != 0)
		return -1;

	return 0;
}

static int windows_rebuild_link(const struct fssnapshot_struct* fss, const char* name)
{
	char cmd[PS_CMD_MAX];
	char device_path[PATH_MAX];
	char target_path[PATH_MAX];
	char link_path[PATH_MAX];
	char guid_link[PATH_MAX];
	char guid[PATH_MAX];

	pathcpy(guid_link, sizeof(guid_link), fss->snapshot_dir);
	pathcat(guid_link, sizeof(guid_link), name);
	pathcat(guid_link, sizeof(guid_link), SNAPSHOT_GUID);

	pathcpy(link_path, sizeof(link_path), fss->snapshot_dir);
	pathcat(link_path, sizeof(link_path), name);

	int ret = windows_read_guid(guid_link, guid, sizeof(guid));
	if (ret != 0) {
		return -1;
	}

	/* if no GUID, assume it absent */
	if (guid[0] == 0) {
		/* remove stale links */
		windows_rmdir(link_path);
		return 0;
	}

	snprintf(cmd, sizeof(cmd),
		"(Get-WmiObject Win32_ShadowCopy | Where-Object {$_.ID -eq '%s'}).DeviceObject",
		guid);

	if (windows_ps(cmd, device_path, sizeof(device_path)) != 0) {
		log_error(errno, "Error getting DeviceObject from GUID '%s'. %s.\n", guid, strerror(errno));
		return -1;
	}

	/* if no device path, assume snapshot absent */
	if (device_path[0] == 0) {
		/* remove stale links */
		windows_rmdir(guid_link);
		windows_rmdir(link_path);
		return 0;
	}

	pathimport(target_path, sizeof(target_path), device_path);
	pathslash(target_path, sizeof(target_path));

	/* remove stale link if present */
	windows_rmdir(link_path);

	if (windows_symlink_directory(target_path, link_path) != 0) {
		log_error(errno, "Error creating symlink '%s'. %s.\n", link_path, strerror(errno));
		return -1;
	}

	return 0;
}

static int windows_delete_link(const struct fssnapshot_struct* fss, const char* name)
{
	char link_path[PATH_MAX];

	pathcpy(link_path, sizeof(link_path), fss->snapshot_dir);
	pathcat(link_path, sizeof(link_path), name);

	/* remove link if present */
	windows_rmdir(link_path);

	return 0;
}

int fssnapshot_mount(const char* dir, struct fssnapshot_struct* fss)
{
	wchar_t conv_buf_vol[CONV_MAX];
	char conv_buf_root[CONV_MAX];
	wchar_t volume_root[PATH_MAX];
	wchar_t volume_name[PATH_MAX];
	wchar_t fs_name[32];
	uint32_t magic;

	/*
	 * GetVolumePathNameW() accepts any path: file, directory, or deep
	 * subdirectory, and resolves it to the volume root (e.g. L"C:\").
	 * The result always carries a trailing backslash.
	 *
	 * Use convert_if_required() to avoid the automatic addition of \\?\
	 * made by convert() that is propagated in the resulting volume_root
	 */
	if (!GetVolumePathNameW(convert_if_required(conv_buf_vol, dir), volume_root, PATH_MAX)) {
		windows_errno(GetLastError());
		log_error(errno, "Error getting VolumeRoot from '%s'. %s.\n", dir, strerror(errno));
		return -1;
	}

	if (!GetVolumeInformationW(volume_root, 0, 0, 0, 0, 0, fs_name, 32)) {
		windows_errno(GetLastError());
		log_error(errno, "Error getting information of VolumeRoot '%s'. %s.\n", u16tou8(conv_buf_root, volume_root), strerror(errno));
		return -1;
	}
	if (wcscmp(fs_name, L"NTFS") == 0)
		magic = WINDOWS_NTFS_MAGIC;
	else
		return -1;         /* support only NTFS */

	/*
	 * Obtain the canonical volume name: \\?\Volume{GUID}\
	 *
	 * GetVolumePathNameW() may return a drive letter path ("C:\") or a
	 * directory mount point ("D:\Mount\").  Neither is guaranteed to be
	 * accepted by Win32_ShadowCopy.Create() on volumes without a drive
	 * letter.  GetVolumeNameForVolumeMountPointW() always returns the
	 * stable kernel-form GUID path that VSS requires, regardless of how
	 * many (or how few) mount points the volume has.
	 *
	 * We store it in fss->dataset.
	 */
	if (!GetVolumeNameForVolumeMountPointW(volume_root, volume_name, PATH_MAX)) {
		windows_errno(GetLastError());
		log_error(errno, "Error getting VolumeName of VolumeRoot '%s'. %s.\n", u16tou8(conv_buf_root, volume_root), strerror(errno));
		return -1;
	}

	/* don't use pathimport for dataset to keep backslashes */
	pathcpy(fss->dataset, sizeof(fss->dataset), u16tou8(conv_buf_root, volume_name));

	/* use pathimport to convert backslashes to slashes */
	pathimport(fss->root_dir, sizeof(fss->root_dir), u16tou8(conv_buf_root, volume_root));

	/* the returned root_dir should match the start of the passed dir */
	if (pathncmp(fss->root_dir, dir, strlen(fss->root_dir)) != 0) {
		errno = EINVAL;
		log_error(errno, "Not matching VolumeRoot '%s' for '%s'.\n", fss->root_dir, dir);
		return -1;
	}

	pathcpy(fss->snapshot_dir, sizeof(fss->snapshot_dir), fss->root_dir);
	pathcat(fss->snapshot_dir, sizeof(fss->snapshot_dir), SNAPSHOT_CONTAINER "/");

	if (windows_mkdir(fss->snapshot_dir) != 0 && errno != EEXIST) {
		log_error(errno, "Error creating directory '%s'. %s.\n", fss->snapshot_dir, strerror(errno));
		return -1;
	}

	/* refresh the links, after reboots they gets invalidated */
	if (windows_rebuild_link(fss, SNAPSHOT_PENDING) != 0) {
		return -1;
	}

	if (windows_rebuild_link(fss, SNAPSHOT_STABLE) != 0) {
		return -1;
	}

	fss->magic = magic;

	return 0;
}

int fssnapshot_stat(struct fssnapshot_struct* fss, const char* name, struct stat* st)
{
	char link_path[PATH_MAX];

	pathcpy(link_path, sizeof(link_path), fss->snapshot_dir);
	pathcat(link_path, sizeof(link_path), name);
	pathcat(link_path, sizeof(link_path), SNAPSHOT_GUID);         /* we check the GUID as it's the one that matter */

	/* use lstat because we want to check the link */
	if (windows_lstat(link_path, st) != 0)
		return -1;

	/* it must be a link to a directory */
	if (!S_ISLNKDIR(st->st_mode))
		return -1;

	return 0;
}

int fssnapshot_create(const struct fssnapshot_struct* fss, const char* name)
{
	char cmd[PS_CMD_MAX];
	char device_path[PATH_MAX];
	char target_path[PATH_MAX];
	char link_path[PATH_MAX];
	char guid_link[PATH_MAX];
	char out[PATH_MAX];

	/* create VSS shadow copy and capture the system-assigned GUID */
	snprintf(cmd, sizeof(cmd),
		"$r = (Get-WmiObject -List Win32_ShadowCopy).Create('%s', 'ClientAccessible'); "
		"if ($r.ReturnValue -eq 0) { "
		"Write-Output ('ID_' + $r.ShadowID); "
		"} else { "
		"Write-Output ('0x{0:X8}' -f $r.ReturnValue); "
		"}",
		fss->dataset);
	if (windows_ps(cmd, out, sizeof(out)) != 0) {
		log_error(errno, "Error creating snapshot of VolumeName '%s'. %s.\n", fss->dataset, strerror(errno));
		return -1;
	}
	if (strncmp(out, "ID_", 3) != 0) {
		errno = ENODATA;
		log_error(errno, "VSS snapshot creation of '%s' failed with error %s.\n", fss->dataset, out);
		return -1;
	}

	const char* guid = out + 3;

	if (windows_guid_is_valid(guid, strlen(guid)) != 0) {
		errno = EINVAL;
		log_error(errno, "Invalid GUID received from VSS shadow copy creation: '%s'\n", guid);
		return -1;
	}

	/* resolve the shadow copy's device object path */
	snprintf(cmd, sizeof(cmd), "(Get-WmiObject Win32_ShadowCopy | Where-Object {$_.ID -eq '%s'}).DeviceObject", guid);
	if (windows_ps(cmd, device_path, sizeof(device_path)) != 0) {
		log_error(errno, "Error getting the snapshot DeviceObject from GUID '%s'. %s.\n", guid, strerror(errno));
		goto bail_and_delete;
	}
	if (device_path[0] == 0) {
		errno = ENODATA;
		log_error(errno, "Empty snapshot DeviceObject of GUID '%s'. %s.\n", guid, strerror(errno));
		goto bail_and_delete;
	}

	/* use pathimport to convert backslashes to slashes */
	pathimport(target_path, sizeof(target_path), device_path);

	/* the ending slash is required, otherwise the link won't work */
	pathslash(target_path, sizeof(target_path));

	pathcpy(link_path, sizeof(link_path), fss->snapshot_dir);
	pathcat(link_path, sizeof(link_path), name);

	/* remove potential stale link */
	windows_rmdir(link_path);

	int ret = windows_symlink_directory(target_path, link_path);
	if (ret != 0) {
		log_error(errno, "Error creating symlink '%s'. %s.\n", link_path, strerror(errno));
		goto bail_and_delete;
	}

	pathcpy(guid_link, sizeof(guid_link), fss->snapshot_dir);
	pathcat(guid_link, sizeof(guid_link), name);
	pathcat(guid_link, sizeof(guid_link), SNAPSHOT_GUID);

	/* store GUID persistently as symlink target */
	ret = windows_symlink_directory(guid, guid_link);
	if (ret != 0) {
		log_error(errno, "Error creating GUID symlink '%s'. %s.\n", guid_link, strerror(errno));
		windows_rmdir(link_path);
		goto bail_and_delete;
	}

	return 0;

bail_and_delete:
	/* destroy the shadow copy we just created */
	snprintf(cmd, sizeof(cmd), "Get-WmiObject Win32_ShadowCopy | Where-Object {$_.ID -eq '%s'} | Remove-WmiObject", guid);
	windows_ps(cmd, 0, 0);
	return -1;
}

int fssnapshot_delete(const struct fssnapshot_struct* fss, const char* name)
{
	char guid[PATH_MAX];
	char cmd[PS_CMD_MAX];
	char guid_link[PATH_MAX];

	pathcpy(guid_link, sizeof(guid_link), fss->snapshot_dir);
	pathcat(guid_link, sizeof(guid_link), name);
	pathcat(guid_link, sizeof(guid_link), SNAPSHOT_GUID);

	if (windows_read_guid(guid_link, guid, sizeof(guid)) != 0)
		return -1;

	/* if the GUID link is gone, assume everything is gone */
	if (guid[0] == 0) {
		/* remove as not valid anymore */
		windows_delete_link(fss, name);
		return 0;
	}

	/* destroy the VSS shadow copy */
	snprintf(cmd, sizeof(cmd), "Get-WmiObject Win32_ShadowCopy | Where-Object {$_.ID -eq '%s'} | Remove-WmiObject", guid);
	int ret = windows_ps(cmd, 0, 0);
	if (ret != 0) {
		log_error(errno, "Error destroy the snapshot from GUID '%s'. %s.\n", guid, strerror(errno));
		/* remove as not valid anymore */
		windows_delete_link(fss, name);
		return -1;
	}

	/* remove live symlink if present */
	windows_delete_link(fss, name);

	/* remove persistent GUID symlink */
	if (windows_rmdir(guid_link) != 0) {
		log_error(errno, "Error rmdir '%s'. %s.\n", guid_link, strerror(errno));
		return -1;
	}

	return 0;
}

int fssnapshot_rename(const struct fssnapshot_struct* fss, const char* old_name, const char* new_name)
{
	char old_guid[PATH_MAX];
	char new_guid[PATH_MAX];

	pathcpy(old_guid, sizeof(old_guid), fss->snapshot_dir);
	pathcat(old_guid, sizeof(old_guid), old_name);
	pathcat(old_guid, sizeof(old_guid), SNAPSHOT_GUID);
	pathcpy(new_guid, sizeof(new_guid), fss->snapshot_dir);
	pathcat(new_guid, sizeof(new_guid), new_name);
	pathcat(new_guid, sizeof(new_guid), SNAPSHOT_GUID);

	if (windows_rename(old_guid, new_guid) != 0) {
		log_error(errno, "Error renaming  '%s' to '%s'. %s.\n", old_guid, new_guid, strerror(errno));
		return -1;
	}

	/* the rename is the latest operation, and then we don't care about the links, we just remove both */
	windows_delete_link(fss, old_name);
	windows_delete_link(fss, new_name);

	return 0;
}

void fssnapshot_unmount(const struct fssnapshot_struct* fss)
{
	/*
	 * Remove symlinks on exit to prevent "Dangling Links."
	 * Since \Device\HarddiskVolumeShadowCopyN paths are reassigned by the kernel
	 * at boot, a stale link would either point to a non-existent device or,
	 * worse, a different disk's snapshot.
	 *
	 * Deleting the link ensures system hygiene and prevents users from seeing
	 * "Location not available" errors or incorrect data after a reboot.
	 */
	windows_delete_link(fss, SNAPSHOT_PENDING);
	windows_delete_link(fss, SNAPSHOT_STABLE);
}

/****************************************************************************/
/* resolve */

/**
 * Get the device file from a path inside the device.
 */
static int devresolve(const char* mount, char* file, size_t file_size, char* wfile, size_t wfile_size)
{
	wchar_t conv_buf_mount[CONV_MAX];
	char conv_buf_volume_guid[CONV_MAX];
	WCHAR volume_mount[PATH_MAX];
	WCHAR volume_guid[PATH_MAX];
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

	/*
	 * Remove the final slash, otherwise CreateFile() opens the file-system
	 * and not the volume
	 */
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

/****************************************************************************/
/* uuid */

int devuuid(uint64_t device_id, const char* device_path, char* uuid, size_t uuid_size)
{
	(void)device_path;

	/* just use the volume serial number returned in the device parameter */
	snprintf(uuid, uuid_size, "%08x", (unsigned)device_id);

	log_tag("uuid:windows:%u:%s:\n", (unsigned)device_id, uuid);

	return 0;
}

/****************************************************************************/
/* dev */

/**
 * Read a device tree filling the specified list of disk_t entries.
 */
static int devtree(devinfo_t* parent, tommy_list* list)
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
	h = CreateFileW(convert(conv_buf, parent->wfile), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, 0);
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
			return -1;
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
		free(vde_alloc);
		windows_errno(error);
		return -1;
	}

	for (i = 0; i < vde->NumberOfDiskExtents; ++i) {
		devinfo_t* devinfo;

		devinfo = calloc_nofail(1, sizeof(devinfo_t));

		pathcpy(devinfo->name, sizeof(devinfo->name), parent->name);
		pathcpy(devinfo->smartctl, sizeof(devinfo->smartctl), parent->smartctl);
		memcpy(devinfo->smartignore, parent->smartignore, sizeof(devinfo->smartignore));
		devinfo->device = vde->Extents[i].DiskNumber;
		pathprint(devinfo->file, sizeof(devinfo->file), "/dev/pd%" PRIu64, devinfo->device);
		pathprint(devinfo->wfile, sizeof(devinfo->wfile), "\\\\.\\PhysicalDrive%" PRIu64, devinfo->device);
		devinfo->parent = parent;

		/* insert in the list */
		tommy_list_insert_tail(list, &devinfo->node, devinfo);
	}

	if (!CloseHandle(h)) {
		windows_errno(GetLastError());
		free(vde_alloc);
		return -1;
	}

	free(vde_alloc);

	return 0;
}

/**
 * Compute disk usage by aggregating access statistics.
 *
 * On many Windows versions, these raw disk performance counters are disabled by default to save overhead.
 *
 * If DeviceIoControl call with IOCTL_DISK_PERFORMANCE returns zeros or fails, the counters are likely disabled.
 *
 * Run the following command from an elevated command prompt:
 *
 *   diskperf -y
 *
 * This change usually requires a reboot to take effect at the driver level.
 */
static int devstat(uint64_t device, const char* name, const char* wfile, uint64_t* count)
{
	wchar_t conv_buf[CONV_MAX];
	HANDLE h;
	BOOL ret;
	DISK_PERFORMANCE ds;
	DWORD bytes;
	char file[128];

	snprintf(file, sizeof(file), "/dev/pd%" PRIu64, device);

	/* open the volume */
	h = CreateFileW(convert(conv_buf, wfile), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, 0);
	if (h == INVALID_HANDLE_VALUE) {
		DWORD error = GetLastError();
		windows_errno(error);
		log_tag("device:%s:%s:error:%lu\n", file, name, error);
		return -1;
	}

	bytes = 0;
	ret = DeviceIoControl(h, IOCTL_DISK_PERFORMANCE, NULL, 0, &ds, sizeof(ds), &bytes, NULL);
	if (!ret) {
		DWORD error = GetLastError();
		CloseHandle(h);
		windows_errno(error);
		log_tag("device:%s:%s:error:%lu\n", file, name, error);
		return -1;
	}

	if (!CloseHandle(h)) {
		DWORD error = GetLastError();
		windows_errno(error);
		log_tag("device:%s:%s:error:%lu\n", file, name, error);
		return -1;
	}

	*count = ds.ReadCount + ds.WriteCount;

	return 0;
}

/**
 * Get SMART attributes.
 */
static int devsmart(uint64_t device, const char* name, const char* smartctl, const char* smartctl_info, struct smart_attr* smart, uint64_t* info, char* serial, char* family, char* model, char* inter)
{
	char conv_buf[CONV_MAX];
	WCHAR cmd[PATH_MAX + SMART_MAX];
	char file[128];
	FILE* f;
	int ret;
	int count;

	snprintf(file, sizeof(file), "/dev/pd%" PRIu64, device);

	const char* info_opts = smartctl_info[0] ? smartctl_info : "-a";

	/* if there is a custom smartctl command */
	if (smartctl[0]) {
		char option[SMART_MAX];
		snprintf(option, sizeof(option), smartctl, file);
		snwprintf(cmd, sizeof(cmd) / sizeof(cmd[0]), L"\"%lssmartctl.exe\" %s %s", windows_exedir(), info_opts, option);
	} else {
		snwprintf(cmd, sizeof(cmd) / sizeof(cmd[0]), L"\"%lssmartctl.exe\" %s %s", windows_exedir(), info_opts, file);
	}

	count = 0;

retry:
	log_tag("smartctl:%s:%s:run: %s\n", file, name, u16tou8(conv_buf, cmd));

	f = _wpopen(cmd, L"rt");
	if (!f) {
		/* LCOV_EXCL_START */
		log_tag("device:%s:%s:shell\n", file, name);
		log_fatal(errno, "Failed to run '%s' (from popen).\n", u16tou8(conv_buf, cmd));
		return -1;
		/* LCOV_EXCL_STOP */
	}

	if (smartctl_attribute(f, file, name, smart, info, serial, family, model, inter) != 0) {
		/* LCOV_EXCL_START */
		pclose(f);
		log_tag("device:%s:%s:shell\n", file, name);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	ret = pclose(f);

	log_tag("smartctl:%s:%s:ret: %x\n", file, name, ret);

	if (ret == -1) {
		/* LCOV_EXCL_START */
		log_tag("device:%s:%s:shell\n", file, name);
		log_fatal(errno, "Failed to run '%s' (from pclose).\n", u16tou8(conv_buf, cmd));
		return -1;
		/* LCOV_EXCL_STOP */
	}

	/* if first try without custom smartctl command */
	if (count == 0 && smartctl[0] == 0) {
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
			&& smart[SMART_POWER_ON_HOURS].raw == SMART_UNASSIGNED
			&& info[INFO_SIZE] == SMART_UNASSIGNED
			&& info[INFO_ROTATION_RATE] == SMART_UNASSIGNED
		) {
			/* retry using the "sat" type */
			snwprintf(cmd, sizeof(cmd) / sizeof(cmd[0]), L"\"%lssmartctl.exe\" %s -d sat %s", windows_exedir(), info_opts, file);

			++count;
			goto retry;
		}
	}

	/* store the smartctl return value */
	smart[SMART_FLAGS].raw = ret;

	return 0;
}

static int serial_descriptor(PSTORAGE_DEVICE_DESCRIPTOR descriptor, size_t size, char* output, size_t output_size)
{
	if (descriptor->SerialNumberOffset == 0
		|| descriptor->SerialNumberOffset == (DWORD)-1
		|| descriptor->SerialNumberOffset >= size)
		return -1;

	const char* raw = (const char*)descriptor + descriptor->SerialNumberOffset;
	size_t len = 0;

	/* find the length, not assuming a 0 ending */
	while (descriptor->SerialNumberOffset + len < size && raw[len] != 0)
		++len;

	if (len < 6) /* a serial cannot be so short */
		return -1;

	if (len + 1 > output_size)
		return -1;

	/* all must be printable */
	for (size_t i = 0; i < len; ++i) {
		if (!isprint((unsigned char)raw[i]))
			return -1;
	}

	memcpy(output, raw, len);
	output[len] = 0;

	strtrim(output);

	return 0;
}

static void devattr_property(HANDLE h, char* serial, char* model, char* inter)
{
	STORAGE_PROPERTY_QUERY query = { 0 };
	STORAGE_DESCRIPTOR_HEADER header = { 0 };
	DWORD bytes;

	query.PropertyId = StorageDeviceProperty;
	query.QueryType = PropertyStandardQuery;
	if (!DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), &header, sizeof(header), &bytes, 0))
		return;
	if (header.Size == 0)
		return;

	/* allocate buffer and query full descriptor */
	BYTE* buffer = malloc(header.Size);
	if (!buffer)
		return;

	if (!DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), buffer, header.Size, &bytes, 0)) {
		free(buffer);
		return;
	}

	STORAGE_DEVICE_DESCRIPTOR* desc = (STORAGE_DEVICE_DESCRIPTOR*)buffer;

	/* extract strings (offsets are from start of descriptor) */
	if (model && *model == 0 && desc->ProductIdOffset) {
		snprintf(model, SMART_MAX, "%s", (char*)(buffer + desc->ProductIdOffset));
		strtrim(model);
	}

	if (serial && *serial == 0) {
		/*
		 * This is the RAW serial, maybe HEX encoded, maybe BYTES SWAPPED,
		 * but we don't care because we just need any identifier.
		 *
		 * If smartctl was not able to read it, we now accept anything.
		 */
		serial_descriptor(desc, bytes, serial, SMART_MAX);
	}

#define BusTypeVirtual            0x0E
#define BusTypeFileBackedVirtual  0x0F
#define BusTypeSpaces             0x10
#define BusTypeNvme               0x11
#define BusTypeSCM                0x12
#define BusTypeUfs                0x13
#define BusTypeNvmeof             0x14

	const char* type = 0;
	switch ((int)desc->BusType) { /* cast to int to avoid warnings about enum unlisted */
	case BusTypeScsi : type = "SCSI"; break;
	case BusTypeAtapi : type = "ATAPI"; break;
	case BusTypeAta : type = "ATA"; break;
	case BusType1394 : type = "FireWire"; break;
	case BusTypeSsa : type = "SSA"; break;
	case BusTypeFibre : type = "Fibre"; break;
	case BusTypeUsb : type = "USB"; break;
	case BusTypeRAID : type = "RAID"; break;
	case BusTypeiScsi : type = "iSCSI"; break;
	case BusTypeSas : type = "SAS"; break;
	case BusTypeSata : type = "SATA"; break;
	case BusTypeSd : type = "SD"; break; /* Secure Digital cards */
	case BusTypeMmc : type = "MMC"; break; /* NAND Embedded MultiMediaCards (eMMC) */
	case BusTypeVirtual : type = "Virtual"; break;
	case BusTypeFileBackedVirtual : type = "Virtual"; break;
	case BusTypeSpaces : type = "Storage Spaces"; break;
	case BusTypeNvme : type = "NVMe"; break; /* NAND */
	case BusTypeSCM : type = "SCM"; break; /* NAND Storage Class Memory */
	case BusTypeUfs : type = "UFS"; break; /* NAND Universal Flash Storage */
	case BusTypeNvmeof : type = "NVMe"; break; /* NAND NVMe over Fabrics */
	default :
	}
	if (inter && type) /* always override interface as more detailed than smartctl */
		snprintf(inter, SMART_MAX, "%s", type);

	free(buffer);
}

static void devattr_size(HANDLE h, uint64_t* size)
{
	DISK_GEOMETRY_EX geom = { 0 };
	DWORD bytes;
	if (!DeviceIoControl(h, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, 0, 0, &geom, sizeof(geom), &bytes, 0))
		return;

	*size = geom.DiskSize.QuadPart;
}

static void devattr_rotational(HANDLE h, uint64_t* rotational)
{
	STORAGE_PROPERTY_QUERY query = { 0 };
	DEVICE_SEEK_PENALTY_DESCRIPTOR desc = { 0 };
	DWORD bytes;

	query.PropertyId = StorageDeviceSeekPenaltyProperty;
	query.QueryType = PropertyStandardQuery;
	if (!DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), &desc, sizeof(desc), &bytes, 0))
		return;
	if (bytes < sizeof(desc))
		return;
	if (desc.Version != sizeof(DEVICE_SEEK_PENALTY_DESCRIPTOR) || desc.Size != sizeof(DEVICE_SEEK_PENALTY_DESCRIPTOR))
		return;

	*rotational = desc.IncursSeekPenalty ? 1 : 0;
}

/**
 * Get device attributes.
 */
static void devattr(uint64_t device, const char* name, const char* wfile, uint64_t* info, char* serial, char* family, char* model, char* interf)
{
	HANDLE h;
	wchar_t conv_buf[CONV_MAX];
	char file[128];

	snprintf(file, sizeof(file), "/dev/pd%" PRIu64, device);

	(void)family; /* not available, smartctl uses an internal database to get it */

	/* open the volume */
	h = CreateFileW(convert(conv_buf, wfile), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, 0);
	if (h == INVALID_HANDLE_VALUE) {
		DWORD error = GetLastError();
		windows_errno(error);
		log_tag("device:%s:%s:error:%lu\n", file, name, error);
		return;
	}

	if (info[INFO_SIZE] == SMART_UNASSIGNED)
		devattr_size(h, &info[INFO_SIZE]);

	if (*model == 0 || *serial == 0 || *interf == 0)
		devattr_property(h, serial, model, interf);

	/* set NVMe as not rotational */
	if (info[INFO_ROTATION_RATE] == SMART_UNASSIGNED && strcasecmp(interf, "nvme") == 0)
		info[INFO_ROTATION_RATE] = 0;

	/* set SSD as not rotational */
	if (info[INFO_ROTATION_RATE] == SMART_UNASSIGNED)
		devattr_rotational(h, &info[INFO_ROTATION_RATE]);

	if (!CloseHandle(h)) {
		DWORD error = GetLastError();
		windows_errno(error);
		log_tag("device:%s:%s:error:%lu\n", file, name, error);
		return;
	}
}

/**
 * Check if the device needs power management (it's a rotational one)
 */
static int devpower(uint64_t device, const char* name, const char* wfile)
{
	HANDLE h;
	wchar_t conv_buf[CONV_MAX];
	char file[128];
	uint64_t rotational;
	char interf[SMART_MAX];

	rotational = SMART_UNASSIGNED;
	interf[0] = 0;
	snprintf(file, sizeof(file), "/dev/pd%" PRIu64, device);

	/* open the volume */
	h = CreateFileW(convert(conv_buf, wfile), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, 0);
	if (h == INVALID_HANDLE_VALUE) {
		DWORD error = GetLastError();
		windows_errno(error);
		log_tag("device:%s:%s:error:%lu\n", file, name, error);
		return 0; /* assume not rotational */
	}

	/* get the interface */
	devattr_property(h, 0, 0, interf);

	/* set NVMe as not rotational */
	if (strcasecmp(interf, "nvme") == 0)
		rotational = 0;

	/* set SSD as not rotational */
	if (rotational == SMART_UNASSIGNED)
		devattr_rotational(h, &rotational);

	if (!CloseHandle(h)) {
		DWORD error = GetLastError();
		windows_errno(error);
		log_tag("device:%s:%s:error:%lu\n", file, name, error);
		return 0; /* assume not rotational */
	}

	if (rotational == SMART_UNASSIGNED)
		return 0; /* assume not rotational */

	return rotational != 0;
}

/**
 * Get POWER state
 */
static int devprobe(uint64_t device, const char* name, const char* smartctl, const char* smartctl_info, int* power, struct smart_attr* smart, uint64_t* info, char* serial, char* family, char* model, char* interf)
{
	char conv_buf[CONV_MAX];
	WCHAR cmd[PATH_MAX + SMART_MAX];
	char file[128];
	FILE* f;
	int ret;
	int count;

	snprintf(file, sizeof(file), "/dev/pd%" PRIu64, device);

	const char* info_opts = smartctl_info[0] ? smartctl_info : "-a";

	/* if there is a custom smartctl command */
	if (smartctl[0]) {
		char option[SMART_MAX];
		snprintf(option, sizeof(option), smartctl, file);
		snwprintf(cmd, sizeof(cmd) / sizeof(cmd[0]), L"\"%lssmartctl.exe\" -n standby,3 %s %s", windows_exedir(), info_opts, option);
	} else {
		snwprintf(cmd, sizeof(cmd) / sizeof(cmd[0]), L"\"%lssmartctl.exe\" -n standby,3 %s %s", windows_exedir(), info_opts, file);
	}

	count = 0;

retry:
	log_tag("smartctl:%s:%s:run: %s\n", file, name, u16tou8(conv_buf, cmd));

	f = _wpopen(cmd, L"rt");
	if (!f) {
		/* LCOV_EXCL_START */
		log_tag("device:%s:%s:shell\n", file, name);
		log_fatal(errno, "Failed to run '%s' (from popen).\n", u16tou8(conv_buf, cmd));
		return -1;
		/* LCOV_EXCL_STOP */
	}

	if (smartctl_attribute(f, file, name, smart, info, serial, family, model, interf) != 0) {
		/* LCOV_EXCL_START */
		pclose(f);
		log_tag("device:%s:%s:shell\n", file, name);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	ret = pclose(f);

	log_tag("smartctl:%s:%s:ret: %x\n", file, name, ret);

	if (ret == -1) {
		/* LCOV_EXCL_START */
		log_tag("device:%s:%s:shell\n", file, name);
		log_fatal(errno, "Failed to run '%s' (from pclose).\n", u16tou8(conv_buf, cmd));
		return -1;
		/* LCOV_EXCL_STOP */
	}

	/* if first try without custom smartctl command */
	if (count == 0 && smartctl[0] == 0) {
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
			snwprintf(cmd, sizeof(cmd) / sizeof(cmd[0]), L"\"%lssmartctl.exe\" -n standby,3 %s -d sat %s", windows_exedir(), info_opts, file);

			++count;
			goto retry;
		}
	}

	if (ret == 3) {
		log_tag("attr:%s:%s:power:standby\n", file, name);
		*power = POWER_STANDBY;
	} else {
		log_tag("attr:%s:%s:power:active\n", file, name);
		*power = POWER_ACTIVE;

		/* store the smartctl return value */
		if (smart)
			smart[SMART_FLAGS].raw = ret;
	}

	return 0;
}

/**
 * Spin down a specific device.
 */
static int devdown(uint64_t device, const char* name, const char* smartctl)
{
	char conv_buf[CONV_MAX];
	WCHAR cmd[PATH_MAX + SMART_MAX];
	char file[128];
	FILE* f;
	int ret;
	int count;

	snprintf(file, sizeof(file), "/dev/pd%" PRIu64, device);

	/* if there is a custom smartctl command */
	if (smartctl[0]) {
		char option[SMART_MAX];
		snprintf(option, sizeof(option), smartctl, file);
		snwprintf(cmd, sizeof(cmd) / sizeof(cmd[0]), L"\"%lssmartctl.exe\" -s standby,now %s", windows_exedir(), option);
	} else {
		snwprintf(cmd, sizeof(cmd) / sizeof(cmd[0]), L"\"%lssmartctl.exe\" -s standby,now %s", windows_exedir(), file);
	}

	count = 0;

retry:
	log_tag("smartctl:%s:%s:run: %s\n", file, name, u16tou8(conv_buf, cmd));

	f = _wpopen(cmd, L"rt");
	if (!f) {
		/* LCOV_EXCL_START */
		log_tag("device:%s:%s:shell\n", file, name);
		log_fatal(errno, "Failed to run '%s' (from popen).\n", u16tou8(conv_buf, cmd));
		return -1;
		/* LCOV_EXCL_STOP */
	}

	if (smartctl_flush(f, file, name) != 0) {
		/* LCOV_EXCL_START */
		pclose(f);
		log_tag("device:%s:%s:shell\n", file, name);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	ret = pclose(f);

	log_tag("smartctl:%s:%s:ret: %x\n", file, name, ret);

	if (ret == -1) {
		/* LCOV_EXCL_START */
		log_tag("device:%s:%s:shell\n", file, name);
		log_fatal(errno, "Failed to run '%s' (from pclose).\n", u16tou8(conv_buf, cmd));
		return -1;
		/* LCOV_EXCL_STOP */
	}

	/* if first try without custom smartctl command */
	if (count == 0 && smartctl[0] == 0) {
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
			snwprintf(cmd, sizeof(cmd) / sizeof(cmd[0]), L"\"%lssmartctl.exe\" -s standby,now -d sat %s", windows_exedir(), file);

			++count;
			goto retry;
		}
	}

	if (ret != 0) {
		/* LCOV_EXCL_START */
		log_tag("device:%s:%s:exit:%d\n", file, name, ret);
		log_fatal(errno, "Failed to run '%s' with return code %xh.\n", u16tou8(conv_buf, cmd), ret);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	log_tag("attr:%s:%s:power:down\n", file, name);

	return 0;
}

/**
 * Spin down a specific device if it's up
 */
static int devdownifup(uint64_t device, const char* name, const char* smartctl, const char* smartctl_info, int* power)
{
	*power = POWER_UNKNOWN;

	if (devprobe(device, name, smartctl, smartctl_info, power, 0, 0, 0, 0, 0, 0) != 0)
		return -1;

	if (*power == POWER_ACTIVE)
		return devdown(device, name, smartctl);

	return 0;
}

/**
 * Spin up a device.
 */
static int devup(uint64_t device, const char* name, const char* wfile)
{
	wchar_t conv_buf[CONV_MAX];
	HANDLE h;
	BOOL ret;
	DISK_GEOMETRY dg;
	DWORD bytes;
	void* buffer;
	char file[128];

	snprintf(file, sizeof(file), "/dev/pd%" PRIu64, device);

	/* open the volume */
	h = CreateFileW(convert(conv_buf, wfile), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH, 0);
	if (h == INVALID_HANDLE_VALUE) {
		DWORD error = GetLastError();
		windows_errno(error);
		log_tag("device:%s:%s:error:%lu\n", file, name, error);
		return -1;
	}

	bytes = 0;
	ret = DeviceIoControl(h, IOCTL_DISK_GET_DRIVE_GEOMETRY, NULL, 0, &dg, sizeof(dg), &bytes, NULL);
	if (!ret) {
		DWORD error = GetLastError();
		CloseHandle(h);
		windows_errno(error);
		log_tag("device:%s:%s:error:%lu\n", file, name, error);
		return -1;
	}

	buffer = VirtualAlloc(NULL, dg.BytesPerSector, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!buffer) {
		DWORD error = GetLastError();
		CloseHandle(h);
		windows_errno(error);
		log_tag("device:%s:%s:error:%lu\n", file, name, error);
		return -1;
	}

	bytes = 0;
	if (!ReadFile(h, buffer, dg.BytesPerSector, &bytes, NULL)) {
		DWORD error = GetLastError();
		CloseHandle(h);
		VirtualFree(buffer, 0, MEM_RELEASE);
		windows_errno(error);
		log_tag("device:%s:%s:error:%lu\n", file, name, error);
		return -1;
	}

	if (!CloseHandle(h)) {
		DWORD error = GetLastError();
		VirtualFree(buffer, 0, MEM_RELEASE);
		windows_errno(error);
		log_tag("device:%s:%s:error:%lu\n", file, name, error);
		return -1;
	}

	VirtualFree(buffer, 0, MEM_RELEASE);

	log_tag("attr:%s:%s:power:up\n", file, name);

	return 0;
}

/**
 * Thread for spinning up.
 */
static void* thread_spinup(void* arg)
{
	devinfo_t* devinfo = arg;
	uint64_t start;

	/* skip not rotational devices */
	if (devpower(devinfo->device, devinfo->name, devinfo->wfile) == 0)
		return (void*)-1;

	start = os_tick_ms();

	if (devup(devinfo->device, devinfo->name, devinfo->wfile) != 0) {
		/* LCOV_EXCL_START */
		return (void*)-1;
		/* LCOV_EXCL_STOP */
	}

	msg_status("Spunup device '%s' for disk '%s' in %" PRIu64 " ms.\n", devinfo->file, devinfo->name, os_tick_ms() - start);

	/* after the spin up, get SMART info */
	if (devsmart(devinfo->device, devinfo->name, devinfo->smartctl, devinfo->smartctl_info, devinfo->smart, devinfo->info, devinfo->serial, devinfo->family, devinfo->model, devinfo->interf) != 0) {
		/* LCOV_EXCL_START */
		return (void*)-1;
		/* LCOV_EXCL_STOP */
	}

	/*
	 * Retrieve some attributes directly from the system.
	 *
	 * smartctl intentionally skips queries on devices in standby mode
	 * to prevent accidentally spinning them up.
	 */
	devattr(devinfo->device, devinfo->name, devinfo->wfile, devinfo->info, devinfo->serial, devinfo->family, devinfo->model, devinfo->interf);

	return 0;
}

/**
 * Thread for spinning down.
 */
static void* thread_spindown(void* arg)
{
	devinfo_t* devinfo = arg;
	uint64_t start;

	/* skip not rotational devices */
	if (devpower(devinfo->device, devinfo->name, devinfo->wfile) == 0)
		return (void*)-1;

	start = os_tick_ms();

	if (devdown(devinfo->device, devinfo->name, devinfo->smartctl) != 0) {
		/* LCOV_EXCL_START */
		return (void*)-1;
		/* LCOV_EXCL_STOP */
	}

	msg_status("Spundown device '%s' for disk '%s' in %" PRIu64 " ms.\n", devinfo->file, devinfo->name, os_tick_ms() - start);

	return 0;
}

/**
 * Thread for spinning down.
 */
static void* thread_spindownifup(void* arg)
{
	devinfo_t* devinfo = arg;
	uint64_t start;
	int power;

	start = os_tick_ms();

	if (devdownifup(devinfo->device, devinfo->name, devinfo->smartctl, devinfo->smartctl_info, &power) != 0) {
		/* LCOV_EXCL_START */
		return (void*)-1;
		/* LCOV_EXCL_STOP */
	}

	if (power == POWER_ACTIVE)
		msg_status("Spundown device '%s' for disk '%s' in %" PRIu64 " ms.\n", devinfo->file, devinfo->name, os_tick_ms() - start);

	return 0;
}

/**
 * Thread for getting smart info.
 */
static void* thread_smart(void* arg)
{
	devinfo_t* devinfo = arg;

	if (devsmart(devinfo->device, devinfo->name, devinfo->smartctl, devinfo->smartctl_info, devinfo->smart, devinfo->info, devinfo->serial, devinfo->family, devinfo->model, devinfo->interf) != 0) {
		/* LCOV_EXCL_START */
		return (void*)-1;
		/* LCOV_EXCL_STOP */
	}

	/*
	 * Retrieve some attributes directly from the system.
	 *
	 * smartctl intentionally skips queries on devices in standby mode
	 * to prevent accidentally spinning them up.
	 */
	devattr(devinfo->device, devinfo->name, devinfo->wfile, devinfo->info, devinfo->serial, devinfo->family, devinfo->model, devinfo->interf);

	return 0;
}

/**
 * Thread for getting power info.
 */
static void* thread_probe(void* arg)
{
	devinfo_t* devinfo = arg;

	if (devprobe(devinfo->device, devinfo->name, devinfo->smartctl, devinfo->smartctl_info, &devinfo->power, devinfo->smart, devinfo->info, devinfo->serial, devinfo->family, devinfo->model, devinfo->interf) != 0) {
		/* LCOV_EXCL_START */
		return (void*)-1;
		/* LCOV_EXCL_STOP */
	}

	/*
	 * Retrieve some attributes directly from the system.
	 *
	 * smartctl intentionally skips queries on devices in standby mode
	 * to prevent accidentally spinning them up.
	 */
	devattr(devinfo->device, devinfo->name, devinfo->wfile, devinfo->info, devinfo->serial, devinfo->family, devinfo->model, devinfo->interf);

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

int devquery(tommy_list* high, tommy_list* low, int operation)
{
	tommy_node* i;
	void* (*func)(void* arg) = 0;

	/* for each device */
	for (i = tommy_list_head(high); i != 0; i = i->next) {
		devinfo_t* devinfo = i->data;
		uint64_t access_stat;

		if (devresolve(devinfo->mount, devinfo->file, sizeof(devinfo->file), devinfo->wfile, sizeof(devinfo->wfile)) != 0) {
			/* LCOV_EXCL_START */
			log_fatal(EEXTERNAL, "Failed to resolve path '%s'.\n", devinfo->mount);
			return -1;
			/* LCOV_EXCL_STOP */
		}

		/* retrieve access stat for the high level device */
		if (devstat(devinfo->device, devinfo->name, devinfo->wfile, &access_stat) == 0) {
			/* cumulate access stat in the first split */
			if (devinfo->split)
				devinfo->split->access_stat += access_stat;
			else
				devinfo->access_stat += access_stat;
		}

		/* expand the tree of devices */
		if (devtree(devinfo, low) != 0) {
			/* LCOV_EXCL_START */
			log_fatal(EEXTERNAL, "Failed to expand device '%s'.\n", devinfo->file);
			return -1;
			/* LCOV_EXCL_STOP */
		}
	}

	switch (operation) {
	case DEVICE_UP : func = thread_spinup; break;
	case DEVICE_DOWN : func = thread_spindown; break;
	case DEVICE_SMART : func = thread_smart; break;
	case DEVICE_PROBE : func = thread_probe; break;
	case DEVICE_DOWNIFUP : func = thread_spindownifup; break;
	}

	if (!func)
		return 0;

	return device_thread(low, func);
}

int devmap(void)
{
	for (int i = 0; i < 32; i++) {
		char wfile[PATH_MAX];
		wchar_t conv_buf[CONV_MAX];
		pathprint(wfile, sizeof(wfile), "\\\\.\\PhysicalDrive%d", i);
		HANDLE h;

		/* open the volume */
		h = CreateFileW(convert(conv_buf, wfile), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, 0);
		if (h == INVALID_HANDLE_VALUE) {
			DWORD error = GetLastError();
			if (error != ERROR_FILE_NOT_FOUND)
				log_tag("enumeration:/dev/pd%d::error:%lu\n", i, error);
			continue;
		}

		char serial[SMART_MAX];
		serial[0] = 0;
		devattr_property(h, serial, 0, 0);
		if (serial[0]) {
			log_tag("map:/dev/pd%d:%s\n", i, esc_tag(serial));
		}

		CloseHandle(h);
	}

	return 0;
}

/****************************************************************************/
/* temperature */

int ambient_temperature(void)
{
	return 0;
}

/****************************************************************************/
/* app */

void app_init(void)
{
	/*
	 * Set LC_ALL=C to make child ignoring the locale when printing info
	 *
	 * Note anyway that in Windows this trick doesn't work because the
	 * Windows locale is used with priority.
	 */
	_wputenv_s(L"LC_ALL", L"C");
}

void app_done(void)
{
}

void app_default_conf(char* conf, size_t conf_size, const char* argv0)
{
	char* slash;

	pathimport(conf, conf_size, argv0);

	slash = strrchr(conf, '/');
	if (slash) {
		slash[1] = 0;
		pathcat(conf, conf_size, PACKAGE ".conf");
	} else {
		pathcpy(conf, conf_size, PACKAGE ".conf");
	}
}

#endif

