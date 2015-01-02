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
 * Standard exit codes.
 */
int exit_success = 0;
int exit_failure = 1;

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

#if HAVE_FSTATAT
/**
 * This function get the UUID using the /dev/disk/by-uuid/ links.
 * Comparing with the libblkid library, it works also from not root processes.
 */
int devuuid(uint64_t device, char* uuid, size_t uuid_size)
{
	int ret;
	DIR* d;
	struct stat st;

	/* scan the UUID directory searching for the device */
	d = opendir("/dev/disk/by-uuid");
	if (!d) {
		/* directory missing?, likely we are not in Linux */
		return -1;
	}

	while (1) {
		struct dirent* dd;

		dd = readdir(d);
		if (dd == 0) {
			/* not found or generic error */
			goto bail;
		}

		/* skip "." and ".." files, UUIDs never start with '.' */
		if (dd->d_name[0] == '.') {
			continue;
		}

		ret = fstatat(dirfd(d), dd->d_name, &st, 0);
		if (ret != 0) {
			/* generic error, ignore and continue the search */
			continue;
		}

		/* if it matches, we have the uuid */
		if (S_ISBLK(st.st_mode) && st.st_rdev == (dev_t)device) {
			snprintf(uuid, uuid_size, "%s", dd->d_name);
			break;
		}
	}

	closedir(d);
	return 0;

bail:
	closedir(d);
	return -1;
}
#else
int devuuid(uint64_t device, char* uuid, size_t uuid_size)
{
	(void)device;
	(void)uuid;
	(void)uuid_size;

	/* not supported */
	return -1;
}
#endif

int filephy(const char* path, uint64_t size, uint64_t* physical)
{
#if HAVE_LINUX_FIEMAP_H
	/* In Linux gets the real physical address of the file */
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
 * Returns 0 on error.
 */
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
		fprintf(stderr, "Failed to open '%s'.\n", path);
		return 0;
		/* LCOV_EXCL_STOP */
	}

	ret = read(f, buf, sizeof(buf));
	if (ret < 0) {
		/* LCOV_EXCL_START */
		fprintf(stderr, "Failed to read '%s'.\n", path);
		return 0;
		/* LCOV_EXCL_STOP */
	}
	if (ret == sizeof(buf)) {
		/* LCOV_EXCL_START */
		fprintf(stderr, "Too long read '%s'.\n", path);
		return 0;
		/* LCOV_EXCL_STOP */
	}

	buf[ret] = 0;

	ma = strtoul(buf, &e, 10);
	if (*e != ':') {
		/* LCOV_EXCL_START */
		fprintf(stderr, "Invalid format in '%s' for '%s'.\n", path, buf);
		return 0;
		/* LCOV_EXCL_STOP */
	}

	mi = strtoul(e+1, &e, 10);
	if (*e != 0 && !isspace(*e)) {
		/* LCOV_EXCL_START */
		fprintf(stderr, "Invalid format in '%s' for '%s'.\n", path, buf);
		return 0;
		/* LCOV_EXCL_STOP */
	}

	ret = close(f);
	if (ret != 0) {
		/* LCOV_EXCL_START */
		fprintf(stderr, "Failed to close '%s'.\n", path);
		return 0;
		/* LCOV_EXCL_STOP */
	}

	return makedev(ma, mi);
}

/**
 * Read a device tree filling the specified list of disk_t entries.
 */
static int devtree(dev_t device, tommy_list* list)
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
					return -1;
					/* LCOV_EXCL_STOP */
				}

				if (devtree(device, list) != 0) {
					/* LCOV_EXCL_START */
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
		struct stat st;
		disk_t* disk;
		char buf[PATH_MAX];
		int ret;

		/* check if it's a partition */
		pathprint(path, sizeof(path), "/sys/dev/block/%u:%u/partition", major(device), minor(device));
		if (stat(path, &st) == 0) {
			/* get the parent device */
			pathprint(path, sizeof(path), "/sys/dev/block/%u:%u/../dev", major(device), minor(device));

			device = devread(path);
			if (!device) {
				/* LCOV_EXCL_START */
				return -1;
				/* LCOV_EXCL_STOP */
			}
		}

		/* resolve the link from /dev/block */
		pathprint(path, sizeof(path), "/dev/block/%u:%u", major(device), minor(device));

		ret = readlink(path, buf, sizeof(buf));
		if (ret < 0) {
			/* LCOV_EXCL_START */
			fprintf(stderr, "Failed to readlink '%s'.\n", path);
			return 0;
			/* LCOV_EXCL_STOP */
		}
		if (ret == sizeof(buf)) {
			/* LCOV_EXCL_START */
			fprintf(stderr, "Too long readlink '%s'.\n", path);
			return 0;
			/* LCOV_EXCL_STOP */
		}

		buf[ret] = 0;

		if (buf[0] != '.' || buf[1] != '.' || buf[2] != '/') {
			/* LCOV_EXCL_START */
			fprintf(stderr, "Unexpected link '%s' at '%s'.\n", buf, path);
			return 0;
			/* LCOV_EXCL_STOP */
		}

		disk = malloc_nofail(sizeof(disk_t));

		pathprint(disk->path, sizeof(disk->path), "/dev/%s", buf + 3);
		disk->device = device;

		/* check the device */
		if (stat(path, &st) != 0) {
			/* LCOV_EXCL_START */
			fprintf(stderr, "Failed to stat '%s'.\n", disk->path);
			return 0;
			/* LCOV_EXCL_STOP */
		}
		if (st.st_rdev != device) {
			/* LCOV_EXCL_START */
			fprintf(stderr, "Unexpected device '%u:%u' instead of '%u:%u' for '%s'.\n", major(st.st_rdev), minor(st.st_rdev), major(device), minor(device), disk->path);
			return 0;
			/* LCOV_EXCL_STOP */
		}

		/* insert in the list */
		tommy_list_insert_tail(list, &disk->node, disk);
	}

	return 0;
}

/**
 * Spin down a specific device.
 */
static int devdown(dev_t device)
{
	char cmd[128];
	int ret;

	snprintf(cmd, sizeof(cmd), "hdparm -y /dev/block/%u:%u >/dev/null 2>/dev/null", major(device), minor(device));

	ret = system(cmd);
	if (ret == -1) {
		/* LCOV_EXCL_START */
		fprintf(stderr, "Failed to run hdparm -y on device '%u:%u' (from system).\n", major(device), minor(device));
		return -1;
		/* LCOV_EXCL_STOP */
	}
	if (WEXITSTATUS(ret) == 127) {
		/* LCOV_EXCL_START */
		fprintf(stderr, "Failed to run hdparm -y on device '%u:%u' (from sh).\n", major(device), minor(device));
		return -1;
		/* LCOV_EXCL_STOP */
	}
	if (WEXITSTATUS(ret) != 0) {
		/* LCOV_EXCL_START */
		fprintf(stderr, "Failed to run hdparm -y on device '%u:%u' with return code %u.\n", major(device), minor(device), WEXITSTATUS(ret));
		return -1;
		/* LCOV_EXCL_STOP */
	}

	return 0;
}

/**
 * Compare two device entries.
 */
static int devcompare(const void* void_a, const void* void_b)
{
	const disk_t* a = void_a;
	const disk_t* b = void_b;

	if (a->device < b->device)
		return -1;
	if (a->device > b->device)
		return 1;

	return strcmp(a->path, b->path);
}

/**
 * Thread for spinning up.
 *
 * There isn't a defined way to spin up a device,
 * so we just do a generic write access.
 */
static void* spinup(void* arg)
{
	disk_t* disk = arg;
	dev_t device;
	int ret;
	uint64_t start;
	struct stat st;

	start = tick_ms();

	/* first get the device number, this usually doesn't trigger a spinup */
	if (stat(disk->path, &st) != 0) {
		/* LCOV_EXCL_START */
		fprintf(stderr, "Failed to stat device '%s'.\n", disk->path);
		return (void*)-1;
		/* LCOV_EXCL_STOP */
	}

	/* set the device number for printing */
	device = st.st_dev;

	/* add a temporary name used for writing */
	pathcat(disk->path, sizeof(disk->path), ".snapraid-spinup");

	/* do a generic write, and immediately undo it */
	ret = mkdir(disk->path, 0);
	if (ret == 0)
		rmdir(disk->path);

	/* remove the added name */
	pathcut(disk->path);

	printf("Spunup device '%u:%u' at '%s' in %" PRIu64 " ms.\n", major(device), minor(device), disk->path, tick_ms() - start);

	return 0;
}

/**
 * Thread for spinning down.
 */
static void* spindown(void* arg)
{
	disk_t* disk = arg;
	dev_t device = disk->device;
	uint64_t start;

	start = tick_ms();

	if (devdown(device) != 0) {
		/* LCOV_EXCL_START */
		return (void*)-1;
		/* LCOV_EXCL_STOP */
	}

	printf("Spundown device '%u:%u' at '%s' in %" PRIu64 " ms.\n", major(device), minor(device), disk->path, tick_ms() - start);

	return 0;
}

/**
 * Lists all the devices.
 */
static int spin_devices(tommy_list* list)
{
	tommy_node* i;

	for (i = tommy_list_head(list); i != 0; i = i->next) {
		tommy_list tree;
		tommy_node* j;
		disk_t* disk = i->data;

		tommy_list_init(&tree);

		/* expand the tree of devices */
		if (devtree(disk->device, &tree) != 0) {
			/* LCOV_EXCL_START */
			tommy_list_foreach(&tree, free);
			fprintf(stderr, "Failed to expand device '%u:%u'.\n", major(disk->device), minor(disk->device));
			return -1;
			/* LCOV_EXCL_STOP */
		}

		for (j = tommy_list_head(&tree); j != 0; j = j->next) {
			disk_t* entry = j->data;
			printf("%u:%u\t%s\t%u:%u\t%s\n", major(entry->device), minor(entry->device), entry->path, major(disk->device), minor(disk->device), disk->path);
		}

		tommy_list_foreach(&tree, free);
	}

	return 0;
}

int diskspin(tommy_list* list, int operation)
{
	tommy_node* i;
	tommy_list tree;
	int fail = 0;

	if (operation == SPIN_DOWN || operation == SPIN_DEVICES) {
		struct stat st;
		/* sysfs and devfs interfaces are required */
		if (stat("/sys/dev/block", &st) != 0) {
			/* LCOV_EXCL_START */
			fprintf(stderr, "Missing interface sys/dev/block.\n");
			return -1;
			/* LCOV_EXCL_STOP */
		}
		if (stat("/dev/block", &st) != 0) {
			/* LCOV_EXCL_START */
			fprintf(stderr, "Missing interface dev/block.\n");
			return -1;
			/* LCOV_EXCL_STOP */
		}
	}

	if (operation == SPIN_DEVICES)
		return spin_devices(list);

	tommy_list_init(&tree);

	if (operation == SPIN_DOWN) {
		/* for each device */
		for (i = tommy_list_head(list); i != 0; i = i->next) {
			disk_t* disk = i->data;

			/* expand the tree of devices */
			if (devtree(disk->device, &tree) != 0) {
				/* LCOV_EXCL_START */
				tommy_list_foreach(&tree, free);
				fprintf(stderr, "Failed to expand device '%u:%u'.\n", major(disk->device), minor(disk->device));
				return -1;
				/* LCOV_EXCL_STOP */
			}
		}
	} else {
		/* duplicate the list */
		for (i = tommy_list_head(list); i != 0; i = i->next) {
			disk_t* disk = i->data;
			disk_t* entry;

			entry = malloc_nofail(sizeof(disk_t));

			pathcpy(entry->path, sizeof(entry->path), disk->path);
			entry->device = disk->device;

			/* insert in the list */
			tommy_list_insert_tail(&tree, &entry->node, entry);
		}
	}

	/* sort the list */
	tommy_list_sort(&tree, devcompare);

	/* removes duplicates */
	for (i = tommy_list_head(&tree); i != 0; i = i->next) {
		while (i->next != 0 && devcompare(i->data, i->next->data) == 0)
			free(tommy_list_remove_existing(&tree, i->next));
	}

#if HAVE_PTHREAD_CREATE
	/* starts all threads */
	for (i = tommy_list_head(&tree); i != 0; i = i->next) {
		disk_t* disk = i->data;

		if (pthread_create(&disk->thread, 0, operation == SPIN_UP ? &spinup : &spindown, disk) != 0) {
			/* LCOV_EXCL_START */
			fprintf(stderr, "Failed to create thread.\n");
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}

	/* joins all threads */
	for (i = tommy_list_head(&tree); i != 0; i = i->next) {
		disk_t* disk = i->data;
		void* retval;

		if (pthread_join(disk->thread, &retval) != 0) {
			/* LCOV_EXCL_START */
			fprintf(stderr, "Failed to join thread.\n");
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}

		if (retval != 0)
			++fail;
	}
#else
	for (i = tommy_list_head(&tree); i != 0; i = i->next) {
		disk_t* disk = i->data;

		if (operation == SPIN_UP) {
			if (spinup(disk) != 0)
				++fail;
		} else {
			if (spindown(disk) != 0)
				++fail;
		}
	}
#endif

	tommy_list_foreach(&tree, free);

	if (fail != 0) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	return 0;
}

void os_init(int opt)
{
	(void)opt;
}

void os_done(void)
{
}

#endif

