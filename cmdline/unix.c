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

int fsinfo(const char* path, int* has_persistent_inode, uint64_t* free_space)
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

	if (free_space)
		*free_space = st.f_bsize * (uint64_t)st.f_bfree;
#else
	(void)path;

	/* by default assume yes */
	if (has_persistent_inode)
		*has_persistent_inode = 1;
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

void os_init(int opt)
{
	(void)opt;
}

void os_done(void)
{
}

#endif

