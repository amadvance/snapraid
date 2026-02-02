/*
 * Copyright (C) 2013 Andrea Mazzoleni
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

#ifndef __MINGW32__ /* Only for Unix */

#include "support.h"

/**
 * Exit codes.
 */
int exit_success = 0;
int exit_failure = 1;
int exit_sync_needed = 2;

int open_noatime(const char* file, int flags)
{
#ifdef O_NOATIME
	int f = open(file, flags | O_NOATIME);

	/* only root is allowed to use O_NOATIME, in case retry without it */
	if (f == -1 && errno == EPERM)
		f = open(file, flags);
	return f;
#else
	return open(file, flags);
#endif
}

int dirent_hidden(struct dirent* dd)
{
	return dd->d_name[0] == '.';
}

const char* stat_desc(struct stat* st)
{
	if (S_ISREG(st->st_mode))
		return "regular";
	if (S_ISDIR(st->st_mode))
		return "directory";
	if (S_ISCHR(st->st_mode))
		return "character";
	if (S_ISBLK(st->st_mode))
		return "block-device";
	if (S_ISFIFO(st->st_mode))
		return "fifo";
	if (S_ISLNK(st->st_mode))
		return "link";
	if (S_ISLNK(st->st_mode))
		return "symbolic-link";
	if (S_ISSOCK(st->st_mode))
		return "socket";
	return "unknown";
}

static const char* smartctl_paths[] = {
	/* Linux & BSD */
	"/usr/sbin/smartctl",
	"/sbin/smartctl",
	"/usr/local/sbin/smartctl",
	"/usr/bin/smartctl",
	"/usr/local/bin/smartctl",
	/* macOS (Intel & Apple Silicon) */
	"/opt/homebrew/sbin/smartctl",
	0
};

const char* find_smartctl(void)
{
	int i;

	for (i = 0; smartctl_paths[i]; ++i) {
		if (access(smartctl_paths[i], X_OK) == 0)
			return smartctl_paths[i];
	}

	return 0;
}

/**
 * Read a file from sys
 *
 * Return -1 on error, otherwise the size of data read
 */
#if HAVE_LINUX_DEVICE
static int sysread(const char* path, char* buf, size_t buf_size)
{
	int f;
	int ret;
	int len;

	f = open(path, O_RDONLY);
	if (f == -1) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	len = read(f, buf, buf_size);
	if (len < 0) {
		/* LCOV_EXCL_START */
		close(f);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	ret = close(f);
	if (ret != 0) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	return len;
}
#endif

/**
 * Read a file from sys.
 * Trim spaces.
 * Always put an ending 0.
 * Do not report error on reading.
 *
 * Return 0 on error, otherwise a pointer to buf
 */
#if HAVE_LINUX_DEVICE
static char* sysattr(const char* path, char* buf, size_t buf_size)
{
	int len;

	len = sysread(path, buf, buf_size);
	if (len < 0) {
		/* LCOV_EXCL_START */
		return 0;
		/* LCOV_EXCL_STOP */
	}

	if ((size_t)len + 1 > buf_size) {
		/* LCOV_EXCL_START */
		return 0;
		/* LCOV_EXCL_STOP */
	}

	buf[len] = 0;

	strtrim(buf);

	return buf;
}
#endif

/**
 * Get the device file from the device number.
 *
 * It uses /proc/self/mountinfo.
 *
 * Null devices (major==0) are resolved to the device indicated in mountinfo.
 */
#if HAVE_LINUX_DEVICE
static int devresolve_proc(uint64_t device, char* path, size_t path_size)
{
	FILE* f;
	char match[32];

	/* generate the matching string */
	snprintf(match, sizeof(match), "%u:%u", major(device), minor(device));

	f = fopen("/proc/self/mountinfo", "r");
	if (!f) {
		log_tag("resolve:proc:%u:%u: failed to open /proc/self/mountinfo\n", major(device), minor(device));
		return -1;
	}

	/*
	 * mountinfo format
	 * 0 - mount ID
	 * 1 - parent ID
	 * 2 - major:minor
	 * 3 - root
	 * 4 - mount point
	 * 5 - options
	 * 6 - "-" (separator)
	 * 7 - fs
	 * 8 - mount source - /dev/device
	 */

	while (1) {
		char buf[256];
		char* first_map[8];
		unsigned first_mac;
		char* second_map[8];
		unsigned second_mac;
		char* s;
		struct stat st;
		char* separator;
		char* majorminor;
		char* mountpoint;
		char* fs;
		char* mountsource;

		s = fgets(buf, sizeof(buf), f);
		if (s == 0)
			break;

		/* find the separator position */
		separator = strstr(s, " - ");
		if (!separator)
			continue;

		/* skip the separator */
		*separator = 0;
		separator += 3;

		/* split the line */
		first_mac = strsplit(first_map, 8, s, " \t\r\n");
		second_mac = strsplit(second_map, 8, separator, " \t\r\n");

		/* if too short, it's the wrong line */
		if (first_mac < 5)
			continue;
		if (second_mac < 2)
			continue;

		majorminor = first_map[2];
		mountpoint = first_map[4];
		fs = second_map[0];
		mountsource = second_map[1];

		/* compare major:minor from mountinfo */
		if (strcmp(majorminor, match) == 0) {
			/*
			 * Accept only /dev/... mountsource
			 *
			 * This excludes ZFS that uses a bare label for mountsource, like "tank".
			 *
			 * 410 408 0:193 / /XXX rw,relatime shared:217 - zfs tank/system/data/var/lib/docker/XXX rw,xattr,noacl
			 *
			 * Also excludes AUTOFS unmounted devices that point to a fake filesystem
			 * used to remount them at the first use.
			 *
			 * 97 25 0:42 / /XXX rw,relatime shared:76 - autofs /etc/auto.seed rw,fd=6,pgrp=952,timeout=30,minproto=5,maxproto=5,indirect
			 */
			if (strncmp(mountsource, "/dev/", 5) != 0) {
				log_tag("resolve:proc:%u:%u: match skipped for not /dev/ mountsource for %s %s\n", major(device), minor(device), fs, mountsource);
				continue;
			}

			pathcpy(path, path_size, mountsource);

			log_tag("resolve:proc:%u:%u: found device %s matching device %s\n", major(device), minor(device), path, match);

			fclose(f);
			return 0;
		}

		/* get the device of the mount point */
		/* in Btrfs it could be different than the one in mountinfo */
		if (stat(mountpoint, &st) == 0 && st.st_dev == device) {
			if (strncmp(mountsource, "/dev/", 5) != 0) {
				log_tag("resolve:proc:%u:%u: match skipped for not /dev/ mountsource for %s %s\n", major(device), minor(device), fs, mountsource);
				continue;
			}

			pathcpy(path, path_size, mountsource);

			log_tag("resolve:proc:%u:%u: found device %s matching mountpoint %s\n", major(device), minor(device), path, mountpoint);

			fclose(f);
			return 0;
		}
	}

	log_tag("resolve:proc:%u:%u: not found\n", major(device), minor(device));

	fclose(f);
	return -1;
}
#endif

/**
 * Get the device of a virtual superblock.
 *
 * This is intended to resolve the case of Btrfs filesystems that
 * create a virtual superblock (major==0) not backed by any low
 * level device.
 *
 * See:
 * Bug 711881 - too funny btrfs st_dev numbers
 * https://bugzilla.redhat.com/show_bug.cgi?id=711881
 */
#if HAVE_LINUX_DEVICE
static int devdereference(uint64_t device, uint64_t* new_device)
{
	char path[PATH_MAX];
	struct stat st;

	/* use the proc interface to get the device containing the filesystem */
	if (devresolve_proc(device, path, sizeof(path)) != 0) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	/* check the device */
	if (stat(path, &st) != 0) {
		/* LCOV_EXCL_START */
		log_tag("dereference:%u:%u: failed to stat %s\n", major(device), minor(device), path);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	if (major(st.st_rdev) == 0) {
		/* LCOV_EXCL_START */
		log_tag("dereference:%u:%u: still null device %s -> %u:%u\n", major(device), minor(device), path, major(st.st_rdev), minor(st.st_rdev));
		return -1;
		/* LCOV_EXCL_STOP */
	}

	*new_device = st.st_rdev;
	log_tag("dereference:%u:%u: found %u:%u\n", major(device), minor(device), major(st.st_rdev), minor(st.st_rdev));
	return 0;
}
#endif

/**
 * Read a file extracting the specified tag TAG=VALUE format.
 * Return !=0 on error.
 */
#if HAVE_LINUX_DEVICE
static int tagread(const char* path, const char* tag, char* value, size_t value_size)
{
	int ret;
	char buf[512];
	size_t tag_len;
	char* i;
	char* e;

	ret = sysread(path, buf, sizeof(buf));
	if (ret < 0) {
		/* LCOV_EXCL_START */
		log_fatal(errno, "Failed to read '%s'.\n", path);
		return -1;
		/* LCOV_EXCL_STOP */
	}
	if ((size_t)ret + 1 > sizeof(buf)) {
		/* LCOV_EXCL_START */
		log_fatal(EEXTERNAL, "Too long read '%s'.\n", path);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	/* ending 0 */
	buf[ret] = 0;

	tag_len = strlen(tag);

	for (i = buf; *i; ++i) {
		char* p = i;

		/* start with a space */
		if (p != buf) {
			if (!isspace(*p))
				continue;
			++p;
		}

		if (strncmp(p, tag, tag_len) != 0)
			continue;
		p += tag_len;

		/* end with a = */
		if (*p != '=')
			continue;
		++p;

		/* found */
		i = p;
		break;
	}
	if (!*i) {
		/* LCOV_EXCL_START */
		log_fatal(EEXTERNAL, "Missing tag '%s' for '%s'.\n", tag, path);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	/* terminate at the first space */
	e = i;
	while (*e != 0 && !isspace(*e))
		++e;
	*e = 0;

	if (!*i) {
		/* LCOV_EXCL_START */
		log_fatal(EEXTERNAL, "Empty tag '%s' for '%s'.\n", tag, path);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	pathprint(value, value_size, "%s", i);

	return 0;
}
#endif

/**
 * Get the device file from the device number.
 *
 * It uses /sys/dev/block/.../uevent.
 *
 * For null device (major==0) it fails.
 */
#if HAVE_LINUX_DEVICE
static int devresolve_sys(dev_t device, char* path, size_t path_size)
{
	struct stat st;
	char buf[PATH_MAX];

	/* default device path from device number */
	pathprint(path, path_size, "/sys/dev/block/%u:%u/uevent", major(device), minor(device));

	if (tagread(path, "DEVNAME", buf, sizeof(buf)) != 0) {
		/* LCOV_EXCL_START */
		log_tag("resolve:sys:%u:%u: failed to read DEVNAME tag '%s'\n", major(device), minor(device), path);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	/* set the real device path */
	pathprint(path, path_size, "/dev/%s", buf);

	/* check the device */
	if (stat(path, &st) != 0) {
		/* LCOV_EXCL_START */
		log_tag("resolve:sys:%u:%u: failed to stat '%s'\n", major(device), minor(device), path);
		return -1;
		/* LCOV_EXCL_STOP */
	}
	if (st.st_rdev != device) {
		/* LCOV_EXCL_START */
		log_tag("resolve:sys:%u:%u: unexpected device '%u:%u' for '%s'.\n", major(device), minor(device), major(st.st_rdev), minor(st.st_rdev), path);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	log_tag("resolve:sys:%u:%u:%s: found\n", major(device), minor(device), path);

	return 0;
}
#endif

/**
 * Get the device file from the device number.
 */
#if HAVE_LINUX_DEVICE
static int devresolve(uint64_t device, char* path, size_t path_size)
{
	if (devresolve_sys(device, path, path_size) == 0)
		return 0;

	return -1;
}
#endif

/**
 * Cache used by blkid.
 */
#if HAVE_BLKID
static blkid_cache cache = 0;
#endif

/**
 * Get the UUID using the /dev/disk/by-uuid/ links.
 * It doesn't require root permission, and the uuid are always updated.
 * It doesn't work with Btrfs file-systems that don't export the main UUID
 * in /dev/disk/by-uuid/.
 */
#if HAVE_LINUX_DEVICE
static int devuuid_dev(uint64_t device, char* uuid, size_t uuid_size)
{
	int ret;
	DIR* d;
	struct dirent* dd;
	struct stat st;

	/* scan the UUID directory searching for the device */
	d = opendir("/dev/disk/by-uuid");
	if (!d) {
		log_tag("uuid:by-uuid:%u:%u: opendir(/dev/disk/by-uuid) failed, %s\n", major(device), minor(device), strerror(errno));
		/* directory missing?, likely we are not in Linux */
		return -1;
	}

	int dir_fd = dirfd(d);
	if (dir_fd == -1) {
		log_tag("uuid:by-uuid:%u:%u: dirfd(/dev/disk/by-uuid) failed, %s\n", major(device), minor(device), strerror(errno));
		return -1;
	}

	while ((dd = readdir(d)) != 0) {
		/* skip "." and ".." files, UUIDs never start with '.' */
		if (dd->d_name[0] == '.')
			continue;

		ret = fstatat(dir_fd, dd->d_name, &st, 0);
		if (ret != 0) {
			log_tag("uuid:by-uuid:%u:%u: fstatat(%s) failed, %s\n", major(device), minor(device), dd->d_name, strerror(errno));
			/* generic error, ignore and continue the search */
			continue;
		}

		/* if it matches, we have the uuid */
		if (S_ISBLK(st.st_mode) && st.st_rdev == (dev_t)device) {
			char buf[PATH_MAX];
			char path[PATH_MAX];

			/* resolve the link */
			pathprint(path, sizeof(path), "/dev/disk/by-uuid/%s", dd->d_name);
			ret = readlink(path, buf, sizeof(buf));
			if (ret < 0 || ret >= PATH_MAX) {
				log_tag("uuid:by-uuid:%u:%u: readlink(/dev/disk/by-uuid/%s) failed, %s\n", major(device), minor(device), dd->d_name, strerror(errno));
				/* generic error, ignore and continue the search */
				continue;
			}
			buf[ret] = 0;

			/* found */
			pathcpy(uuid, uuid_size, dd->d_name);

			log_tag("uuid:by-uuid:%u:%u:%s: found %s\n", major(device), minor(device), uuid, buf);

			closedir(d);
			return 0;
		}
	}

	log_tag("uuid:by-uuid:%u:%u: /dev/disk/by-uuid doesn't contain a matching block device\n", major(device), minor(device));

	/* not found */
	closedir(d);
	return -1;
}
#endif

/**
 * Get the UUID using libblkid.
 * It uses a cache to work without root permission, resulting in UUID
 * not necessarily recent.
 * We could call blkid_probe_all() to refresh the UUID, but it would
 * require root permission to read the superblocks, and resulting in
 * all the disks spinning.
 */
#if HAVE_BLKID
static int devuuid_blkid(uint64_t device, char* uuid, size_t uuid_size)
{
	char* devname;
	char* uuidname;

	devname = blkid_devno_to_devname(device);
	if (!devname) {
		log_tag("uuid:blkid:%u:%u: blkid_devno_to_devname() failed, %s\n", major(device), minor(device), strerror(errno));
		/* device mapping failed */
		return -1;
	}

	uuidname = blkid_get_tag_value(cache, "UUID", devname);
	if (!uuidname) {
		log_tag("uuid:blkid:%u:%u: blkid_get_tag_value(UUID,%s) failed, %s\n", major(device), minor(device), devname, strerror(errno));
		/* uuid mapping failed */
		free(devname);
		return -1;
	}

	pathcpy(uuid, uuid_size, uuidname);

	log_tag("uuid:blkid:%u:%u:%s: found %s\n", major(device), minor(device), uuid, devname);

	free(devname);
	free(uuidname);
	return 0;
}
#endif


/**
 * Get the LABEL using libblkid.
 * It uses a cache to work without root permission, resulting in LABEL
 * not necessarily recent.
 */
#if HAVE_BLKID
static int devuuid_label(uint64_t device, char* label, size_t label_size)
{
	char* devname;
	char* labelname;

	devname = blkid_devno_to_devname(device);
	if (!devname) {
		log_tag("label:blkid:%u:%u: blkid_devno_to_devname() failed, %s\n", major(device), minor(device), strerror(errno));
		/* device mapping failed */
		return -1;
	}

	labelname = blkid_get_tag_value(cache, "LABEL", devname);
	if (!labelname) {
		log_tag("label:blkid:%u:%u: blkid_get_tag_value(LABEL,%s) failed, %s\n", major(device), minor(device), devname, strerror(errno));
		free(devname);
		/* label mapping failed */
		return -1;
	}

	pathcpy(label, label_size, labelname);

	log_tag("label:blkid:%u:%u:%s: found %s\n", major(device), minor(device), label, devname);

	free(devname);
	free(labelname);
	return 0;
}
#endif


#ifdef __APPLE__
static int devuuid_darwin(const char* path, char* uuid, size_t uuid_size)
{
	CFStringRef path_apple = 0;
	DASessionRef session = 0;
	CFURLRef path_appler = 0;
	int result = -1;

	*uuid = 0;

	path_apple = CFStringCreateWithCString(kCFAllocatorDefault, path, kCFStringEncodingUTF8);
	if (!path_apple)
		goto bail;

	session = DASessionCreate(kCFAllocatorDefault);
	if (!session)
		goto bail;

	path_appler = CFURLCreateWithFileSystemPath(kCFAllocatorDefault, path_apple, kCFURLPOSIXPathStyle, false);
	if (!path_appler)
		goto bail;

	/* find the mount point */
	DADiskRef disk = NULL;
	while (path_appler) {
		disk = DADiskCreateFromVolumePath(kCFAllocatorDefault, session, path_appler);
		if (disk)
			break;

		CFURLRef parent = CFURLCreateCopyDeletingLastPathComponent(kCFAllocatorDefault, path_appler);
		if (!parent)
			break;

		/* check if we hit the root (parent == child) */
		if (CFEqual(parent, path_appler)) {
			CFRelease(parent);
			break;
		}

		CFRelease(path_appler);
		path_appler = parent;
	}

	/* safely extract the UUID */
	if (disk) {
		CFDictionaryRef description = DADiskCopyDescription(disk);
		if (description) {
			/* key might not exist for NTFS/ExFAT on some drivers */
			const void* value = (CFUUIDRef)CFDictionaryGetValue(description, kDADiskDescriptionVolumeUUIDKey);
			CFStringRef uuid_string = NULL;

			if (value) {
				if (CFGetTypeID(value) == CFUUIDGetTypeID()) {
					uuid_string = CFUUIDCreateString(kCFAllocatorDefault, (CFUUIDRef)value);
				} else if (CFGetTypeID(value) == CFStringGetTypeID()) {
					/* if it's already a string, retain it so we can release it later consistently */
					uuid_string = (CFStringRef)CFRetain(value);
				}
			}

			if (uuid_string) {
				if (CFStringGetCString(uuid_string, uuid, uuid_size, kCFStringEncodingUTF8)) {
					result = 0; /* success */
				}
				CFRelease(uuid_string);
			}

			CFRelease(description);
		}
		CFRelease(disk);
	}

bail:
	/* clean up */
	if (path_appler) CFRelease(path_appler);
	if (session) CFRelease(session);
	if (path_apple) CFRelease(path_apple);

	return result;
}
#endif

int devuuid(uint64_t device, const char* path, char* uuid, size_t uuid_size)
{
	(void)path;
	(void)device;

#ifdef __APPLE__
	if (devuuid_darwin(path, uuid, uuid_size) == 0)
		return 0;
#endif

#if HAVE_LINUX_DEVICE
	/* if the major is the null device */
	if (major(device) == 0) {
		/* obtain the real device */
		if (devdereference(device, &device) != 0) {
			/* LCOV_EXCL_START */
			return -1;
			/* LCOV_EXCL_STOP */
		}
	}
#endif

	/* first try with the /dev/disk/by-uuid version */
#if HAVE_LINUX_DEVICE
	if (devuuid_dev(device, uuid, uuid_size) == 0)
		return 0;
#else
	log_tag("uuid:by-uuid:%u:%u: by-uuid not supported\n", major(device), minor(device));
#endif

	/* fall back to blkid for other cases */
#if HAVE_BLKID
	if (devuuid_blkid(device, uuid, uuid_size) == 0)
		return 0;
#else
	log_tag("uuid:blkid:%u:%u: blkid support not compiled in\n", major(device), minor(device));
#endif

	log_tag("uuid:notfound:%u:%u:\n", major(device), minor(device));

	/* not supported */
	(void)uuid;
	(void)uuid_size;
	return -1;
}

int filephy(const char* path, uint64_t size, uint64_t* physical)
{
#if HAVE_LINUX_FIEMAP_H
	/* In Linux get the real physical address of the file */
	/* Note that FIEMAP doesn't require root permission */
	int f;
	struct {
		struct fiemap fiemap;
		struct fiemap_extent extent;
	} fm;
	unsigned int blknum;

	f = open(path, O_RDONLY);
	if (f == -1) {
		return -1;
	}

	/* first try with FIEMAP */
	/* if works for ext2, ext3, ext4, xfs, btrfs */
	memset(&fm, 0, sizeof(fm));
	fm.fiemap.fm_start = 0;
	fm.fiemap.fm_length = ~0ULL;
	fm.fiemap.fm_flags = FIEMAP_FLAG_SYNC; /* required to ensure that just created files report a valid address and not 0 */
	fm.fiemap.fm_extent_count = 1; /* we are interested only at the first block */

	if (ioctl(f, FS_IOC_FIEMAP, &fm) != -1) {
		uint32_t flags = fm.fiemap.fm_extents[0].fe_flags;
		uint64_t offset = fm.fiemap.fm_extents[0].fe_physical;

		/* check some condition for validating the offset */
		if (flags & FIEMAP_EXTENT_DATA_INLINE) {
			/* if the data is inline, we don't have an offset to report */
			*physical = FILEPHY_WITHOUT_OFFSET;
		} else if (flags & FIEMAP_EXTENT_UNKNOWN) {
			/* if the offset is unknown, we don't have an offset to report */
			*physical = FILEPHY_WITHOUT_OFFSET;
		} else if (offset == 0) {
			/* 0 is the general fallback for file-systems when */
			/* they don't have an offset to report */
			*physical = FILEPHY_WITHOUT_OFFSET;
		} else {
			/* finally report the real offset */
			*physical = offset + FILEPHY_REAL_OFFSET;
		}

		if (close(f) == -1)
			return -1;
		return 0;
	}

	/* if the file is empty, FIBMAP doesn't work, and we don't even try to use it */
	if (size == 0) {
		*physical = FILEPHY_WITHOUT_OFFSET;
		if (close(f) == -1)
			return -1;
		return 0;
	}

	/* then try with FIBMAP */
	/* it works for jfs, reiserfs, ntfs-3g */
	/* in exfat it always returns 0, that it's anyway better than the fake inodes */
	blknum = 0; /* first block */
	if (ioctl(f, FIBMAP, &blknum) != -1) {
		*physical = blknum + FILEPHY_REAL_OFFSET;
		if (close(f) == -1)
			return -1;
		return 0;
	}

	/* otherwise don't use anything, and keep the directory traversal order */
	/* at now this should happen only for vfat */
	/* and it's surely better than using fake inodes */
	*physical = FILEPHY_UNREPORTED_OFFSET;
	if (close(f) == -1)
		return -1;
#else
	/* In a generic Unix use a dummy value for all the files */
	/* We don't want to risk to use the inode without knowing */
	/* if it really improves performance. */
	/* In this way we keep them in the directory traversal order */
	/* that at least keeps files in the same directory together. */
	/* Note also that in newer file-system with snapshot, like ZFS, */
	/* the inode doesn't represent even more the disk position, because files */
	/* are not overwritten in place, but rewritten in another location */
	/* of the disk. */
	*physical = FILEPHY_UNREPORTED_OFFSET;

	(void)path; /* not used here */
	(void)size;
#endif

	return 0;
}

/* from man statfs */
#define ADFS_SUPER_MAGIC 0xadf5
#define AFFS_SUPER_MAGIC 0xADFF
#define BDEVFS_MAGIC 0x62646576
#define BEFS_SUPER_MAGIC 0x42465331
#define BFS_MAGIC 0x1BADFACE
#define BINFMTFS_MAGIC 0x42494e4d
#define BTRFS_SUPER_MAGIC 0x9123683E
#define CGROUP_SUPER_MAGIC 0x27e0eb
#define CIFS_MAGIC_NUMBER 0xFF534D42
#define CODA_SUPER_MAGIC 0x73757245
#define COH_SUPER_MAGIC 0x012FF7B7
#define CRAMFS_MAGIC 0x28cd3d45
#define DEBUGFS_MAGIC 0x64626720
#define DEVFS_SUPER_MAGIC 0x1373
#define DEVPTS_SUPER_MAGIC 0x1cd1
#define EFIVARFS_MAGIC 0xde5e81e4
#define EFS_SUPER_MAGIC 0x00414A53
#define EXT_SUPER_MAGIC 0x137D
#define EXT2_OLD_SUPER_MAGIC 0xEF51
#define EXT4_SUPER_MAGIC 0xEF53 /* also ext2/ext3 */
#define FUSE_SUPER_MAGIC 0x65735546
#define FUTEXFS_SUPER_MAGIC 0xBAD1DEA
#define HFS_SUPER_MAGIC 0x4244
#define HFSPLUS_SUPER_MAGIC 0x482b
#define HOSTFS_SUPER_MAGIC 0x00c0ffee
#define HPFS_SUPER_MAGIC 0xF995E849
#define HUGETLBFS_MAGIC 0x958458f6
#define ISOFS_SUPER_MAGIC 0x9660
#define JFFS2_SUPER_MAGIC 0x72b6
#define JFS_SUPER_MAGIC 0x3153464a
#define MINIX_SUPER_MAGIC 0x137F
#define MINIX_SUPER_MAGIC2 0x138F
#define MINIX2_SUPER_MAGIC 0x2468
#define MINIX2_SUPER_MAGIC2 0x2478
#define MINIX3_SUPER_MAGIC 0x4d5a
#define MQUEUE_MAGIC 0x19800202
#define MSDOS_SUPER_MAGIC 0x4d44
#define NCP_SUPER_MAGIC 0x564c
#define NFS_SUPER_MAGIC 0x6969
#define NILFS_SUPER_MAGIC 0x3434
#define NTFS_SB_MAGIC 0x5346544e
#define OCFS2_SUPER_MAGIC 0x7461636f
#define OPENPROM_SUPER_MAGIC 0x9fa1
#define PIPEFS_MAGIC 0x50495045
#define PROC_SUPER_MAGIC 0x9fa0
#define PSTOREFS_MAGIC 0x6165676C
#define QNX4_SUPER_MAGIC 0x002f
#define QNX6_SUPER_MAGIC 0x68191122
#define RAMFS_MAGIC 0x858458f6
#define REISERFS_SUPER_MAGIC 0x52654973
#define ROMFS_MAGIC 0x7275
#define SELINUX_MAGIC 0xf97cff8c
#define SMACK_MAGIC 0x43415d53
#define SMB_SUPER_MAGIC 0x517B
#define SOCKFS_MAGIC 0x534F434B
#define SQUASHFS_MAGIC 0x73717368
#define SYSFS_MAGIC 0x62656572
#define SYSV2_SUPER_MAGIC 0x012FF7B6
#define SYSV4_SUPER_MAGIC 0x012FF7B5
#define TMPFS_MAGIC 0x01021994
#define UDF_SUPER_MAGIC 0x15013346
#define UFS_MAGIC 0x00011954
#define USBDEVICE_SUPER_MAGIC 0x9fa2
#define V9FS_MAGIC 0x01021997
#define VXFS_SUPER_MAGIC 0xa501FCF5
#define XENFS_SUPER_MAGIC 0xabba1974
#define XENIX_SUPER_MAGIC 0x012FF7B4
#define XFS_SUPER_MAGIC 0x58465342
#define _XIAFS_SUPER_MAGIC 0x012FD16D
#define AFS_SUPER_MAGIC 0x5346414F
#define AUFS_SUPER_MAGIC 0x61756673
#define ANON_INODE_FS_SUPER_MAGIC 0x09041934
#define CEPH_SUPER_MAGIC 0x00C36400
#define ECRYPTFS_SUPER_MAGIC 0xF15F
#define FAT_SUPER_MAGIC 0x4006
#define FHGFS_SUPER_MAGIC 0x19830326
#define FUSEBLK_SUPER_MAGIC 0x65735546
#define FUSECTL_SUPER_MAGIC 0x65735543
#define GFS_SUPER_MAGIC 0x1161970
#define GPFS_SUPER_MAGIC 0x47504653
#define MTD_INODE_FS_SUPER_MAGIC 0x11307854
#define INOTIFYFS_SUPER_MAGIC 0x2BAD1DEA
#define ISOFS_R_WIN_SUPER_MAGIC 0x4004
#define ISOFS_WIN_SUPER_MAGIC 0x4000
#define JFFS_SUPER_MAGIC 0x07C0
#define KAFS_SUPER_MAGIC 0x6B414653
#define LUSTRE_SUPER_MAGIC 0x0BD00BD0
#define NFSD_SUPER_MAGIC 0x6E667364
#define PANFS_SUPER_MAGIC 0xAAD7AAEA
#define RPC_PIPEFS_SUPER_MAGIC 0x67596969
#define SECURITYFS_SUPER_MAGIC 0x73636673
#define UFS_BYTESWAPPED_SUPER_MAGIC 0x54190100
#define VMHGFS_SUPER_MAGIC 0xBACBACBC
#define VZFS_SUPER_MAGIC 0x565A4653
#define ZFS_SUPER_MAGIC 0x2FC12FC1

struct filesystem_entry {
	unsigned id;
	const char* name;
	int remote;
} FILESYSTEMS[] = {
	{ ADFS_SUPER_MAGIC, "adfs", 0 },
	{ AFFS_SUPER_MAGIC, "affs", 0 },
	{ AFS_SUPER_MAGIC, "afs", 1 },
	{ AUFS_SUPER_MAGIC, "aufs", 1 },
	{ BEFS_SUPER_MAGIC, "befs", 0 },
	{ BDEVFS_MAGIC, "bdevfs", 0 },
	{ BFS_MAGIC, "bfs", 0 },
	{ BINFMTFS_MAGIC, "binfmt_misc", 0 },
	{ BTRFS_SUPER_MAGIC, "btrfs", 0 },
	{ CEPH_SUPER_MAGIC, "ceph", 1 },
	{ CGROUP_SUPER_MAGIC, "cgroupfs", 0 },
	{ CIFS_MAGIC_NUMBER, "cifs", 1 },
	{ CODA_SUPER_MAGIC, "coda", 1 },
	{ COH_SUPER_MAGIC, "coh", 0 },
	{ CRAMFS_MAGIC, "cramfs", 0 },
	{ DEBUGFS_MAGIC, "debugfs", 0 },
	{ DEVFS_SUPER_MAGIC, "devfs", 0 },
	{ DEVPTS_SUPER_MAGIC, "devpts", 0 },
	{ ECRYPTFS_SUPER_MAGIC, "ecryptfs", 0 },
	{ EFS_SUPER_MAGIC, "efs", 0 },
	{ EXT_SUPER_MAGIC, "ext", 0 },
	{ EXT4_SUPER_MAGIC, "ext4", 0 },
	{ EXT2_OLD_SUPER_MAGIC, "ext2", 0 },
	{ FAT_SUPER_MAGIC, "fat", 0 },
	{ FHGFS_SUPER_MAGIC, "fhgfs", 1 },
	{ FUSEBLK_SUPER_MAGIC, "fuseblk", 1 },
	{ FUSECTL_SUPER_MAGIC, "fusectl", 1 },
	{ FUTEXFS_SUPER_MAGIC, "futexfs", 0 },
	{ GFS_SUPER_MAGIC, "gfs/gfs2", 1 },
	{ GPFS_SUPER_MAGIC, "gpfs", 1 },
	{ HFS_SUPER_MAGIC, "hfs", 0 },
	{ HFSPLUS_SUPER_MAGIC, "hfsplus", 0 },
	{ HPFS_SUPER_MAGIC, "hpfs", 0 },
	{ HUGETLBFS_MAGIC, "hugetlbfs", 0 },
	{ MTD_INODE_FS_SUPER_MAGIC, "inodefs", 0 },
	{ INOTIFYFS_SUPER_MAGIC, "inotifyfs", 0 },
	{ ISOFS_SUPER_MAGIC, "isofs", 0 },
	{ ISOFS_R_WIN_SUPER_MAGIC, "isofs", 0 },
	{ ISOFS_WIN_SUPER_MAGIC, "isofs", 0 },
	{ JFFS_SUPER_MAGIC, "jffs", 0 },
	{ JFFS2_SUPER_MAGIC, "jffs2", 0 },
	{ JFS_SUPER_MAGIC, "jfs", 0 },
	{ KAFS_SUPER_MAGIC, "k-afs", 1 },
	{ LUSTRE_SUPER_MAGIC, "lustre", 1 },
	{ MINIX_SUPER_MAGIC, "minix", 0 },
	{ MINIX_SUPER_MAGIC2, "minix", 0 },
	{ MINIX2_SUPER_MAGIC, "minix2", 0 },
	{ MINIX2_SUPER_MAGIC2, "minix2", 0 },
	{ MINIX3_SUPER_MAGIC, "minix3", 0 },
	{ MQUEUE_MAGIC, "mqueue", 0 },
	{ MSDOS_SUPER_MAGIC, "msdos", 0 },
	{ NCP_SUPER_MAGIC, "novell", 1 },
	{ NFS_SUPER_MAGIC, "nfs", 1 },
	{ NFSD_SUPER_MAGIC, "nfsd", 1 },
	{ NILFS_SUPER_MAGIC, "nilfs", 0 },
	{ NTFS_SB_MAGIC, "ntfs", 0 },
	{ OPENPROM_SUPER_MAGIC, "openprom", 0 },
	{ OCFS2_SUPER_MAGIC, "ocfs2", 1 },
	{ PANFS_SUPER_MAGIC, "panfs", 1 },
	{ PIPEFS_MAGIC, "pipefs", 1 },
	{ PROC_SUPER_MAGIC, "proc", 0 },
	{ PSTOREFS_MAGIC, "pstorefs", 0 },
	{ QNX4_SUPER_MAGIC, "qnx4", 0 },
	{ QNX6_SUPER_MAGIC, "qnx6", 0 },
	{ RAMFS_MAGIC, "ramfs", 0 },
	{ REISERFS_SUPER_MAGIC, "reiserfs", 0 },
	{ ROMFS_MAGIC, "romfs", 0 },
	{ RPC_PIPEFS_SUPER_MAGIC, "rpc_pipefs", 0 },
	{ SECURITYFS_SUPER_MAGIC, "securityfs", 0 },
	{ SELINUX_MAGIC, "selinux", 0 },
	{ SMB_SUPER_MAGIC, "smb", 1 },
	{ SOCKFS_MAGIC, "sockfs", 0 },
	{ SQUASHFS_MAGIC, "squashfs", 0 },
	{ SYSFS_MAGIC, "sysfs", 0 },
	{ SYSV2_SUPER_MAGIC, "sysv2", 0 },
	{ SYSV4_SUPER_MAGIC, "sysv4", 0 },
	{ TMPFS_MAGIC, "tmpfs", 0 },
	{ UDF_SUPER_MAGIC, "udf", 0 },
	{ UFS_MAGIC, "ufs", 0 },
	{ UFS_BYTESWAPPED_SUPER_MAGIC, "ufs", 0 },
	{ USBDEVICE_SUPER_MAGIC, "usbdevfs", 0 },
	{ V9FS_MAGIC, "v9fs", 0 },
	{ VMHGFS_SUPER_MAGIC, "vmhgfs", 1 },
	{ VXFS_SUPER_MAGIC, "vxfs", 0 },
	{ VZFS_SUPER_MAGIC, "vzfs", 0 },
	{ XENFS_SUPER_MAGIC, "xenfs", 0 },
	{ XENIX_SUPER_MAGIC, "xenix", 0 },
	{ XFS_SUPER_MAGIC, "xfs", 0 },
	{ _XIAFS_SUPER_MAGIC, "xia", 0 },
	{ ZFS_SUPER_MAGIC, "zfs", 0 },
	{ 0 }
};

int fsinfo(const char* path, int* has_persistent_inode, int* has_syncronized_hardlinks, uint64_t* total_space, uint64_t* free_space, char* fstype, size_t fstype_size, char* fslabel, size_t fslabel_size)
{
#if HAVE_STATFS
	struct statfs st;

	if (statfs(path, &st) != 0) {
		char dir[PATH_MAX];
		char* slash;

		if (errno != ENOENT) {
			return -1;
		}

		/* if it doesn't exist, we assume a file */
		/* and we check for the containing dir */
		if (strlen(path) + 1 > sizeof(dir)) {
			errno = ENAMETOOLONG;
			return -1;
		}

		strcpy(dir, path);

		slash = strrchr(dir, '/');
		if (!slash)
			return -1;

		*slash = 0;
		if (statfs(dir, &st) != 0)
			return -1;
	}
#endif

	/* to get the fs type check "man stat" or "stat -f -t FILE" */
	if (has_persistent_inode) {
#if HAVE_STATFS && HAVE_STRUCT_STATFS_F_TYPE
		switch (st.f_type) {
		case FUSEBLK_SUPER_MAGIC : /* FUSE, "fuseblk" in the stat command */
		case MSDOS_SUPER_MAGIC : /* VFAT, "msdos" in the stat command */
			*has_persistent_inode = 0;
			break;
		default :
			/* by default assume yes */
			*has_persistent_inode = 1;
			break;
		}
#else
		/* in Unix inodes are persistent by default */
		*has_persistent_inode = 1;
#endif
	}

	if (has_syncronized_hardlinks) {
#if HAVE_STATFS && HAVE_STRUCT_STATFS_F_TYPE
		switch (st.f_type) {
		case NTFS_SB_MAGIC : /* NTFS */
		case MSDOS_SUPER_MAGIC : /* VFAT, "msdos" in the stat command */
			*has_syncronized_hardlinks = 0;
			break;
		default :
			/* by default assume yes */
			*has_syncronized_hardlinks = 1;
			break;
		}
#else
		/* in Unix hardlinks share the same metadata by default */
		*has_syncronized_hardlinks = 1;
#endif
	}

	if (total_space) {
#if HAVE_STATFS
		*total_space = st.f_bsize * (uint64_t)st.f_blocks;
#else
		*total_space = 0;
#endif
	}

	if (free_space) {
#if HAVE_STATFS
		*free_space = st.f_bsize * (uint64_t)st.f_bfree;
#else
		*free_space = 0;
#endif
	}

	const char* ptype = 0;

#if HAVE_STATFS && HAVE_STRUCT_STATFS_F_FSTYPENAME
	/* get the filesystem type directly from the struct (Mac OS X) */
	ptype = st.f_fstypename;
#elif HAVE_STATFS && HAVE_STRUCT_STATFS_F_TYPE
	/* get the filesystem type from f_type (Linux) */
	/* from: https://github.com/influxdata/gopsutil/blob/master/disk/disk_linux.go */
	for (int i = 0; FILESYSTEMS[i].id != 0; ++i) {
		if (st.f_type == FILESYSTEMS[i].id) {
			ptype = FILESYSTEMS[i].name;
			break;
		}
	}
#endif

	if (fstype) {
		fstype[0] = 0;
		if (ptype)
			snprintf(fstype, fstype_size, "%s", ptype);
	}

	if (fslabel) {
		fslabel[0] = 0;
#if HAVE_BLKID
		struct stat fst;
		if (stat(path, &fst) == 0)
			devuuid_label(fst.st_dev, fslabel, fslabel_size);
#else
		(void)fslabel_size;
#endif
	}

	return 0;
}

uint64_t tick(void)
{
#if HAVE_MACH_ABSOLUTE_TIME
	/* for Mac OS X */
	return mach_absolute_time();
#elif HAVE_CLOCK_GETTIME && (defined(CLOCK_MONOTONIC) || defined(CLOCK_MONOTONIC_RAW))
	/* for Linux */
	struct timespec tv;

	/* nanosecond precision with clock_gettime() */
#if defined(CLOCK_MONOTONIC_RAW)
	if (clock_gettime(CLOCK_MONOTONIC_RAW, &tv) != 0) {
#else
	if (clock_gettime(CLOCK_MONOTONIC, &tv) != 0) {
#endif
		return 0;
	}

	return tv.tv_sec * 1000000000ULL + tv.tv_nsec;
#else
	/* other platforms */
	struct timeval tv;

	/* microsecond precision with gettimeofday() */
	if (gettimeofday(&tv, 0) != 0) {
		return 0;
	}

	return tv.tv_sec * 1000000ULL + tv.tv_usec;
#endif
}

uint64_t tick_ms(void)
{
	struct timeval tv;

	if (gettimeofday(&tv, 0) != 0)
		return 0;

	return tv.tv_sec * 1000ULL + tv.tv_usec / 1000;
}

int randomize(void* ptr, size_t size)
{
	int f;
	ssize_t ret;

	f = open("/dev/urandom", O_RDONLY);
	if (f == -1)
		return -1;

	ret = read(f, ptr, size);
	if (ret < 0 || (size_t)ret != size) {
		close(f);
		return -1;
	}

	if (close(f) != 0)
		return -1;

	return 0;
}

/**
 * Read a file extracting the contained device number in %u:%u format.
 * Return 0 on error.
 */
#if HAVE_LINUX_DEVICE
static dev_t devread(const char* path)
{
	int f;
	int ret;
	int len;
	char buf[64];
	char* e;
	unsigned ma;
	unsigned mi;

	f = open(path, O_RDONLY);
	if (f == -1) {
		/* LCOV_EXCL_START */
		log_fatal(errno, "Failed to open '%s'.\n", path);
		return 0;
		/* LCOV_EXCL_STOP */
	}

	len = read(f, buf, sizeof(buf));
	if (len < 0) {
		/* LCOV_EXCL_START */
		close(f);
		log_fatal(errno, "Failed to read '%s'.\n", path);
		return 0;
		/* LCOV_EXCL_STOP */
	}
	if (len == sizeof(buf)) {
		/* LCOV_EXCL_START */
		close(f);
		log_fatal(EEXTERNAL, "Too long read '%s'.\n", path);
		return 0;
		/* LCOV_EXCL_STOP */
	}

	ret = close(f);
	if (ret != 0) {
		/* LCOV_EXCL_START */
		log_fatal(errno, "Failed to close '%s'.\n", path);
		return 0;
		/* LCOV_EXCL_STOP */
	}

	buf[len] = 0;

	ma = strtoul(buf, &e, 10);
	if (*e != ':') {
		/* LCOV_EXCL_START */
		log_fatal(EEXTERNAL, "Invalid format in '%s' for '%s'.\n", path, buf);
		return 0;
		/* LCOV_EXCL_STOP */
	}

	mi = strtoul(e + 1, &e, 10);
	if (*e != 0 && !isspace(*e)) {
		/* LCOV_EXCL_START */
		log_fatal(EEXTERNAL, "Invalid format in '%s' for '%s'.\n", path, buf);
		return 0;
		/* LCOV_EXCL_STOP */
	}

	return makedev(ma, mi);
}
#endif

/**
 * Read a device tree filling the specified list of disk_t entries.
 */
#if HAVE_LINUX_DEVICE
static int devtree(devinfo_t* parent, dev_t device, tommy_list* list)
{
	char path[PATH_MAX];
	DIR* d;
	int slaves = 0;

	pathprint(path, sizeof(path), "/sys/dev/block/%u:%u/slaves", major(device), minor(device));

	/* check if there is a slaves list */
	d = opendir(path);
	if (d != 0) {
		struct dirent* dd;

		while ((dd = readdir(d)) != 0) {
			if (dd->d_name[0] != '.') {
				dev_t subdev;

				/* for each slave, expand the full potential tree */
				pathprint(path, sizeof(path), "/sys/dev/block/%u:%u/slaves/%s/dev", major(device), minor(device), dd->d_name);

				subdev = devread(path);
				if (!subdev) {
					/* LCOV_EXCL_START */
					closedir(d);
					return -1;
					/* LCOV_EXCL_STOP */
				}

				if (devtree(parent, subdev, list) != 0) {
					/* LCOV_EXCL_START */
					closedir(d);
					return -1;
					/* LCOV_EXCL_STOP */
				}

				++slaves;
			}
		}

		closedir(d);
	}

	/* if no slaves found */
	if (!slaves) {
		/* this is a raw device */
		devinfo_t* devinfo;

		/* check if it's a real device */
		pathprint(path, sizeof(path), "/sys/dev/block/%u:%u/device", major(device), minor(device));
		if (access(path, F_OK) != 0) {
			/* get the parent device */
			pathprint(path, sizeof(path), "/sys/dev/block/%u:%u/../dev", major(device), minor(device));

			device = devread(path);
			if (!device) {
				/* LCOV_EXCL_START */
				return -1;
				/* LCOV_EXCL_STOP */
			}
		}

		/* get the device file */
		if (devresolve(device, path, sizeof(path)) != 0) {
			/* LCOV_EXCL_START */
			log_fatal(EEXTERNAL, "Failed to resolve device '%u:%u'.\n", major(device), minor(device));
			return -1;
			/* LCOV_EXCL_STOP */
		}

		devinfo = calloc_nofail(1, sizeof(devinfo_t));

		devinfo->device = device;
		pathcpy(devinfo->name, sizeof(devinfo->name), parent->name);
		pathcpy(devinfo->smartctl, sizeof(devinfo->smartctl), parent->smartctl);
		memcpy(devinfo->smartignore, parent->smartignore, sizeof(devinfo->smartignore));
		pathcpy(devinfo->file, sizeof(devinfo->file), path);
		devinfo->parent = parent;

		/* insert in the list */
		tommy_list_insert_tail(list, &devinfo->node, devinfo);
	}

	return 0;
}
#endif

/**
 * Scan all the devices.
 *
 * If a device is already in, it's not added again.
 */
#if HAVE_LINUX_DEVICE
static int devscan(tommy_list* list)
{
	char dir[PATH_MAX];
	DIR* d;
	struct dirent* dd;

	pathprint(dir, sizeof(dir), "/sys/dev/block/");

	/* check if there is a slaves list */
	d = opendir(dir);
	if (d == 0) {
		/* LCOV_EXCL_START */
		log_fatal(errno, "Failed to open dir '%s'.\n", dir);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	while ((dd = readdir(d)) != 0) {
		char path[PATH_MAX];
		tommy_node* i;
		dev_t device;
		devinfo_t* devinfo;

		if (dd->d_name[0] == '.')
			continue;

		pathprint(path, sizeof(path), "/sys/dev/block/%s/device", dd->d_name);

		/* check if it's a real device */
		if (access(path, F_OK) != 0)
			continue;

		pathprint(path, sizeof(path), "/sys/dev/block/%s/dev", dd->d_name);

		device = devread(path);
		if (!device) {
			/* LCOV_EXCL_START */
			log_tag("scan:skip: Skipping device %s because failed to read its device number.\n", dd->d_name);
			continue;
			/* LCOV_EXCL_STOP */
		}

		/* check if already present */
		for (i = tommy_list_head(list); i != 0; i = i->next) {
			devinfo = i->data;
			if (devinfo->device == device)
				break;
		}

		/* if already present */
		if (i != 0)
			continue;

		/* get the device file */
		if (devresolve(device, path, sizeof(path)) != 0) {
			/* LCOV_EXCL_START */
			log_tag("scan:skip: Skipping device %u:%u because failed to resolve.\n", major(device), minor(device));
			continue;
			/* LCOV_EXCL_STOP */
		}

		devinfo = calloc_nofail(1, sizeof(devinfo_t));

		devinfo->device = device;
		pathcpy(devinfo->file, sizeof(devinfo->file), path);

		/* insert in the list */
		tommy_list_insert_tail(list, &devinfo->node, devinfo);
	}

	closedir(d);
	return 0;
}
#endif

/**
 * Get SMART attributes.
 */
#if HAVE_LINUX_DEVICE
static int devsmart(dev_t device, const char* name, const char* smartctl, uint64_t* smart, uint64_t* info, char* serial, char* family, char* model, char* interface)
{
	char cmd[PATH_MAX + 64];
	char file[PATH_MAX];
	FILE* f;
	int ret;
	const char* x;

	x = find_smartctl();
	if (!x) {
		/* LCOV_EXCL_START */
		log_fatal(EEXTERNAL, "Cannot find smartctl.\n");
		return -1;
		/* LCOV_EXCL_STOP */
	}

	if (devresolve(device, file, sizeof(file)) != 0) {
		/* LCOV_EXCL_START */
		log_fatal(EEXTERNAL, "Failed to resolve device '%u:%u'.\n", major(device), minor(device));
		return -1;
		/* LCOV_EXCL_STOP */
	}

	/* if there is a custom smartctl command */
	if (smartctl[0]) {
		char option[PATH_MAX];
		snprintf(option, sizeof(option), smartctl, file);
		snprintf(cmd, sizeof(cmd), "%s -a %s", x, option);
	} else {
		snprintf(cmd, sizeof(cmd), "%s -a %s", x, file);
	}

	log_tag("smartctl:%s:%s:run: %s\n", file, name, cmd);

	f = popen(cmd, "r");
	if (!f) {
		/* LCOV_EXCL_START */
		log_tag("device:%s:%s:shell\n", file, name);
		log_fatal(EEXTERNAL, "Failed to run '%s' (from popen).\n", cmd);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	if (smartctl_attribute(f, file, name, smart, info, serial, family, model, interface) != 0) {
		/* LCOV_EXCL_START */
		pclose(f);
		log_tag("device:%s:%s:shell\n", file, name);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	ret = pclose(f);

	log_tag("smartctl:%s:%s:ret: %x\n", file, name, ret);

	if (!WIFEXITED(ret)) {
		/* LCOV_EXCL_START */
		log_tag("device:%s:%s:abort\n", file, name);
		log_fatal(EEXTERNAL, "Failed to run '%s' (not exited).\n", cmd);
		return -1;
		/* LCOV_EXCL_STOP */
	}
	if (WEXITSTATUS(ret) == 127) {
		/* LCOV_EXCL_START */
		log_tag("device:%s:%s:shell\n", file, name);
		log_fatal(EEXTERNAL, "Failed to run '%s' (from sh).\n", cmd);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	/* store the return smartctl return value */
	smart[SMART_FLAGS] = WEXITSTATUS(ret);

	return 0;
}
#endif

/**
 * Compute disk usage by aggregating access statistics.
 */
#if HAVE_LINUX_DEVICE
static int devstat(dev_t device, uint64_t* count)
{
	char path[PATH_MAX];
	char buf[512];
	int token;
	int ret;
	char* i;

	*count = 0;

	pathprint(path, sizeof(path), "/sys/dev/block/%u:%u/stat", major(device), minor(device));

	ret = sysread(path, buf, sizeof(buf));
	if (ret < 0) {
		/* LCOV_EXCL_START */
		log_fatal(errno, "Failed to read '%s'.\n", path);
		return -1;
		/* LCOV_EXCL_STOP */
	}
	if ((size_t)ret + 1 > sizeof(buf)) {
		/* LCOV_EXCL_START */
		log_fatal(EEXTERNAL, "Too long read '%s'.\n", path);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	/* ending 0 */
	buf[ret] = 0;

	i = buf;
	token = 1; /* token number */
	while (*i) {
		char* n;
		char* e;
		unsigned long long v;

		/* skip spaces */
		while (*i && isspace(*i))
			++i;

		/* read digits */
		n = i;
		while (*i && isdigit(*i))
			++i;

		if (i == n) /* if no digit, abort */
			break;
		if (*i == 0 || !isspace(*i)) /* if no space, abort */
			break;
		*i++ = 0; /* put a terminator */

		v = strtoull(n, &e, 10);
		if (*e != 0)
			break;

		/* sum reads and writes completed */
		if (token == 1 || token == 5) {
			*count += v;
			if (token == 5)
				break; /* stop here */
		}

		++token;
	}

	return 0;
}
#endif

/**
 * Get device attributes.
 */
#if HAVE_LINUX_DEVICE
static void devattr(dev_t device, uint64_t* info, char* serial, char* family, char* model, char* interface)
{
	char path[PATH_MAX];
	char buf[512];
	int ret;
	char* attr;

	(void)family; /* not available, smartctl uses an internal database to get it */

	if (info[INFO_SIZE] == SMART_UNASSIGNED) {
		pathprint(path, sizeof(path), "/sys/dev/block/%u:%u/size", major(device), minor(device));
		attr = sysattr(path, buf, sizeof(buf));
		if (attr) {
			char* e;
			uint64_t v;
			v = strtoul(attr, &e, 10);
			if (*e == 0)
				info[INFO_SIZE] = v * 512;
		}
	}

	if (info[INFO_ROTATION_RATE] == SMART_UNASSIGNED) {
		pathprint(path, sizeof(path), "/sys/dev/block/%u:%u/queue/rotational", major(device), minor(device));
		attr = sysattr(path, buf, sizeof(buf));
		if (attr) {
			char* e;
			uint64_t v;
			v = strtoul(attr, &e, 10);
			if (*e == 0)
				info[INFO_ROTATION_RATE] = v;
		}
	}

	if (*model == 0) {
		pathprint(path, sizeof(path), "/sys/dev/block/%u:%u/device/model", major(device), minor(device));
		attr = sysattr(path, buf, sizeof(buf));
		if (attr && *attr) {
			pathcpy(model, SMART_MAX, attr);
			strtrim(model);
		}
	}

	if (*serial == 0) {
		pathprint(path, sizeof(path), "/sys/dev/block/%u:%u/device/serial", major(device), minor(device));
		attr = sysattr(path, buf, sizeof(buf));
		if (attr && *attr) {
			pathcpy(serial, SMART_MAX, attr);
			strtrim(serial);
		}
	}

	if (*serial == 0) {
		// --- Page 0x80: Unit Serial Number ---
		pathprint(path, sizeof(path), "/sys/dev/block/%u:%u/device/vpd_pg80", major(device), minor(device));
		ret = sysread(path, buf, sizeof(buf));
		if (ret > 4) {
			unsigned len = (unsigned char)buf[3];
			if (4 + len <= (size_t)ret && 4 + len + 1 <= sizeof(buf)) {
				attr = buf + 4;
				attr[len] = 0;
				strtrim(attr);
				if (*attr) {
					pathcpy(serial, SMART_MAX, attr);
					strtrim(serial);
				}
			}
		}
	}

	/* always override interface if it's usb */
	pathprint(path, sizeof(path), "/sys/dev/block/%u:%u", major(device), minor(device));
	ret = readlink(path, buf, sizeof(buf));
	if (ret > 0 && (unsigned)ret < sizeof(buf)) {
		buf[ret] = 0;
		if (strstr(buf, "usb") != 0)
			strcpy(interface, "USB");
	}
}
#endif

/**
 * Get POWER state.
 */
#if HAVE_LINUX_DEVICE
static int devprobe(dev_t device, const char* name, const char* smartctl, int* power, uint64_t* smart, uint64_t* info, char* serial, char* family, char* model, char* interface)
{
	char cmd[PATH_MAX + 64];
	char file[PATH_MAX];
	FILE* f;
	int ret;
	const char* x;

	x = find_smartctl();
	if (!x) {
		/* LCOV_EXCL_START */
		log_fatal(EEXTERNAL, "Cannot find smartctl.\n");
		return -1;
		/* LCOV_EXCL_STOP */
	}

	if (devresolve(device, file, sizeof(file)) != 0) {
		/* LCOV_EXCL_START */
		log_fatal(EEXTERNAL, "Failed to resolve device '%u:%u'.\n", major(device), minor(device));
		return -1;
		/* LCOV_EXCL_STOP */
	}

	/* if there is a custom smartctl command */
	if (smartctl[0]) {
		char option[PATH_MAX];
		snprintf(option, sizeof(option), smartctl, file);
		snprintf(cmd, sizeof(cmd), "%s -n standby,3 -a %s", x, option);
	} else {
		snprintf(cmd, sizeof(cmd), "%s -n standby,3 -a %s", x, file);
	}

	log_tag("smartctl:%s:%s:run: %s\n", file, name, cmd);

	f = popen(cmd, "r");
	if (!f) {
		/* LCOV_EXCL_START */
		log_tag("device:%s:%s:shell\n", file, name);
		log_fatal(EEXTERNAL, "Failed to run '%s' (from popen).\n", cmd);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	if (smartctl_attribute(f, file, name, smart, info, serial, family, model, interface) != 0) {
		/* LCOV_EXCL_START */
		pclose(f);
		log_tag("device:%s:%s:shell\n", file, name);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	ret = pclose(f);

	log_tag("smartctl:%s:%s:ret: %x\n", file, name, ret);

	if (!WIFEXITED(ret)) {
		/* LCOV_EXCL_START */
		log_tag("device:%s:%s:abort\n", file, name);
		log_fatal(EEXTERNAL, "Failed to run '%s' (not exited).\n", cmd);
		return -1;
		/* LCOV_EXCL_STOP */
	}
	if (WEXITSTATUS(ret) == 127) {
		/* LCOV_EXCL_START */
		log_tag("device:%s:%s:shell\n", file, name);
		log_fatal(EEXTERNAL, "Failed to run '%s' (from sh).\n", cmd);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	if (WEXITSTATUS(ret) == 3) {
		log_tag("attr:%s:%s:power:standby\n", file, name);
		*power = POWER_STANDBY;
	} else {
		log_tag("attr:%s:%s:power:active\n", file, name);
		*power = POWER_ACTIVE;

		/* store the return smartctl return value */
		smart[SMART_FLAGS] = WEXITSTATUS(ret);
	}

	return 0;
}
#endif

/**
 * Spin down a specific device.
 */
#if HAVE_LINUX_DEVICE
static int devdown(dev_t device, const char* name, const char* smartctl)
{
	char cmd[PATH_MAX + 64];
	char file[PATH_MAX];
	FILE* f;
	int ret;
	const char* x;

	x = find_smartctl();
	if (!x) {
		/* LCOV_EXCL_START */
		log_fatal(EEXTERNAL, "Cannot find smartctl.\n");
		return -1;
		/* LCOV_EXCL_STOP */
	}

	if (devresolve(device, file, sizeof(file)) != 0) {
		/* LCOV_EXCL_START */
		log_fatal(EEXTERNAL, "Failed to resolve device '%u:%u'.\n", major(device), minor(device));
		return -1;
		/* LCOV_EXCL_STOP */
	}

	/* if there is a custom smartctl command */
	if (smartctl[0]) {
		char option[PATH_MAX];
		snprintf(option, sizeof(option), smartctl, file);
		snprintf(cmd, sizeof(cmd), "%s -s standby,now %s", x, option);
	} else {
		snprintf(cmd, sizeof(cmd), "%s -s standby,now %s", x, file);
	}

	log_tag("smartctl:%s:%s:run: %s\n", file, name, cmd);

	f = popen(cmd, "r");
	if (!f) {
		/* LCOV_EXCL_START */
		log_tag("device:%s:%s:shell\n", file, name);
		log_fatal(EEXTERNAL, "Failed to run '%s' (from popen).\n", cmd);
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

	if (!WIFEXITED(ret)) {
		/* LCOV_EXCL_START */
		log_tag("device:%s:%s:abort\n", file, name);
		log_fatal(EEXTERNAL, "Failed to run '%s' (not exited).\n", cmd);
		return -1;
		/* LCOV_EXCL_STOP */
	}
	if (WEXITSTATUS(ret) == 127) {
		/* LCOV_EXCL_START */
		log_tag("device:%s:%s:shell\n", file, name);
		log_fatal(EEXTERNAL, "Failed to run '%s' (from sh).\n", cmd);
		return -1;
		/* LCOV_EXCL_STOP */
	}
	if (WEXITSTATUS(ret) != 0) {
		/* LCOV_EXCL_START */
		log_tag("device:%s:%s:exit:%d\n", file, name, WEXITSTATUS(ret));
		log_fatal(EEXTERNAL, "Failed to run '%s' with return code %xh.\n", cmd, WEXITSTATUS(ret));
		return -1;
		/* LCOV_EXCL_STOP */
	}

	log_tag("attr:%s:%s:power:down\n", file, name);

	return 0;
}
#endif

/**
 * Spin down a specific device if it's up.
 */
#if HAVE_LINUX_DEVICE
static int devdownifup(dev_t device, const char* name, const char* smartctl, int* power)
{
	*power = POWER_UNKNOWN;

	if (devprobe(device, name, smartctl, power, 0, 0, 0, 0, 0, 0) != 0)
		return -1;

	if (*power == POWER_ACTIVE)
		return devdown(device, name, smartctl);

	return 0;
}
#endif

/**
 * Spin up a specific device.
 */
#if HAVE_LINUX_DEVICE
static int devup(dev_t device, const char* name)
{
	char file[PATH_MAX];
	int ret;
	int f;
	void* buf;
	uint64_t size;
	uint64_t offset;

	if (devresolve(device, file, sizeof(file)) != 0) {
		/* LCOV_EXCL_START */
		log_fatal(EEXTERNAL, "Failed to resolve device '%u:%u'.\n", major(device), minor(device));
		return -1;
		/* LCOV_EXCL_STOP */
	}

	/* O_DIRECT requires memory aligned to the block size */
	if (posix_memalign(&buf, 4096, 4096) != 0) {
		/* LCOV_EXCL_START */
		log_fatal(errno, "Failed to allocate aligned memory for device '%u:%u'.\n", major(device), minor(device));
		return -1;
		/* LCOV_EXCL_STOP */
	}

	f = open(file, O_RDONLY | O_DIRECT);
	if (f < 0) {
		/* LCOV_EXCL_START */
		free(buf);
		log_tag("device:%s:%s:error:%d\n", file, name, errno);
		log_fatal(errno, "Failed to open device '%u:%u'.\n", major(device), minor(device));
		return -1;
		/* LCOV_EXCL_STOP */
	}

	if (ioctl(f, BLKGETSIZE64, &size) < 0) {
		/* LCOV_EXCL_START */
		close(f);
		free(buf);
		log_tag("device:%s:%s:error:%d\n", file, name, errno);
		log_fatal(errno, "Failed to get device size '%u:%u'.\n", major(device), minor(device));
		return -1;
		/* LCOV_EXCL_STOP */
	}

	/* select a random offset */
	offset = (random_u64() % (size / 4096)) * 4096;

#if HAVE_POSIX_FADVISE
	/* clear cache */
	ret = posix_fadvise(f, offset, 4096, POSIX_FADV_DONTNEED);
	if (ret != 0) {
		/* LCOV_EXCL_START */
		close(f);
		free(buf);
		log_tag("device:%s:%s:error:%d\n", file, name, errno);
		log_fatal(errno, "Failed to advise device '%u:%u'.\n", major(device), minor(device));
		return -1;
		/* LCOV_EXCL_STOP */
	}
#endif

	ret = pread(f, buf, 4096, offset);
	if (ret < 0) {
		/* LCOV_EXCL_START */
		close(f);
		free(buf);
		log_tag("device:%s:%s:error:%d\n", file, name, errno);
		log_fatal(errno, "Failed to read device '%u:%u'.\n", major(device), minor(device));
		return -1;
		/* LCOV_EXCL_STOP */
	}

	ret = close(f);
	if (ret < 0) {
		/* LCOV_EXCL_START */
		free(buf);
		log_tag("device:%s:%s:error:%d\n", file, name, errno);
		log_fatal(errno, "Failed to close device '%u:%u'.\n", major(device), minor(device));
		return -1;
		/* LCOV_EXCL_STOP */
	}

	log_tag("attr:%s:%s:power:up\n", file, name);

	free(buf);
	return 0;
}
#endif

/**
 * Thread for spinning up.
 *
 * Note that filling up the devinfo object is done inside this thread,
 * to avoid to block the main thread if the device need to be spin up
 * to handle stat/resolve requests.
 */
static void* thread_spinup(void* arg)
{
#if HAVE_LINUX_DEVICE
	devinfo_t* devinfo = arg;
	uint64_t start;

	start = tick_ms();

	if (devup(devinfo->device, devinfo->name) != 0) {
		/* LCOV_EXCL_START */
		return (void*)-1;
		/* LCOV_EXCL_STOP */
	}

	msg_status("Spunup device '%s' for disk '%s' in %" PRIu64 " ms.\n", devinfo->file, devinfo->name, tick_ms() - start);

	/* after the spin up, get SMART info */
	if (devsmart(devinfo->device, devinfo->name, devinfo->smartctl, devinfo->smart, devinfo->info, devinfo->serial, devinfo->family, devinfo->model, devinfo->interf) != 0) {
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
	devattr(devinfo->device, devinfo->info, devinfo->serial, devinfo->family, devinfo->model, devinfo->interf);

	return 0;
#else
	(void)arg;
	return (void*)-1;
#endif
}

/**
 * Thread for spinning down.
 */
static void* thread_spindown(void* arg)
{
#if HAVE_LINUX_DEVICE
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
#else
	(void)arg;
	return (void*)-1;
#endif
}

/**
 * Thread for spinning down.
 */
static void* thread_spindownifup(void* arg)
{
#if HAVE_LINUX_DEVICE
	devinfo_t* devinfo = arg;
	uint64_t start;
	int power;

	start = tick_ms();

	if (devdownifup(devinfo->device, devinfo->name, devinfo->smartctl, &power) != 0) {
		/* LCOV_EXCL_START */
		return (void*)-1;
		/* LCOV_EXCL_STOP */
	}

	if (power == POWER_ACTIVE)
		msg_status("Spundown device '%s' for disk '%s' in %" PRIu64 " ms.\n", devinfo->file, devinfo->name, tick_ms() - start);

	return 0;
#else
	(void)arg;
	return (void*)-1;
#endif
}

/**
 * Thread for getting smart info.
 */
static void* thread_smart(void* arg)
{
#if HAVE_LINUX_DEVICE
	devinfo_t* devinfo = arg;

	if (devsmart(devinfo->device, devinfo->name, devinfo->smartctl, devinfo->smart, devinfo->info, devinfo->serial, devinfo->family, devinfo->model, devinfo->interf) != 0) {
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
	devattr(devinfo->device, devinfo->info, devinfo->serial, devinfo->family, devinfo->model, devinfo->interf);

	return 0;
#else
	(void)arg;
	return (void*)-1;
#endif
}

/**
 * Thread for getting power info.
 */
static void* thread_probe(void* arg)
{
#if HAVE_LINUX_DEVICE
	devinfo_t* devinfo = arg;

	if (devprobe(devinfo->device, devinfo->name, devinfo->smartctl, &devinfo->power, devinfo->smart, devinfo->info, devinfo->serial, devinfo->family, devinfo->model, devinfo->interf) != 0) {
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
	devattr(devinfo->device, devinfo->info, devinfo->serial, devinfo->family, devinfo->model, devinfo->interf);

	return 0;
#else
	(void)arg;
	return (void*)-1;
#endif
}

static int device_thread(tommy_list* list, void* (*func)(void* arg))
{
	int fail = 0;
	tommy_node* i;

#if HAVE_THREAD
	/* start all threads */
	for (i = tommy_list_head(list); i != 0; i = i->next) {
		devinfo_t* devinfo = i->data;

		thread_create(&devinfo->thread, func, devinfo);
	}

	/* join all threads */
	for (i = tommy_list_head(list); i != 0; i = i->next) {
		devinfo_t* devinfo = i->data;
		void* retval;

		thread_join(devinfo->thread, &retval);

		if (retval != 0)
			++fail;
	}
#else
	for (i = tommy_list_head(list); i != 0; i = i->next) {
		devinfo_t* devinfo = i->data;

		if (func(devinfo) != 0)
			++fail;
	}
#endif
	if (fail != 0) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	return 0;
}

int devquery(tommy_list* high, tommy_list* low, int operation, int others)
{
	void* (*func)(void* arg) = 0;

#if HAVE_LINUX_DEVICE
	tommy_node* i;
	struct stat st;

	/* sysfs interface is required */
	if (stat("/sys/dev/block", &st) != 0) {
		/* LCOV_EXCL_START */
		log_fatal(EEXTERNAL, "Missing interface /sys/dev/block.\n");
		return -1;
		/* LCOV_EXCL_STOP */
	}

	/* for each device */
	for (i = tommy_list_head(high); i != 0; i = i->next) {
		devinfo_t* devinfo = i->data;
		uint64_t device = devinfo->device;
		uint64_t access_stat;

#if HAVE_SYNCFS
		if (operation == DEVICE_DOWN) {
			/* flush the high level filesystem before spinning down */
			int f = open(devinfo->mount, O_RDONLY);
			if (f >= 0) {
				syncfs(f);
				close(f);
			}
		}
#endif

		/* if the major is the null device, find the real one */
		if (major(device) == 0) {
			/* obtain the real device */
			if (devdereference(device, &device) != 0) {
				/* LCOV_EXCL_START */
				log_fatal(EEXTERNAL, "Failed to dereference device '%u:%u'.\n", major(device), minor(device));
				return -1;
				/* LCOV_EXCL_STOP */
			}
		}

		/* retrieve access stat for the high level device */
		if (devstat(device, &access_stat) == 0) {
			/* cumulate access stat in the first split */
			if (devinfo->split)
				devinfo->split->access_stat += access_stat;
			else
				devinfo->access_stat += access_stat;
		}

		/* get the device file */
		if (devresolve(device, devinfo->file, sizeof(devinfo->file)) != 0) {
			/* LCOV_EXCL_START */
			log_fatal(EEXTERNAL, "Failed to resolve device '%u:%u'.\n", major(device), minor(device));
			return -1;
			/* LCOV_EXCL_STOP */
		}

		/* expand the tree of devices */
		if (devtree(devinfo, device, low) != 0) {
			/* LCOV_EXCL_START */
			log_fatal(EEXTERNAL, "Failed to expand device '%u:%u'.\n", major(device), minor(device));
			return -1;
			/* LCOV_EXCL_STOP */
		}
	}

	/* add other devices */
	if (others) {
		if (devscan(low) != 0) {
			/* LCOV_EXCL_START */
			log_fatal(EEXTERNAL, "Failed to list other devices.\n");
			return -1;
			/* LCOV_EXCL_STOP */
		}
	}
#else
	(void)high;
	(void)others;
#endif

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

void os_init(int opt)
{
#if HAVE_BLKID
	int ret;
	ret = blkid_get_cache(&cache, NULL);
	if (ret != 0) {
		/* LCOV_EXCL_START */
		log_fatal(EEXTERNAL, "WARNING Failed to get blkid cache\n");
		/* LCOV_EXCL_STOP */
	}
#endif

	(void)opt;
}

void os_done(void)
{
#if HAVE_BLKID
	if (cache != 0)
		blkid_put_cache(cache);
#endif
}

/* LCOV_EXCL_START */
void os_abort(void)
{
#if HAVE_BACKTRACE && HAVE_BACKTRACE_SYMBOLS
	void* stack[32];
	char** messages;
	size_t size;
	unsigned i;
#endif

	printf("Stacktrace of " PACKAGE " v" VERSION);
#ifdef _linux
	printf(", linux");
#endif
#ifdef __GNUC__
	printf(", gcc " __VERSION__);
#endif
	printf(", %d-bit", (int)sizeof(void *) * 8);
	printf(", PATH_MAX=%d", PATH_MAX);
	printf("\n");

#if HAVE_BACKTRACE && HAVE_BACKTRACE_SYMBOLS
	size = backtrace(stack, 32);

	messages = backtrace_symbols(stack, size);

	for (i = 1; i < size; ++i) {
		const char* msg;

		if (messages)
			msg = messages[i];
		else
			msg = "<unknown>";

		printf("[bt] %02u: %s\n", i, msg);

		if (messages) {
			int ret;
			char addr2line[1024];
			size_t j = 0;
			while (msg[j] != '(' && msg[j] != ' ' && msg[j] != 0)
				++j;

			snprintf(addr2line, sizeof(addr2line), "addr2line %p -e %.*s", stack[i], (unsigned)j, msg);

			ret = system(addr2line);
			if (WIFEXITED(ret) && WEXITSTATUS(ret) != 0)
				printf("exit:%d\n", WEXITSTATUS(ret));
			if (WIFSIGNALED(ret))
				printf("signal:%d\n", WTERMSIG(ret));
		}
	}
#endif

	printf("Please report this error to the SnapRAID Forum:\n");
	printf("https://sourceforge.net/p/snapraid/discussion/1677233/\n");

	abort();
}
/* LCOV_EXCL_STOP */

void os_clear(void)
{
	/* ANSI codes */
	printf("\033[H"); /* cursor at topleft */
	printf("\033[2J"); /* clear screen */
}

size_t direct_size(void)
{
	long size;

	size = sysconf(_SC_PAGESIZE);

	if (size == -1) {
		/* LCOV_EXCL_START */
		log_fatal(EEXTERNAL, "No page size\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	return size;
}

#if HAVE_LINUX_DEVICE

/* List of possible ambient temperature labels */
const char* AMBIENT_LABEL[] = {
	"systin",
	"auxtin",
	"mb",
	"m/b",
	"board",
	"motherboard",
	"system",
	"chassis",
	"case",
	"room",
	"ambient",
	0
};

int ambient_temperature(void)
{
	DIR* dir;
	struct dirent* entry;
	int lowest_temp = 0;

	dir = opendir("/sys/class/hwmon");
	if (!dir)
		return 0;

	/* iterate through hwmon devices */
	while ((entry = readdir(dir)) != NULL) {
		char path[PATH_MAX];
		DIR* hwmon_dir;
		struct dirent* hwmon_entry;

		if (strncmp(entry->d_name, "hwmon", 5) != 0)
			continue;

		pathprint(path, sizeof(path), "/sys/class/hwmon/%s", entry->d_name);

		/* iterate through temp*_input files */
		hwmon_dir = opendir(path);
		if (!hwmon_dir)
			continue;

		while ((hwmon_entry = readdir(hwmon_dir)) != NULL) {
			char value[128];
			char name[128];
			char label[128];
			char* dash;
			char* e;
			int temp;

			if (strncmp(hwmon_entry->d_name, "temp", 4) != 0)
				continue;

			dash = strrchr(hwmon_entry->d_name, '_');
			if (dash == 0)
				continue;

			if (strcmp(dash, "_input") != 0)
				continue;

			/* read the temperature */
			pathprint(path, sizeof(path), "/sys/class/hwmon/%s/%s", entry->d_name, hwmon_entry->d_name);

			if (!sysattr(path, value, sizeof(value)))
				continue;

			temp = strtol(value, &e, 10) / 1000;
			if (*e != 0 && !isspace(*e))
				continue;

			/* cut the file name at "_input" */
			*dash = 0;

			/* read the corresponding name */
			pathprint(path, sizeof(path), "/sys/class/hwmon/%s/name", entry->d_name);
			if (!sysattr(path, name, sizeof(name))) {
				/* fallback to using the hwmon entry */
				pathcpy(name, sizeof(name), entry->d_name);
			}

			/* read the corresponding label file */
			pathprint(path, sizeof(path), "/sys/class/hwmon/%s/%s_label", entry->d_name, hwmon_entry->d_name);
			if (!sysattr(path, label, sizeof(label))) {
				/* fallback to using the temp* name (e.g., temp1, temp2) */
				pathcpy(label, sizeof(label), hwmon_entry->d_name);
			}

			log_tag("thermal:ambient:device:%s:%s:%s:%s:%d\n", entry->d_name, name, hwmon_entry->d_name, label, temp);

			/* check if temperature is in reasonable range */
			if (temp < 15 || temp > 40)
				continue;

			/* lower case */
			strlwr(label);

			/* check if label matches possible ambient labels */
			for (int i = 0; AMBIENT_LABEL[i]; ++i) {
				if (worddigitstr(label, AMBIENT_LABEL[i]) != 0) {
					log_tag("thermal:ambient:candidate:%d\n", temp);
					if (lowest_temp == 0 || lowest_temp > temp)
						lowest_temp = temp;
					break;
				}
			}

			/* accept also generic "temp1" */
			if (strcmp(label, "temp1") == 0) {
				log_tag("thermal:ambient:candidate:%d\n", temp);
				if (lowest_temp == 0 || lowest_temp > temp)
					lowest_temp = temp;
			}
		}

		closedir(hwmon_dir);
	}

	closedir(dir);

	return lowest_temp;
}
#else
int ambient_temperature(void)
{
	return 0;
}
#endif

#endif

