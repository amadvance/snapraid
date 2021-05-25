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
	int f;
	int ret;
	int len;
	char buf[512];
	size_t tag_len;
	char* i;
	char* e;

	f = open(path, O_RDONLY);
	if (f == -1) {
		/* LCOV_EXCL_START */
		log_fatal("Failed to open '%s'.\n", path);
		return 0;
		/* LCOV_EXCL_STOP */
	}

	len = read(f, buf, sizeof(buf));
	if (len < 0) {
		/* LCOV_EXCL_START */
		close(f);
		log_fatal("Failed to read '%s'.\n", path);
		return -1;
		/* LCOV_EXCL_STOP */
	}
	if (len == sizeof(buf)) {
		/* LCOV_EXCL_START */
		close(f);
		log_fatal("Too long read '%s'.\n", path);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	ret = close(f);
	if (ret != 0) {
		/* LCOV_EXCL_START */
		log_fatal("Failed to close '%s'.\n", path);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	buf[len] = 0;
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
		log_fatal("Missing tag '%s' for '%s'.\n", tag, path);
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
		log_fatal("Empty tag '%s' for '%s'.\n", tag, path);
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

	while ((dd = readdir(d)) != 0) {
		/* skip "." and ".." files, UUIDs never start with '.' */
		if (dd->d_name[0] == '.')
			continue;

		ret = fstatat(dirfd(d), dd->d_name, &st, 0);
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
 * Get the UUID using liblkid.
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

int devuuid(uint64_t device, char* uuid, size_t uuid_size)
{
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

int fsinfo(const char* path, int* has_persistent_inode, int* has_syncronized_hardlinks, uint64_t* total_space, uint64_t* free_space)
{
	char type[64];
	const char* ptype;

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
		case 0x65735546 : /* FUSE, "fuseblk" in the stat command */
		case 0x4d44 : /* VFAT, "msdos" in the stat command */
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
		case 0x5346544E : /* NTFS */
		case 0x4d44 : /* VFAT, "msdos" in the stat command */
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

#if HAVE_STATFS && HAVE_STRUCT_STATFS_F_FSTYPENAME
	/* get the filesystem type directly from the struct (Mac OS X) */
	(void)type;
	ptype = st.f_fstypename;
#elif HAVE_STATFS && HAVE_STRUCT_STATFS_F_TYPE
	/* get the filesystem type from f_type (Linux) */
	/* from: https://github.com/influxdata/gopsutil/blob/master/disk/disk_linux.go */
	switch (st.f_type) {
	case 0x65735546 : ptype = "fuseblk"; break;
	case 0x4D44 : ptype = "vfat/msdos"; break;
	case 0xEF53 : ptype = "ext2/3/4"; break;
	case 0x6969 : ptype = "nfs"; break; /* remote */
	case 0x6E667364 : ptype = "nfsd"; break; /* remote */
	case 0x517B : ptype = "smb"; break; /* remote */
	case 0x5346544E : ptype = "ntfs"; break;
	case 0x52654973 : ptype = "reiserfs"; break;
	case 0x3153464A : ptype = "jfs"; break;
	case 0x58465342 : ptype = "xfs"; break;
	case 0x9123683E : ptype = "btrfs"; break;
	case 0x2FC12FC1 : ptype = "zfs"; break;
	default :
		snprintf(type, sizeof(type), "0x%X", (unsigned)st.f_type);
		ptype = type;
	}
#else
	(void)type;
	ptype = "unknown";
#endif

	log_tag("statfs:%s: %s \n", ptype, path);

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
		log_fatal("Failed to open '%s'.\n", path);
		return 0;
		/* LCOV_EXCL_STOP */
	}

	len = read(f, buf, sizeof(buf));
	if (len < 0) {
		/* LCOV_EXCL_START */
		close(f);
		log_fatal("Failed to read '%s'.\n", path);
		return 0;
		/* LCOV_EXCL_STOP */
	}
	if (len == sizeof(buf)) {
		/* LCOV_EXCL_START */
		close(f);
		log_fatal("Too long read '%s'.\n", path);
		return 0;
		/* LCOV_EXCL_STOP */
	}

	ret = close(f);
	if (ret != 0) {
		/* LCOV_EXCL_START */
		log_fatal("Failed to close '%s'.\n", path);
		return 0;
		/* LCOV_EXCL_STOP */
	}

	buf[len] = 0;

	ma = strtoul(buf, &e, 10);
	if (*e != ':') {
		/* LCOV_EXCL_START */
		log_fatal("Invalid format in '%s' for '%s'.\n", path, buf);
		return 0;
		/* LCOV_EXCL_STOP */
	}

	mi = strtoul(e + 1, &e, 10);
	if (*e != 0 && !isspace(*e)) {
		/* LCOV_EXCL_START */
		log_fatal("Invalid format in '%s' for '%s'.\n", path, buf);
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
static int devtree(const char* name, const char* custom, dev_t device, devinfo_t* parent, tommy_list* list)
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

				if (devtree(name, custom, subdev, parent, list) != 0) {
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
			log_fatal("Failed to resolve device '%u:%u'.\n", major(device), minor(device));
			return -1;
			/* LCOV_EXCL_STOP */
		}

		devinfo = calloc_nofail(1, sizeof(devinfo_t));

		devinfo->device = device;
		pathcpy(devinfo->name, sizeof(devinfo->name), name);
		pathcpy(devinfo->smartctl, sizeof(devinfo->smartctl), custom);
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
		log_fatal("Failed to open dir '%s'.\n", dir);
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
static int devsmart(dev_t device, const char* name, const char* custom, uint64_t* smart, char* serial, char* vendor, char* model)
{
	char cmd[PATH_MAX + 64];
	char file[PATH_MAX];
	FILE* f;
	int ret;

	if (devresolve(device, file, sizeof(file)) != 0) {
		/* LCOV_EXCL_START */
		log_fatal("Failed to resolve device '%u:%u'.\n", major(device), minor(device));
		return -1;
		/* LCOV_EXCL_STOP */
	}

	/* if there is a custom command */
	if (custom[0]) {
		char option[PATH_MAX];
		snprintf(option, sizeof(option), custom, file);
		snprintf(cmd, sizeof(cmd), "smartctl -a %s", option);
	} else {
		snprintf(cmd, sizeof(cmd), "smartctl -a %s", file);
	}

	log_tag("smartctl:%s:%s:run: %s\n", file, name, cmd);

	f = popen(cmd, "r");
	if (!f) {
		/* LCOV_EXCL_START */
		log_fatal("Failed to run '%s' (from popen).\n", cmd);
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

	if (!WIFEXITED(ret)) {
		/* LCOV_EXCL_START */
		log_fatal("Failed to run '%s' (not exited).\n", cmd);
		return -1;
		/* LCOV_EXCL_STOP */
	}
	if (WEXITSTATUS(ret) == 127) {
		/* LCOV_EXCL_START */
		log_fatal("Failed to run '%s' (from sh).\n", cmd);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	/* store the return smartctl return value */
	smart[SMART_FLAGS] = WEXITSTATUS(ret);

	return 0;
}
#endif

/**
 * Spin down a specific device.
 */
#if HAVE_LINUX_DEVICE
static int devdown(dev_t device, const char* name, const char* custom)
{
	char cmd[PATH_MAX + 64];
	char file[PATH_MAX];
	FILE* f;
	int ret;

	if (devresolve(device, file, sizeof(file)) != 0) {
		/* LCOV_EXCL_START */
		log_fatal("Failed to resolve device '%u:%u'.\n", major(device), minor(device));
		return -1;
		/* LCOV_EXCL_STOP */
	}

	/* if there is a custom command */
	if (custom[0]) {
		char option[PATH_MAX];
		snprintf(option, sizeof(option), custom, file);
		snprintf(cmd, sizeof(cmd), "smartctl -s standby,now %s", option);
	} else {
		snprintf(cmd, sizeof(cmd), "smartctl -s standby,now %s", file);
	}

	log_tag("smartctl:%s:%s:run: %s\n", file, name, cmd);

	f = popen(cmd, "r");
	if (!f) {
		/* LCOV_EXCL_START */
		log_fatal("Failed to run '%s' (from popen).\n", cmd);
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

	if (!WIFEXITED(ret)) {
		/* LCOV_EXCL_START */
		log_fatal("Failed to run '%s' (not exited).\n", cmd);
		return -1;
		/* LCOV_EXCL_STOP */
	}
	if (WEXITSTATUS(ret) == 127) {
		/* LCOV_EXCL_START */
		log_fatal("Failed to run '%s' (from sh).\n", cmd);
		return -1;
		/* LCOV_EXCL_STOP */
	}
	if (WEXITSTATUS(ret) != 0) {
		/* LCOV_EXCL_START */
		log_fatal("Failed to run '%s' with return code %xh.\n", cmd, WEXITSTATUS(ret));
		return -1;
		/* LCOV_EXCL_STOP */
	}

	return 0;
}
#endif

/**
 * Spin up a device.
 *
 * There isn't a defined way to spin up a device,
 * so we just do a generic write.
 */
static int devup(const char* mountpoint)
{
	int ret;
	char path[PATH_MAX];

	/* add a temporary name used for writing */
	pathprint(path, sizeof(path), "%s.snapraid-spinup", mountpoint);

	/* do a generic write, and immediately undo it */
	ret = mkdir(path, 0);
	if (ret != 0 && errno != EEXIST) {
		/* LCOV_EXCL_START */
		log_fatal("Failed to create dir '%s'.\n", path);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	/* remove the just created dir */
	rmdir(path);

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

	/* first get the device number, this usually doesn't trigger a thread_spinup */
	if (stat(devinfo->mount, &st) != 0) {
		/* LCOV_EXCL_START */
		log_fatal("Failed to stat device '%s'.\n", devinfo->mount);
		return (void*)-1;
		/* LCOV_EXCL_STOP */
	}

	/* set the device number for printing */
	devinfo->device = st.st_dev;

	if (devup(devinfo->mount) != 0) {
		/* LCOV_EXCL_START */
		return (void*)-1;
		/* LCOV_EXCL_STOP */
	}

	msg_status("Spunup device '%u:%u' for disk '%s' in %" PRIu64 " ms.\n", major(devinfo->device), minor(devinfo->device), devinfo->name, tick_ms() - start);

	return 0;
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
 * Thread for getting smart info.
 */
static void* thread_smart(void* arg)
{
#if HAVE_LINUX_DEVICE
	devinfo_t* devinfo = arg;

	if (devsmart(devinfo->device, devinfo->name, devinfo->smartctl, devinfo->smart, devinfo->smart_serial, devinfo->smart_vendor, devinfo->smart_model) != 0) {
		/* LCOV_EXCL_START */
		return (void*)-1;
		/* LCOV_EXCL_STOP */
	}

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
	tommy_node* i;
	void* (*func)(void* arg) = 0;

#if HAVE_LINUX_DEVICE
	if (operation != DEVICE_UP) {
		struct stat st;
		/* sysfs interface is required */
		if (stat("/sys/dev/block", &st) != 0) {
			/* LCOV_EXCL_START */
			log_fatal("Missing interface /sys/dev/block.\n");
			return -1;
			/* LCOV_EXCL_STOP */
		}

		/* for each device */
		for (i = tommy_list_head(high); i != 0; i = i->next) {
			devinfo_t* devinfo = i->data;
			uint64_t device = devinfo->device;

			/* if the major is the null device, find the real one */
			if (major(device) == 0) {
				/* obtain the real device */
				if (devdereference(device, &device) != 0) {
					/* LCOV_EXCL_START */
					log_fatal("Failed to dereference device '%u:%u'.\n", major(device), minor(device));
					return -1;
					/* LCOV_EXCL_STOP */
				}
			}

			/* get the device file */
			if (devresolve(device, devinfo->file, sizeof(devinfo->file)) != 0) {
				/* LCOV_EXCL_START */
				log_fatal("Failed to resolve device '%u:%u'.\n", major(device), minor(device));
				return -1;
				/* LCOV_EXCL_STOP */
			}

			/* expand the tree of devices */
			if (devtree(devinfo->name, devinfo->smartctl, device, devinfo, low) != 0) {
				/* LCOV_EXCL_START */
				log_fatal("Failed to expand device '%u:%u'.\n", major(device), minor(device));
				return -1;
				/* LCOV_EXCL_STOP */
			}
		}
	}
#endif

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

#if HAVE_LINUX_DEVICE
	/* add other devices */
	if (others) {
		if (devscan(low) != 0) {
			/* LCOV_EXCL_START */
			log_fatal("Failed to list other devices.\n");
			return -1;
			/* LCOV_EXCL_STOP */
		}
	}
#else
	(void)others;
#endif

	switch (operation) {
	case DEVICE_UP : func = thread_spinup; break;
	case DEVICE_DOWN : func = thread_spindown; break;
	case DEVICE_SMART : func = thread_smart; break;
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
		log_fatal("WARNING Failed to get blkid cache\n");
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
		log_fatal("No page size\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}

	return size;
}

#endif

