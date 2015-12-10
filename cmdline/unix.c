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
 * Cache used by blkid.
 */
#if HAVE_BLKID
static blkid_cache cache = 0;
#endif

/**
 * Get the UUID using the /dev/disk/by-uuid/ links.
 * It doesn't require root permission, and the uuid are always updated.
 * It doesn't work with Btrfs filesystems that don't export the main UUID
 * in /dev/disk/by-uuid/.
 */
#if HAVE_FSTATAT
static int devuuid_dev(uint64_t device, char* uuid, size_t uuid_size)
{
	int ret;
	DIR* d;
	struct dirent* dd;
	struct stat st;

	/* scan the UUID directory searching for the device */
	d = opendir("/dev/disk/by-uuid");
	if (!d) {
		/* directory missing?, likely we are not in Linux */
		return -1;
	}

	while ((dd = readdir(d)) != 0) {
		/* skip "." and ".." files, UUIDs never start with '.' */
		if (dd->d_name[0] == '.')
			continue;

		ret = fstatat(dirfd(d), dd->d_name, &st, 0);
		if (ret != 0) {
			/* generic error, ignore and continue the search */
			continue;
		}

		log_tag("uuid:dev:%s:%u:%u:\n", dd->d_name, major(st.st_rdev), minor(st.st_rdev));

		/* if it matches, we have the uuid */
		if (S_ISBLK(st.st_mode) && st.st_rdev == (dev_t)device) {
			/* found */
			pathcpy(uuid, uuid_size, dd->d_name);
			closedir(d);
			return 0;
		}
	}

	/* not found */
	closedir(d);
	return -1;
}
#endif

/**
 * Get the UUID using liblkid.
 * It uses a cache to work without root permission, resultin in UUID
 * not necessarely recent.
 * We could call blkid_probe_all() to refresh the UUID, but it would
 * require root permission to read the superblocks, and to have
 * all the disks spinning.
 */
#if HAVE_BLKID
static int devuuid_blkid(uint64_t device, char* uuid, size_t uuid_size)
{
	char* devname;
	char* uuidname;

	devname = blkid_devno_to_devname(device);
	if (!devname) {
		/* device mapping failed */
		return -1;
	}

	uuidname = blkid_get_tag_value(cache, "UUID", devname);
	if (!uuidname) {
		/* uuid mapping failed */
		free(devname);
		return -1;
	}

	log_tag("uuid:blkid:%s:%u:%u:\n", uuidname, major(device), minor(device));

	pathcpy(uuid, uuid_size, uuidname);

	free(devname);
	free(uuidname);
	return 0;
}
#endif

int devuuid(uint64_t device, char* uuid, size_t uuid_size)
{
	/* first try with the /dev/disk/by-uuid version */
#if HAVE_FSTATAT
	if (devuuid_dev(device, uuid, uuid_size) == 0)
		return 0;
#endif

	/* fall back to blkid for other cases */
#if HAVE_BLKID
	if (devuuid_blkid(device, uuid, uuid_size) == 0)
		return 0;
#endif

	/* not supported */
	(void)device;
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
		*physical = fm.fiemap.fm_extents[0].fe_physical + FILEPHY_REAL_OFFSET;
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
	/* Note also that in newer filesystem with snapshot, like ZFS, */
	/* the inode doesn't represent even less the disk position, because files */
	/* are not overwritten in place, but rewritten in another location */
	/* of the disk. */
	*physical = FILEPHY_UNREPORTED_OFFSET;

	(void)path; /* not used here */
	(void)size;
#endif

	return 0;
}

int fsinfo(const char* path, int* has_persistent_inode, uint64_t* total_space, uint64_t* free_space)
{
#if HAVE_STATFS && HAVE_STRUCT_STATFS_F_TYPE
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

	/* to get the fs type check "man stat" or "stat -f -t FILE" */
	if (has_persistent_inode)
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

	if (total_space)
		*total_space = st.f_bsize * (uint64_t)st.f_blocks;
	if (free_space)
		*free_space = st.f_bsize * (uint64_t)st.f_bfree;
#else
	(void)path;

	/* by default assume yes */
	if (has_persistent_inode)
		*has_persistent_inode = 1;
	if (total_space)
		*total_space = 0;
	if (free_space)
		*free_space = 0;
#endif

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

	ret = read(f, buf, sizeof(buf));
	if (ret < 0) {
		/* LCOV_EXCL_START */
		close(f);
		log_fatal("Failed to read '%s'.\n", path);
		return 0;
		/* LCOV_EXCL_STOP */
	}
	if (ret == sizeof(buf)) {
		/* LCOV_EXCL_START */
		close(f);
		log_fatal("Too long read '%s'.\n", path);
		return 0;
		/* LCOV_EXCL_STOP */
	}

	buf[ret] = 0;

	ma = strtoul(buf, &e, 10);
	if (*e != ':') {
		/* LCOV_EXCL_START */
		close(f);
		log_fatal("Invalid format in '%s' for '%s'.\n", path, buf);
		return 0;
		/* LCOV_EXCL_STOP */
	}

	mi = strtoul(e+1, &e, 10);
	if (*e != 0 && !isspace(*e)) {
		/* LCOV_EXCL_START */
		close(f);
		log_fatal("Invalid format in '%s' for '%s'.\n", path, buf);
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

	return makedev(ma, mi);
}
#endif

#if HAVE_LINUX_DEVICE
/**
 * Get the device file from the device number.
 */
static int devresolve(dev_t device, char* path, size_t path_size)
{
	struct stat st;
	char buf[PATH_MAX];
	int ret;

	/* default device path from device number */
	pathprint(path, path_size, "/dev/block/%u:%u", major(device), minor(device));

	/* resolve the link from /dev/block */
	ret = readlink(path, buf, sizeof(buf));
	if (ret < 0) {
		/* LCOV_EXCL_START */
		log_fatal("Failed to readlink '%s'.\n", path);
		return -1;
		/* LCOV_EXCL_STOP */
	}
	if (ret == sizeof(buf)) {
		/* LCOV_EXCL_START */
		log_fatal("Too long readlink '%s'.\n", path);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	buf[ret] = 0;

	if (buf[0] != '.' || buf[1] != '.' || buf[2] != '/') {
		/* LCOV_EXCL_START */
		log_fatal("Unexpected link '%s' at '%s'.\n", buf, path);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	/* set the real device path */
	pathprint(path, path_size, "/dev/%s", buf + 3);

	/* check the device */
	if (stat(path, &st) != 0) {
		/* LCOV_EXCL_START */
		log_fatal("Failed to stat '%s'.\n", path);
		return -1;
		/* LCOV_EXCL_STOP */
	}
	if (st.st_rdev != device) {
		/* LCOV_EXCL_START */
		log_fatal("Unexpected device '%u:%u' instead of '%u:%u' for '%s'.\n", major(st.st_rdev), minor(st.st_rdev), major(device), minor(device), path);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	return 0;
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
				/* for each slave, expand the full potential tree */
				pathprint(path, sizeof(path), "/sys/dev/block/%u:%u/slaves/%s/dev", major(device), minor(device), dd->d_name);

				device = devread(path);
				if (!device) {
					/* LCOV_EXCL_START */
					closedir(d);
					return -1;
					/* LCOV_EXCL_STOP */
				}

				if (devtree(name, custom, device, parent, list) != 0) {
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
		if (dd->d_name[0] != '.') {
			char path[PATH_MAX];

			pathprint(path, sizeof(path), "/sys/dev/block/%s/device", dd->d_name);

			/* check if it's a real device */
			if (access(path, F_OK) == 0) {
				tommy_node* i;
				dev_t device;

				pathprint(path, sizeof(path), "/sys/dev/block/%s/dev", dd->d_name);

				device = devread(path);
				if (!device) {
					/* LCOV_EXCL_START */
					closedir(d);
					return -1;
					/* LCOV_EXCL_STOP */
				}

				/* check if already present */
				for (i = tommy_list_head(list); i != 0; i = i->next) {
					devinfo_t* devinfo = i->data;
					if (devinfo->device == device)
						break;
				}

				/* if not found */
				if (i == 0) {
					devinfo_t* devinfo;

					/* get the device file */
					if (devresolve(device, path, sizeof(path)) != 0) {
						/* LCOV_EXCL_START */
						closedir(d);
						return -1;
						/* LCOV_EXCL_STOP */
					}

					devinfo = calloc_nofail(1, sizeof(devinfo_t));

					devinfo->device = device;
					pathcpy(devinfo->file, sizeof(devinfo->file), path);

					/* insert in the list */
					tommy_list_insert_tail(list, &devinfo->node, devinfo);
				}
			}
		}
	}

	closedir(d);
	return 0;
}
#endif

/**
 * Get SMART attributes.
 */
static int devsmart(dev_t device, const char* name, const char* custom, uint64_t* smart, char* serial)
{
	char cmd[128];
	char file[128];
	FILE* f;
	int ret;

	snprintf(file, sizeof(file), "/dev/block/%u:%u", major(device), minor(device));

	/* if there is a custom command */
	if (custom[0]) {
		char option[128];
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

	if (smartctl_attribute(f, file, name, smart, serial) != 0) {
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

/**
 * Spin down a specific device.
 */
#if HAVE_LINUX_DEVICE
static int devdown(dev_t device, const char* name, const char* custom)
{
	char cmd[128];
	char file[128];
	FILE* f;
	int ret;

	snprintf(file, sizeof(file), "/dev/block/%u:%u", major(device), minor(device));

	/* if there is a custom command */
	if (custom[0]) {
		char option[128];
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

#if HAVE_LINUX_DEVICE
	/* get the device file, but only in Linux and only for the message */
	if (devresolve(devinfo->device, devinfo->file, sizeof(devinfo->file)) != 0) {
		/* LCOV_EXCL_START */
		return (void*)-1;
		/* LCOV_EXCL_STOP */
	}
#endif

	if (devup(devinfo->mount) != 0) {
		/* LCOV_EXCL_START */
		return (void*)-1;
		/* LCOV_EXCL_STOP */
	}

#if HAVE_LINUX_DEVICE
	msg_status("Spunup device '%s' for disk '%s' in %" PRIu64 " ms.\n", devinfo->file, devinfo->name, tick_ms() - start);
#else
	msg_status("Spunup device '%u:%u' for disk '%s' in %" PRIu64 " ms.\n", major(devinfo->device), minor(devinfo->device), devinfo->name, tick_ms() - start);
#endif

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
	devinfo_t* devinfo = arg;

	if (devsmart(devinfo->device, devinfo->name, devinfo->smartctl, devinfo->smart, devinfo->smart_serial) != 0) {
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

#if HAVE_PTHREAD_CREATE
	/* start all threads */
	for (i = tommy_list_head(list); i != 0; i = i->next) {
		devinfo_t* devinfo = i->data;

		if (pthread_create(&devinfo->thread, 0, func, devinfo) != 0) {
			/* LCOV_EXCL_START */
			log_fatal("Failed to create thread.\n");
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}

	/* join all threads */
	for (i = tommy_list_head(list); i != 0; i = i->next) {
		devinfo_t* devinfo = i->data;
		void* retval;

		if (pthread_join(devinfo->thread, &retval) != 0) {
			/* LCOV_EXCL_START */
			log_fatal("Failed to join thread.\n");
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

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

int devquery(tommy_list* high, tommy_list* low, int operation)
{
	tommy_node* i;
	void* (*func)(void* arg) = 0;

#if HAVE_LINUX_DEVICE
	if (operation != DEVICE_UP) {
		struct stat st;
		/* sysfs and devfs interfaces are required */
		if (stat("/sys/dev/block", &st) != 0) {
			/* LCOV_EXCL_START */
			log_fatal("Missing interface /sys/dev/block.\n");
			return -1;
			/* LCOV_EXCL_STOP */
		}
		if (stat("/dev/block", &st) != 0) {
			/* LCOV_EXCL_START */
			log_fatal("Missing interface /dev/block.\n");
			return -1;
			/* LCOV_EXCL_STOP */
		}

		/* for each device */
		for (i = tommy_list_head(high); i != 0; i = i->next) {
			devinfo_t* devinfo = i->data;

			/* get the device file */
			if (devresolve(devinfo->device, devinfo->file, sizeof(devinfo->file)) != 0) {
				/* LCOV_EXCL_START */
				log_fatal("Failed to resolve device '%u:%u'.\n", major(devinfo->device), minor(devinfo->device));
				return -1;
				/* LCOV_EXCL_STOP */
			}

			/* expand the tree of devices */
			if (devtree(devinfo->name, devinfo->smartctl, devinfo->device, devinfo, low) != 0) {
				/* LCOV_EXCL_START */
				log_fatal("Failed to expand device '%u:%u'.\n", major(devinfo->device), minor(devinfo->device));
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
	if (operation == DEVICE_SMART) {
		if (devscan(low) != 0) {
			/* LCOV_EXCL_START */
			log_fatal("Failed to list other devices.\n");
			return -1;
			/* LCOV_EXCL_STOP */
		}
	}
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

void os_abort(void)
{
#if HAVE_BACKTRACE && HAVE_BACKTRACE_SYMBOLS
	void* stack[32];
	char** messages;
	size_t size;
	unsigned i;

	printf("Stacktrace of " PACKAGE " v" VERSION);
#ifdef _linux
	printf(", linux");
#endif
#ifdef __GNUC__
	printf(", gcc " __VERSION__);
#endif
	printf(", %d-bit", (int)sizeof(void *) * 8);
	printf("\n");

	size = backtrace(stack, 32);

	messages = backtrace_symbols(stack, size);

	for (i=1; i<size; ++i) {
		const char* msg;

		if (messages)
			msg = messages[i];
		else
			msg = "<unknown>";

		printf("[bt] %02u: %s\n", i, msg);

		if (messages) {
			char addr2line[1024];
			size_t j = 0;
			while (msg[j] != '(' && msg[j] != ' ' && msg[j] != 0)
				++j;

			snprintf(addr2line, sizeof(addr2line), "addr2line %p -e %.*s", stack[i], j, msg);
			system(addr2line);
		}
	}
#endif

	abort();
}

#endif

