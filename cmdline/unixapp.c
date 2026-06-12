// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2013 Andrea Mazzoleni

#include "portable.h"

#ifndef __MINGW32__ /* Only for Unix */

#include "os.h"
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

volatile int global_interrupt = 0;

int app_global_interrupt(void)
{
	return global_interrupt;
}

/* LCOV_EXCL_START */
void app_signal_handler(int signum)
{
	/* report the request of interruption with the signal received */
	global_interrupt = signum;
}
/* LCOV_EXCL_STOP */

/****************************************************************************/
/* find */

#define BTRFS_SUPER_MAGIC 0x9123683E
#define BCACHEFS_SUPER_MAGIC 0xCA451A4E
#define ZFS_SUPER_MAGIC 0x2FC12FC1

#if HAVE_LINUX_DEVICE
static const char* const bcachefs_paths[] = {
#ifdef BCACHEFS_PATH
	/* Path configured at build time (e.g. on NixOS). */
	BCACHEFS_PATH,
#else
	"/usr/sbin/bcachefs",
	"/sbin/bcachefs",
	"/usr/local/sbin/bcachefs",
	"/usr/bin/bcachefs",
	"/bin/bcachefs",
	"/usr/local/bin/bcachefs",
#ifdef __APPLE__
	/* macOS (Intel & Apple Silicon) */
	"/opt/homebrew/sbin/bcachefs",
#endif
#endif
	0
};

static const char* find_bcachefs(void)
{
	for (int i = 0; bcachefs_paths[i]; ++i) {
		if (access(bcachefs_paths[i], X_OK) == 0) {
			return bcachefs_paths[i];
		}
	}

	return 0;
}
#endif

#if HAVE_LINUX_DEVICE
static const char* const zfs_paths[] = {
#ifdef ZFS_PATH
	/* Path configured at build time (e.g. on NixOS). */
	ZFS_PATH,
#else
	"/usr/sbin/zfs",
	"/sbin/zfs",
	"/usr/local/sbin/zfs",
	"/usr/bin/zfs",
	"/bin/zfs",
	"/usr/local/bin/zfs",
#ifdef __APPLE__
	/* macOS (Intel & Apple Silicon) */
	"/opt/homebrew/sbin/zfs",
#endif
#endif
	0
};

static const char* find_zfs(void)
{
	for (int i = 0; zfs_paths[i]; ++i) {
		if (access(zfs_paths[i], X_OK) == 0) {
			return zfs_paths[i];
		}
	}
	return 0;
}

static const char* const zpool_paths[] = {
#ifdef ZPOOL_PATH
	/* Path configured at build time (e.g. on NixOS). */
	ZPOOL_PATH,
#else
	"/usr/sbin/zpool",
	"/sbin/zpool",
	"/usr/local/sbin/zpool",
	"/usr/bin/zpool",
	"/bin/zpool",
	"/usr/local/bin/zpool",
#ifdef __APPLE__
	/* macOS (Intel & Apple Silicon) */
	"/opt/homebrew/sbin/zpool",
#endif
#endif
	0
};

static const char* find_zpool(void)
{
	for (int i = 0; zpool_paths[i]; ++i) {
		if (access(zpool_paths[i], X_OK) == 0) {
			return zpool_paths[i];
		}
	}
	return 0;
}
#endif

#if HAVE_LINUX_DEVICE

static const char* smartctl_paths[] = {
#ifdef SMARTCTL_PATH
	/* Path configured at build time (e.g. on NixOS). */
	SMARTCTL_PATH,
#else
	/* Linux & BSD */
	"/usr/sbin/smartctl",
	"/sbin/smartctl",
	"/usr/local/sbin/smartctl",
	"/usr/bin/smartctl",
	"/usr/local/bin/smartctl",
#ifdef __APPLE__
	/* macOS (Intel & Apple Silicon) */
	"/opt/homebrew/sbin/smartctl",
#endif
#endif
	0
};

static const char* find_smartctl(void)
{
	for (int i = 0; smartctl_paths[i]; ++i) {
		if (eaccess(smartctl_paths[i], X_OK) == 0) {
			return smartctl_paths[i];
		}
	}

	return 0;
}
#endif

/****************************************************************************/
/* sys */

/**
 * Read a file from sys
 *
 * Return -1 on error, otherwise the size of data read
 */
#if HAVE_LINUX_DEVICE
static ssize_t sysread(const char* path, char* buf, size_t buf_size)
{
	int f;
	int ret;
	ssize_t len;

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

#if HAVE_LINUX_DEVICE
/*
 * sysread_vpd_pg83 — parse raw binary VPD page 0x83 (Device Identification)
 * from a sysfs vpd_pg83 file and extract the first NAA (Network Address
 * Authority) designator for the Logical Unit (ASSOCIATION == 0x00).
 */
static int sysattr_vpd_pg80(const char* path, char* dst, size_t dst_size)
{
	unsigned char buf[512];
	ssize_t ret = sysread(path, (char*)buf, sizeof(buf));

	/* need at least the header */
	if (ret < 4) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	/* validate page code */
	if (buf[1] != 0x80) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	/* clamp to what was actually read */
	size_t page_len = buf[3];
	size_t available = (size_t)ret - 4;
	if (available < page_len)
		page_len = available;

	/* if empty */
	if (page_len == 0) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	/* if too bit to store the 0 termination */
	if (4 + page_len + 1 > sizeof(buf)) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	char* attr = (char*)buf + 4;
	attr[page_len] = 0;
	strtrim(attr);

	/* if empty */
	if (!*attr) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	pathcpy(dst, dst_size, attr);
	return 0;
}
#endif

/*
 * sysread_vpd_pg83 — parse raw binary VPD page 0x83 (Device Identification)
 * from a sysfs vpd_pg83 file and extract the first NAA (Network Address
 * Authority) designator for the Logical Unit (ASSOCIATION == 0x00).
 *
 * The result is written into dst as a lowercase hex string,
 * e.g.: "5000c500abcdef01"   (8-byte / NAA type 1,2,3,5)
 *       "6000c500abcdef010000000000000000"  (16-byte / NAA type 6)
 *
 * VPD page 0x83 wire format (SPC-4 §7.7.2)
 *
 *  Page header — 4 bytes:
 *    [0]  peripheral qualifier (7:5) | device type (4:0)
 *    [1]  page code  = 0x83
 *    [2]  PAGE LENGTH MSB  }  16-bit big-endian: number of bytes that follow
 *    [3]  PAGE LENGTH LSB  }  the header (i.e. total descriptor bytes)
 *
 *  Then a sequence of Designation Descriptors:
 *    [+0]  PROTOCOL ID (7:4) | CODE SET (3:0)
 *              0x1 = binary, 0x2 = ASCII, 0x3 = UTF-8
 *    [+1]  PIV (7) | reserved (6) | ASSOCIATION (5:4) | DESIGNATOR TYPE (3:0)
 *              ASSOCIATION:      0x0 = logical unit  ← we want only this
 *                                0x1 = target port
 *                                0x2 = target device
 *              DESIGNATOR TYPE:  0x3 = NAA  ← we want only this
 *    [+2]  reserved
 *    [+3]  DESIGNATOR LENGTH = N  (bytes that follow in this descriptor)
 *    [+4 … +4+N-1]  DESIGNATOR (binary for NAA)
 *
 * NAA subtypes (top nibble of designator byte 0)
 *
 *    0x1  NAA IEEE Extended          →  8 bytes
 *    0x2  NAA Locally Assigned       →  8 bytes
 *    0x3  NAA IEEE Registered        →  8 bytes   (most common on SATA/SAS)
 *    0x5  NAA IEEE Registered        →  8 bytes
 *    0x6  NAA IEEE Registered Ext.   → 16 bytes
 *    others: reserved
 *
 * The DESIGNATOR LENGTH from the descriptor header must agree with the
 * expected length for the NAA subtype.  If they disagree, the descriptor
 * is malformed and must be skipped.
 *
 * Returns 0 and fills dst on success, -1 if no valid NAA LU descriptor found.
 */
#if HAVE_LINUX_DEVICE
static int sysattr_vpd_pg83(const char* path, char* dst, size_t dst_size)
{
	unsigned char buf[4096];

	ssize_t ret = sysread(path, (char*)buf, sizeof(buf));
	if (ret < 4) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	/* validate page code */
	if (buf[1] != 0x83) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	size_t page_len = ((size_t)buf[2] << 8) | buf[3];

	/* clamp to what was actually read */
	size_t available = (size_t)ret - 4;
	if (page_len > available)
		page_len = available;

	/* if empty */
	if (page_len == 0) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	/*
	 * Walk every Designation Descriptor looking for the first NAA descriptor
	 * that is associated with the Logical Unit (ASSOCIATION == 0x00).
	 *
	 * We take the first one found rather than scoring: all NAA subtypes are
	 * globally unique by construction; the first LU-associated NAA is the
	 * canonical device WWN.
	 */
	size_t offset = 0;
	while (offset + 4 <= page_len) {
		const unsigned char* desc = buf + 4 + offset;

		unsigned code_set = desc[0] & 0x0f;
		unsigned assoc = (desc[1] >> 4) & 0x03;
		unsigned dtype = desc[1] & 0x0f;
		size_t id_len = desc[3];

		/* bounds check: the full descriptor must lie within page_len */
		if (offset + 4 + id_len > page_len)
			break; /* malformed page — stop iterating */

		/* skip anything that is not an LU-associated NAA descriptor */
		if (assoc != 0x00 || dtype != 0x03)
			goto next;

		/*
		 * NAA is always binary (CODE SET == 0x01).
		 * Reject if the device mis-advertises it as ASCII (defensive).
		 */
		if (code_set != 0x01)
			goto next;

		/* if empty */
		if (id_len == 0)
			goto next;

		/*
		 * Validate NAA subtype vs. expected byte length.
		 * The NAA value is the top nibble of the first byte of the
		 * designator (NOT the descriptor header byte).
		 */
		unsigned naa = (desc[4] >> 4) & 0x0f;
		size_t expected = 0;

		switch (naa) {
		case 0x1 : /* IEEE Extended */
		case 0x2 : /* Locally Assigned */
		case 0x3 : /* IEEE Registered */
		case 0x5 : /* IEEE Registered */
			expected = 8;
			break;
		case 0x6 : /* IEEE Registered Extended */
			expected = 16;
			break;
		default :
			goto next;
		}

		/* DESIGNATOR LENGTH must match the NAA subtype exactly */
		if (id_len != expected)
			goto next;

		if (dst_size < id_len * 2 + 1)
			return -1;

		for (size_t i = 0; i < id_len; i++)
			snprintf(dst + i * 2, 3, "%02x", desc[4 + i]);
		dst[id_len * 2] = 0;

		return 0;

next:
		offset += 4 + id_len;
	}

	return -1;
}
#endif

/**
 * Read a file from sys.
 * Trim spaces.
 * Always put an ending 0.
 * Do not report error on reading.
 * Return an error if truncated
 *
 * Return -1 on error, 0 on success
 */
#if HAVE_LINUX_DEVICE
static int sysattr(const char* path, char* buf, size_t buf_size)
{
	ssize_t len;

	len = sysread(path, buf, buf_size);
	if (len < 0) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	if ((size_t)len + 1 > buf_size) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	buf[len] = 0;

	strtrim(buf);

	return 0;
}
#endif

/******************************************************************************/
/* cpu */

#if HAVE_LINUX_DEVICE
static long long syslong(const char* path)
{
	char buf[32];
	if (sysattr(path, buf, sizeof(buf)) != 0)
		return -1;
	char* end;
	long long v = strtoll(buf, &end, 10);
	if (end == buf)
		return -1;

	return v;
}

static unsigned long long syshex(const char* path)
{
	char buf[1152]; /* enough for 1024 CPUs */
	if (sysattr(path, buf, sizeof(buf)) != 0)
		return 0;

	unsigned long long v = 0;
	for (char* p = buf; *p != 0 && *p != '\n'; ++p) {
		if (*p == ',')
			continue;

		v <<= 4;
		if (*p >= '0' && *p <= '9')
			v |= *p - '0';
		else if (*p >= 'a' && *p <= 'f')
			v |= *p - 'a' + 10;
		else if (*p >= 'A' && *p <= 'F')
			v |= *p - 'A' + 10;
		else
			break; /* hit unexpected character */
	}

	return v;
}

static int cpu_is_online(int i)
{
	char path[256];
	snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/online", i);
	long long v = syslong(path);
	if (v < 0)
		return 1; /* cpu0 has no "online" knob — always on */
	return v;
}

/*
 * Returns 1 if the specified cpu is the lowest-numbered logical CPU in its
 * physical core's SMT sibling set.
 *
 * thread_siblings is a hex bitmask where bit N = logical cpu N.
 * The lowest set bit identifies the primary sibling.
 * Safe for up to 64 logical CPUs (fits in unsigned long).
 */
static int cpu_is_primary_sibling(int i)
{
	char path[256];
	snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/topology/thread_siblings", i);
	unsigned long long mask = syshex(path);
	if (mask == 0) /* assume primary */
		return 1;
	return __builtin_ctzll(mask) == i;
}

#define CPUS_MAX 64

/**
 * os_get_optimal_cpu() – returns the logical CPU index of the "fastest"
 * core using a three-tier strategy:
 *
 *  Tier 1 – ACPI CPPC preferred-core ranking (hardware-binned quality)
 *  Tier 2 – cpufreq max-frequency (P-core vs E-core discrimination)
 *  Tier 3 – SMT topology isolation (primary sibling only)
 */
int os_get_optimal_cpu(void)
{
	char path[256];

	int num_cpus = sysconf(_SC_NPROCESSORS_CONF);
	if (num_cpus <= 0)
		return -1;
	if (num_cpus > CPUS_MAX)
		num_cpus = CPUS_MAX;

	/* ACPI CPPC: highest_perf with nominal_perf tiebreaker */
	long long max_highest = -1;
	long long max_nominal = -1;
	int best_cppc = -1;
	for (int i = 0; i < num_cpus; ++i) {
		if (!cpu_is_online(i))
			continue;

		snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/acpi_cppc/highest_perf", i);
		long long hp = syslong(path);
		if (hp < 0)
			continue;

		snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/acpi_cppc/nominal_perf", i);
		long long np = syslong(path);
		/* np may be missing */

		/* tiebreaker priority: highest_perf > nominal_perf > non-CPU0 */
		int better = (hp > max_highest)
			|| (hp == max_highest && np > max_nominal)
			|| (hp == max_highest && np == max_nominal && best_cppc == 0);
		if (better) {
			max_highest = hp;
			max_nominal = np;
			best_cppc = i;
		}
	}

	if (best_cppc >= 0)
		return best_cppc;

	/* max frequency, then SMT isolation */

	/* find the global maximum frequency among online CPUs */
	long long global_max_freq = -1;
	for (int i = 0; i < num_cpus; ++i) {
		if (!cpu_is_online(i))
			continue;

		snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", i);
		long long freq = syslong(path);
		if (freq < 0)
			continue;

		if (freq > global_max_freq)
			global_max_freq = freq;
	}

	/* first online CPU at max freq that is a primary sibling, preferring non-CPU0 */
	int best_isolated = -1;   /* primary sibling, not CPU 0 */
	int best_any = -1;        /* any matching CPU, not CPU 0 */
	int best_c0 = -1;         /* CPU 0 fallback (always a primary sibling) */

	for (int i = 0; i < num_cpus; ++i) {
		if (!cpu_is_online(i))
			continue;

		if (global_max_freq != -1) {
			snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", i);
			if (syslong(path) != global_max_freq)
				continue;
		}

		if (i == 0) {
			best_c0 = 0;
		} else {
			if (best_isolated == -1 && cpu_is_primary_sibling(i))
				best_isolated = i;
			if (best_any == -1)
				best_any = i;
		}
	}

	/* prefer non-CPU0, fall back to CPU0 only if no alternative exists */
	if (best_isolated != -1)
		return best_isolated;
	if (best_any != -1)
		return best_any;
	if (best_c0 != -1)
		return best_c0;

	return -1;
}
#endif

/******************************************************************************/
/* resolve */

struct dev_struct {
	uint64_t device; /**< Device ID. */
	tommy_node node;
};

#if HAVE_LINUX_DEVICE
static int devdereference_btrfs(uint64_t device, const char* dir, int fd, tommy_list* devlist)
{
	int found = 0;

	struct btrfs_ioctl_fs_info_args fs_info;
	memset(&fs_info, 0, sizeof(fs_info));
	if (ioctl(fd, BTRFS_IOC_FS_INFO, &fs_info) < 0) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	if (fs_info.max_id == 0) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	for (__u64 i = 1; i <= fs_info.max_id; ++i) {
		struct btrfs_ioctl_dev_info_args dev_info;
		memset(&dev_info, 0, sizeof(dev_info));
		dev_info.devid = i;

		if (ioctl(fd, BTRFS_IOC_DEV_INFO, &dev_info) != 0) {
			/* LCOV_EXCL_START */
			/* btrfs device IDs (devid) are not guaranteed to be contiguous */
			if (errno == ENODEV || errno == ENOENT)
				continue; /* this is the expected error on missing id */
			log_error(errno, "Ioctl BTRFS_IOC_DEV_INFO failed. %s.", strerror(errno));
			continue;
			/* LCOV_EXCL_STOP */
		}

		/* get major:minor, use stat on the path returned */
		struct stat st;
		if (stat((char*)dev_info.path, &st) != 0) {
			/* LCOV_EXCL_START */
			return -1;
			/* LCOV_EXCL_STOP */
		}

		struct dev_struct* dev = malloc_nofail(sizeof(struct dev_struct));
		dev->device = st.st_rdev;
		tommy_list_insert_tail(devlist, &dev->node, dev);

		++found;

		log_tag("dereference:btrfs:%s:%u:%u:%u:%u\n", dir, major(device), minor(device), major(dev->device), minor(dev->device));
	}

	/* something has to be found */
	if (found == 0)
		return -1;

	return 0;
}

static int devdereference_bcachefs(uint64_t device, const char* dir, tommy_list* devlist)
{
	char resolved[PATH_MAX];
	char device_list[PATH_MAX * 8];

	if (realpath(dir, resolved) == 0) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	FILE* fd = fopen("/proc/self/mountinfo", "r");
	if (!fd) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
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
	size_t best_len = 0;
	while (1) {
		char buf[PATH_MAX * 2 + 64];
		char* id_map[8];
		unsigned id_mac;
		char* fs_map[8];
		unsigned fs_mac;

		char* s = fgets(buf, sizeof(buf), fd);
		if (s == 0)
			break;

		/* find the separator position */
		char* separator = strstr(s, " - ");
		if (!separator)
			continue;

		/* skip the separator */
		*separator = 0;
		separator += 3;

		/* split the line */
		id_mac = strsplit(id_map, 8, s, " \t\n");
		fs_mac = strsplit(fs_map, 8, separator, " \t\n");

		/* if too short, it's the wrong line */
		if (id_mac < 5)
			continue;
		if (fs_mac < 2)
			continue;

		/* mount point must contain the directory */
		const char* mp = id_map[4];
		size_t mp_len = strlen(mp);
		if (strncmp(resolved, mp, mp_len) != 0)
			continue;
		if (mp_len > 1 && resolved[mp_len] != '/' && resolved[mp_len] != 0)
			continue;

		/* it should be bcachefs */
		const char* fs = fs_map[0];
		if (strcmp(fs, "bcachefs") != 0)
			continue;

		/* keep the longest (innermost) match */
		if (mp_len > best_len) {
			best_len = mp_len;
			pathcpy(device_list, sizeof(device_list), fs_map[1]);
		}
	}

	fclose(fd);

	if (best_len == 0) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	/* split the device list */
	char* dev_map[64];
	unsigned dev_mac = strsplit(dev_map, 64, device_list, ":");
	for (unsigned i = 0; i < dev_mac; ++i) {
		/* get major:minor, use stat on the path returned */
		struct stat st;
		if (stat(dev_map[i], &st) != 0)
			continue;

		struct dev_struct* dev = malloc_nofail(sizeof(struct dev_struct));
		dev->device = st.st_rdev;
		tommy_list_insert_tail(devlist, &dev->node, dev);

		log_tag("dereference:bcachefs:%s:%u:%u:%u:%u\n", dir, major(device), minor(device), major(dev->device), minor(dev->device));
	}

	return 0;
}

static int extract_zfs(const char* dir, char* dataset, size_t dataset_size, char* uuid, size_t uuid_size, char* mount_point, size_t mount_point_size)
{
	char resolved[PATH_MAX];

	const char* zfs = find_zfs();
	if (!zfs) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	if (realpath(dir, resolved) == 0) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	/* list all ZFS filesystems in one shot */
	const char* argv[] = {
		zfs,
		"list",
		"-H",
		"-o",
		"name,guid,mountpoint",
		"-t",
		"filesystem",
		0
	};

	OS_FILE* fp = os_popen(argv, 0);
	if (!fp) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	size_t best_len = 0;
	while (1) {
		char buf[PATH_MAX * 2 + 64];
		char* map[3];
		unsigned mac;

		char* s = os_fgets(buf, sizeof(buf), fp);
		if (s == 0)
			break;

		/* split the line */
		mac = strsplit(map, 3, s, " \t\n");

		if (mac < 3)
			continue;

		const char* mp = map[2];
		size_t mp_len = strlen(mp);
		if (strncmp(resolved, mp, mp_len) != 0)
			continue;
		if (mp_len > 1 && resolved[mp_len] != '/' && resolved[mp_len] != 0)
			continue;

		/* keep the longest (innermost) match */
		if (mp_len > best_len) {
			best_len = mp_len;
			if (dataset)
				pathcpy(dataset, dataset_size, map[0]);
			if (uuid)
				pathcpy(uuid, uuid_size, map[1]);
			if (mount_point)
				pathcpy(mount_point, mount_point_size, map[2]);
		}
	}

	os_pclose(fp);

	if (best_len == 0) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	return 0;
}

static int devdereference_zfs(uint64_t device, const char* dir, tommy_list* devlist)
{
	char pool[PATH_MAX];

	const char* zpool = find_zpool();
	if (!zpool) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	if (extract_zfs(dir, pool, sizeof(pool), 0, 0, 0, 0) != 0) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	/* extract pool name */
	char* slash = strchr(pool, '/');
	if (slash)
		*slash = 0;

	const char* argv[] = {
		zpool,
		"status",
		"-P",
		pool,
		0
	};

	OS_FILE* fp = os_popen(argv, 0);
	if (!fp) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	size_t count = 0;
	while (1) {
		char buf[PATH_MAX * 2 + 64];

		char* s = os_fgets(buf, sizeof(buf), fp);
		if (s == 0)
			break;

		char* file = strstr(s, "/dev/");
		if (!file)
			continue;

		size_t i = 0;
		while (file[i] != 0 && file[i] != ' ' && file[i] != '\t' && file[i] != '\n')
			++i;
		file[i] = 0;

		struct stat st;
		if (stat(file, &st) != 0)
			continue;

		if (!S_ISBLK(st.st_mode))
			continue;

		struct dev_struct* dev = malloc_nofail(sizeof(struct dev_struct));
		dev->device = st.st_rdev;
		tommy_list_insert_tail(devlist, &dev->node, dev);

		log_tag("dereference:zfs:%s:%u:%u:%u:%u\n", dir, major(device), minor(device), major(dev->device), minor(dev->device));
		++count;
	}

	os_pclose(fp);

	if (count == 0) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	return 0;
}

/**
 * Get the devices of a virtual device.
 *
 * This is intended to resolve the case of Btrfs/ZFS filesystems that
 * create a virtual superblock (major==0) not backed by any low
 * level device.
 *
 * See:
 * Bug 711881 - too funny btrfs st_dev numbers
 * https://bugzilla.redhat.com/show_bug.cgi?id=711881
 */
static int devdereference(uint64_t device, const char* dir, tommy_list* devlist)
{
	struct statfs sfs;
	int fd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (fd < 0) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	if (fstatfs(fd, &sfs) != 0) {
		/* LCOV_EXCL_START */
		close(fd);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	int ret;
	if (sfs.f_type == BTRFS_SUPER_MAGIC) {
		ret = devdereference_btrfs(device, dir, fd, devlist);
	} else if (sfs.f_type == BCACHEFS_SUPER_MAGIC) {
		ret = devdereference_bcachefs(device, dir, devlist);
	} else if (sfs.f_type == ZFS_SUPER_MAGIC) {
		ret = devdereference_zfs(device, dir, devlist);
	} else {
		/* insert the device itself in the list */
		struct dev_struct* dev = malloc_nofail(sizeof(struct dev_struct));
		dev->device = device;
		tommy_list_insert_tail(devlist, &dev->node, dev);
		ret = 0;
	}

	close(fd);
	return ret;
}
#endif

/**
 * Read a file extracting the specified tag TAG=VALUE format.
 * Return !=0 on error.
 */
#if HAVE_LINUX_DEVICE
static int tagread(const char* path, const char* tag, char* value, size_t value_size)
{
	ssize_t ret;
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
			if (!isspace((unsigned char)*p))
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
	while (*e != 0 && !isspace((unsigned char)*e))
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

/****************************************************************************/
/* uuid */

/**
 * Get the UUID using the /dev/disk/by-uuid/ links.
 * It doesn't require root permission, and the uuid are always updated.
 * It doesn't work with Btrfs file-systems that don't export the main UUID
 * in /dev/disk/by-uuid/.
 */
#if HAVE_LINUX_DEVICE
static int devuuid_dev(uint64_t device, char* uuid, size_t uuid_size)
{
	ssize_t ret;
	DIR* d;
	struct dirent* dd;
	struct stat st;

	/* scan the UUID directory searching for the device */
	d = opendir("/dev/disk/by-uuid");
	if (!d) {
		/* LCOV_EXCL_START */
		log_tag("uuid:by-uuid:%u:%u: opendir(/dev/disk/by-uuid) failed, %s\n", major(device), minor(device), strerror(errno));
		/* directory missing?, likely we are not in Linux */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	int dir_fd = dirfd(d);
	if (dir_fd == -1) {
		/* LCOV_EXCL_START */
		log_tag("uuid:by-uuid:%u:%u: dirfd(/dev/disk/by-uuid) failed, %s\n", major(device), minor(device), strerror(errno));
		closedir(d);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	while ((dd = readdir(d)) != 0) {
		/* skip "." and ".." files, UUIDs never start with '.' */
		if (dd->d_name[0] == '.')
			continue;

		ret = fstatat(dir_fd, dd->d_name, &st, 0);
		if (ret != 0) {
			/* LCOV_EXCL_START */
			log_tag("uuid:by-uuid:%u:%u: fstatat(%s) failed, %s\n", major(device), minor(device), dd->d_name, strerror(errno));
			/* generic error, ignore and continue the search */
			continue;
			/* LCOV_EXCL_STOP */
		}

		/* if it matches, we have the uuid */
		if (S_ISBLK(st.st_mode) && st.st_rdev == (dev_t)device) {
			char buf[PATH_MAX];
			char path[PATH_MAX];

			/* resolve the link */
			pathprint(path, sizeof(path), "/dev/disk/by-uuid/%s", dd->d_name);
			ret = readlink(path, buf, sizeof(buf));
			if (ret < 0 || ret >= PATH_MAX) {
				/* LCOV_EXCL_START */
				log_tag("uuid:by-uuid:%u:%u: readlink(/dev/disk/by-uuid/%s) failed, %s\n", major(device), minor(device), dd->d_name, strerror(errno));
				/* generic error, ignore and continue the search */
				continue;
				/* LCOV_EXCL_STOP */
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
		/* LCOV_EXCL_START */
		log_tag("uuid:blkid:%u:%u: blkid_devno_to_devname() failed, %s\n", major(device), minor(device), strerror(errno));
		/* device mapping failed */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	uuidname = blkid_get_tag_value(cache, "UUID", devname);
	if (!uuidname) {
		/* LCOV_EXCL_START */
		log_tag("uuid:blkid:%u:%u: blkid_get_tag_value(UUID,%s) failed, %s\n", major(device), minor(device), devname, strerror(errno));
		/* uuid mapping failed */
		free(devname);
		return -1;
		/* LCOV_EXCL_STOP */
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
		/* LCOV_EXCL_START */
		log_tag("label:blkid:%u:%u: blkid_devno_to_devname() failed, %s\n", major(device), minor(device), strerror(errno));
		/* device mapping failed */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	labelname = blkid_get_tag_value(cache, "LABEL", devname);
	if (!labelname) {
		/* LCOV_EXCL_START */
		log_tag("label:blkid:%u:%u: blkid_get_tag_value(LABEL,%s) failed, %s\n", major(device), minor(device), devname, strerror(errno));
		free(devname);
		/* label mapping failed */
		return -1;
		/* LCOV_EXCL_STOP */
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

#if HAVE_LINUX_DEVICE
static int devuuid_btrfs(uint64_t device, const char* dir, char* uuid, size_t uuid_size)
{
	struct statfs sfs;

	int fd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (fd < 0) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP*/
	}

	if (fstatfs(fd, &sfs) != 0) {
		/* LCOV_EXCL_START */
		close(fd);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	if (sfs.f_type != BTRFS_SUPER_MAGIC) {
		close(fd);
		return -1;
	}

	struct btrfs_ioctl_fs_info_args info;
	memset(&info, 0, sizeof(info));
	if (ioctl(fd, BTRFS_IOC_FS_INFO, &info) < 0) {
		/* LCOV_EXCL_START */
		close(fd);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	snprintf(uuid, uuid_size,
		"%02x%02x%02x%02x-"
		"%02x%02x-"
		"%02x%02x-"
		"%02x%02x-"
		"%02x%02x%02x%02x%02x%02x",
		info.fsid[0], info.fsid[1], info.fsid[2], info.fsid[3],
		info.fsid[4], info.fsid[5],
		info.fsid[6], info.fsid[7],
		info.fsid[8], info.fsid[9],
		info.fsid[10], info.fsid[11], info.fsid[12], info.fsid[13], info.fsid[14], info.fsid[15]
	);

	log_tag("uuid:by-btrfs:%u:%u:%s: found %s\n", major(device), minor(device), uuid, dir);

	close(fd);
	return 0;
}
#endif

#if HAVE_LINUX_DEVICE
struct bch_ioctl_query_uuid {
	__u8 uuid[16];
};

#define BCH_IOCTL_QUERY_UUID    _IOR(0xbc, 1, struct bch_ioctl_query_uuid)

static int devuuid_bcachefs(uint64_t device, const char* dir, char* uuid, size_t uuid_size)
{
	struct statfs sfs;

	int fd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (fd < 0) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP*/
	}

	if (fstatfs(fd, &sfs) != 0) {
		/* LCOV_EXCL_START */
		close(fd);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	if (sfs.f_type != BCACHEFS_SUPER_MAGIC) {
		close(fd);
		return -1;
	}

	struct bch_ioctl_query_uuid info;
	memset(&info, 0, sizeof(info));
	if (ioctl(fd, BCH_IOCTL_QUERY_UUID, &info) < 0) {
		/* LCOV_EXCL_START */
		close(fd);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	close(fd);

	snprintf(uuid, uuid_size,
		"%02x%02x%02x%02x-"
		"%02x%02x-"
		"%02x%02x-"
		"%02x%02x-"
		"%02x%02x%02x%02x%02x%02x",
		info.uuid[0], info.uuid[1], info.uuid[2], info.uuid[3],
		info.uuid[4], info.uuid[5],
		info.uuid[6], info.uuid[7],
		info.uuid[8], info.uuid[9],
		info.uuid[10], info.uuid[11], info.uuid[12], info.uuid[13], info.uuid[14], info.uuid[15]
	);

	log_tag("uuid:by-bcachefs:%u:%u:%s: found %s\n", major(device), minor(device), uuid, dir);

	return 0;
}
#endif

#if HAVE_LINUX_DEVICE
int devuuid_zfs(uint64_t device, const char* dir, char* uuid, size_t uuid_size)
{
	if (extract_zfs(dir, 0, 0, uuid, uuid_size, 0, 0) != 0)
		return -1;

	log_tag("uuid:by-zfs:%u:%u:%s: found %s\n", major(device), minor(device), uuid, dir);

	return 0;
}
#endif

int devuuid(uint64_t device_id, const char* device_path, char* uuid, size_t uuid_size)
{
	(void)device_path;
	(void)device_id;

#ifdef __APPLE__
	if (devuuid_darwin(device_path, uuid, uuid_size) == 0)
		return 0;
#endif

#if HAVE_LINUX_DEVICE
	if (devuuid_btrfs(device_id, device_path, uuid, uuid_size) == 0)
		return 0;
#endif

#if HAVE_LINUX_DEVICE
	if (devuuid_bcachefs(device_id, device_path, uuid, uuid_size) == 0)
		return 0;
#endif

#if HAVE_LINUX_DEVICE
	if (devuuid_zfs(device_id, device_path, uuid, uuid_size) == 0)
		return 0;
#endif

#if HAVE_LINUX_DEVICE
	if (devuuid_dev(device_id, uuid, uuid_size) == 0)
		return 0;
#else
	log_tag("uuid:by-uuid:%u:%u: by-uuid not supported\n", major(device_id), minor(device_id));
#endif

#if HAVE_BLKID
	if (devuuid_blkid(device_id, uuid, uuid_size) == 0)
		return 0;
#else
	log_tag("uuid:blkid:%u:%u: blkid support not compiled in\n", major(device_id), minor(device_id));
#endif

	log_tag("uuid:notfound:%u:%u:\n", major(device_id), minor(device_id));

	/* not supported */
	(void)uuid;
	(void)uuid_size;
	return -1;
}

/****************************************************************************/
/* fs */

int filephy(const char* path, uint64_t size, uint64_t* physical)
{
#if HAVE_LINUX_FIEMAP_H
	/*
	 * In Linux get the real physical address of the file
	 * Note that FIEMAP doesn't require root permission
	 */
	int f;
	struct fiemap* fiemap;
	size_t fiemap_size;
	unsigned int blknum;

	f = open(path, O_RDONLY);
	if (f == -1) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	/*
	 * First try with FIEMAP
	 * if works for ext2, ext3, ext4, xfs, btrfs
	 */
	fiemap_size = sizeof(struct fiemap) + sizeof(struct fiemap_extent);
	fiemap = malloc_nofail(fiemap_size);
	memset(fiemap, 0, fiemap_size);
	fiemap->fm_start = 0;
	fiemap->fm_length = ~0ULL;
	fiemap->fm_flags = FIEMAP_FLAG_SYNC; /* required to ensure that just created files report a valid address and not 0 */
	fiemap->fm_extent_count = 1; /* we are interested only at the first block */

	if (ioctl(f, FS_IOC_FIEMAP, fiemap) != -1) {
		uint32_t flags = fiemap->fm_extents[0].fe_flags;
		uint64_t offset = fiemap->fm_extents[0].fe_physical;

		/* check some condition for validating the offset */
		if (flags & FIEMAP_EXTENT_DATA_INLINE) {
			/* if the data is inline, we don't have an offset to report */
			*physical = FILEPHY_WITHOUT_OFFSET;
		} else if (flags & FIEMAP_EXTENT_UNKNOWN) {
			/* if the offset is unknown, we don't have an offset to report */
			*physical = FILEPHY_WITHOUT_OFFSET;
		} else if (offset == 0) {
			/*
			 * 0 is the general fallback for file-systems when
			 * they don't have an offset to report
			 */
			*physical = FILEPHY_WITHOUT_OFFSET;
		} else {
			/* finally report the real offset */
			*physical = offset + FILEPHY_REAL_OFFSET;
		}

		free(fiemap);

		if (close(f) == -1)
			return -1;
		return 0;
	}

	free(fiemap);

	/* if the file is empty, FIBMAP doesn't work, and we don't even try to use it */
	if (size == 0) {
		*physical = FILEPHY_WITHOUT_OFFSET;
		if (close(f) == -1)
			return -1;
		return 0;
	}

	/*
	 * Then try with FIBMAP
	 * it works for jfs, reiserfs, ntfs-3g
	 * in exfat it always returns 0, that it's anyway better than the fake inodes
	 */
	blknum = 0; /* first block */
	if (ioctl(f, FIBMAP, &blknum) != -1) {
		*physical = blknum + FILEPHY_REAL_OFFSET;
		if (close(f) == -1)
			return -1;
		return 0;
	}

	/*
	 * Otherwise don't use anything, and keep the directory traversal order
	 * at now this should happen only for vfat
	 * and it's surely better than using fake inodes
	 */
	*physical = FILEPHY_UNREPORTED_OFFSET;
	if (close(f) == -1)
		return -1;
#else
	/*
	 * In a generic Unix use a dummy value for all the files
	 * We don't want to risk to use the inode without knowing
	 * if it really improves performance.
	 * In this way we keep them in the directory traversal order
	 * that at least keeps files in the same directory together.
	 * Note also that in newer file-system with snapshot, like ZFS,
	 * the inode doesn't represent even more the disk position, because files
	 * are not overwritten in place, but rewritten in another location
	 * of the disk.
	 */
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
	{ BCACHEFS_SUPER_MAGIC, "bcachefs", 0 },
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
			/* LCOV_EXCL_START */
			return -1;
			/* LCOV_EXCL_STOP */
		}

		/*
		 * If it doesn't exist, we assume a file
		 * and we check for the containing dir
		 */
		if (strlen(path) + 1 > sizeof(dir)) {
			/* LCOV_EXCL_START */
			errno = ENAMETOOLONG;
			return -1;
			/* LCOV_EXCL_STOP */
		}

		strcpy(dir, path);

		slash = strrchr(dir, '/');
		if (!slash) {
			/* LCOV_EXCL_START */
			return -1;
			/* LCOV_EXCL_STOP */
		}

		*slash = 0;
		if (statfs(dir, &st) != 0) {
			/* LCOV_EXCL_START */
			return -1;
			/* LCOV_EXCL_STOP */
		}
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
	/*
	 * Get the filesystem type from f_type (Linux)
	 * from: https://github.com/influxdata/gopsutil/blob/master/disk/disk_linux.go
	 */
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

#if HAVE_LINUX_DEVICE
static int fssnapshot_inode(const char* path, uint32_t magic, uint64_t root_inode, struct fssnapshot_struct* fss)
{
	char current_path[PATH_MAX];

	pathcpy(current_path, sizeof(current_path), path);

	/* walk up the directory tree to find the subvolume root (Inode 256) */
	while (1) {
		struct stat st;
		if (stat(current_path, &st) != 0) {
			/* LCOV_EXCL_START */
			log_error(errno, "Error stating '%s'. %s.\n", current_path, strerror(errno));
			return -1;
			/* LCOV_EXCL_STOP */
		}

		if (st.st_ino == root_inode) {
			/* copy the subvol root */
			pathcpy(fss->root_dir, sizeof(fss->root_dir), current_path);
			pathcpy(fss->snapshot_dir, sizeof(fss->snapshot_dir), current_path);
			pathcat(fss->snapshot_dir, sizeof(fss->snapshot_dir), SNAPSHOT_CONTAINER "/");

			/* create the snapshot container directory if not existing */
			int ret = mkdir(fss->snapshot_dir, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
			if (ret != 0 && errno != EEXIST) {
				log_error(errno, "Error creating '%s'. %s.\n", fss->snapshot_dir, strerror(errno));
				return -1;
			}

			fss->magic = magic;
			return 0;
		}

		/* move to parent directory */
		pathup(current_path);

		if (current_path[0] == 0) {
			/* LCOV_EXCL_START */
			log_error(ENOENT, "No subvolume root found for '%s'. %s.\n", path, strerror(errno));
			return -1;
			/* LCOV_EXCL_STOP */
		}
	}
}
#endif

#if HAVE_LINUX_DEVICE
static int fssnapshot_zfs(const char* dir, uint32_t magic, struct fssnapshot_struct* fss)
{
	if (extract_zfs(dir, fss->dataset, sizeof(fss->dataset), 0, 0, fss->root_dir, sizeof(fss->root_dir)) != 0)
		return -1;

	pathslash(fss->root_dir, sizeof(fss->root_dir));
	pathcpy(fss->snapshot_dir, sizeof(fss->snapshot_dir), fss->root_dir);
	pathcat(fss->snapshot_dir, sizeof(fss->snapshot_dir), ".zfs/snapshot/");

	/* ensure ZFS snapshot directory is visible */
	if (access(fss->snapshot_dir, R_OK | X_OK) != 0) {
		log_error(errno, "ZFS snapshot directory not accessible: '%s'. %s.\n", fss->snapshot_dir, strerror(errno));
		return -1;
	}

	fss->magic = magic;
	return 0;
}
#endif

int fssnapshot_mount(const char* path, struct fssnapshot_struct* fss)
{
#if HAVE_LINUX_DEVICE
	struct statfs sfs;

	if (statfs(path, &sfs) != 0) {
		/* LCOV_EXCL_START */
		log_error(errno, "Error stating filesystem '%s'. %s.\n", path, strerror(errno));
		return -1;
		/* LCOV_EXCL_STOP */
	}

	if (sfs.f_type == BTRFS_SUPER_MAGIC) {
		/* btrfs reserved inode 256 for subvolume roots */
		return fssnapshot_inode(path, BTRFS_SUPER_MAGIC, 256, fss);
	} else if (sfs.f_type == BCACHEFS_SUPER_MAGIC) {
		/* Bcachefs reserves inode 4096 for each subvolume's root directory */
		return fssnapshot_inode(path, BCACHEFS_SUPER_MAGIC, 4096, fss);
	} else if (sfs.f_type == ZFS_SUPER_MAGIC) {
		return fssnapshot_zfs(path, ZFS_SUPER_MAGIC, fss);
	} else {
		return -1;
	}
#else
	(void)path;
	(void)fss;
	return -1;
#endif
}

#if HAVE_LINUX_DEVICE
static int fssnapshot_stat_fs(const struct fssnapshot_struct* fss, const char* name, struct stat* st)
{
	char target_path[PATH_MAX];

	pathcpy(target_path, sizeof(target_path), fss->snapshot_dir);
	pathcat(target_path, sizeof(target_path), name);

	if (stat(target_path, st) != 0)
		return -1;

	/* it must be a directory */
	if (!S_ISDIR(st->st_mode))
		return -1;

	return 0;
}
#endif

#if HAVE_LINUX_DEVICE
static int fssnapshot_stat_zfs(const struct fssnapshot_struct* fss, const char* name, struct stat* st)
{
	char target_path[PATH_MAX];

	pathcpy(target_path, sizeof(target_path), fss->snapshot_dir);
	pathcat(target_path, sizeof(target_path), name);

	/*
	 * Ensure to get into into the snapshot
	 *
	 * This is required by ZFS, otherwise we get a different st_dev
	 */
	pathcat(target_path, sizeof(target_path), "/.");

	if (stat(target_path, st) != 0)
		return -1;

	/* it must be a directory */
	if (!S_ISDIR(st->st_mode))
		return -1;

	/**
	 * When verifying if a ZFS snapshot has been successfully deleted, a direct
	 * path lookup (stat, access, open) is unreliable. A directory listing
	 * (readdir) of the .zfs/snapshot directory must be used instead.
	 *
	 * On Linux, the Virtual File System (VFS) maintains a 'dentry cache'
	 * (dcache). If a snapshot path was accessed recently, the kernel caches
	 * the directory entry in RAM. When ZFS destroys the snapshot, the kernel
	 * dcache is not always immediately synchronized. Consequently, stat()
	 * may hit the stale cache and report the snapshot still exists.
	 *
	 * Unlike stat(), a directory listing (ls/readdir) forces the kernel to
	 * query the ZFS module for the current set of valid entries, bypassing
	 * the stale dcache for a "ground truth" check.
	 *
	 * This behavior can be confirmed by forcing the kernel to purge its
	 * dcache manually using the following command:
	 *     sync; echo 2 > /proc/sys/vm/drop_caches
	 *
	 * After dropping caches, stat() will correctly return ENOENT. To avoid
	 * requiring root privileges or impacting system performance with
	 * drop_caches, always use readdir() for existence checks in this context.
	 */
	DIR* d = opendir(fss->snapshot_dir);
	if (!d) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	while (1) {
		struct dirent* entry = readdir(d);
		if (!entry)
			break;

		if (strcmp(entry->d_name, name) == 0) {
			closedir(d);
			return 0;
		}
	}

	closedir(d);
	return -1;
}
#endif

int fssnapshot_stat(struct fssnapshot_struct* fss, const char* name, struct stat* st)
{
#if HAVE_LINUX_DEVICE
	if (fss->magic == ZFS_SUPER_MAGIC) {
		return fssnapshot_stat_zfs(fss, name, st);
	} else {
		return fssnapshot_stat_fs(fss, name, st);
	}
#else
	(void)fss;
	(void)name;
	(void)st;
	return 0;
#endif
}

#if HAVE_LINUX_DEVICE
int fssnapshot_btrfs_create(const struct fssnapshot_struct* fss, const char* name)
{
	struct btrfs_ioctl_vol_args_v2 args;
	int fd_source;
	int fd_dest_parent;
	int ret;

	/* open the source subvolume to get a file descriptor */
	fd_source = open(fss->root_dir, O_RDONLY | O_DIRECTORY);
	if (fd_source < 0) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	fd_dest_parent = open(fss->snapshot_dir, O_RDONLY | O_DIRECTORY);
	if (fd_dest_parent < 0) {
		/* LCOV_EXCL_START */
		close(fd_source);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	memset(&args, 0, sizeof(args));
	args.fd = fd_source;
	args.flags = BTRFS_SUBVOL_RDONLY;
	pathcpy(args.name, BTRFS_SUBVOL_NAME_MAX, name);

	/* issue the snapshot command to the PARENT directory of the destination */
	ret = ioctl(fd_dest_parent, BTRFS_IOC_SNAP_CREATE_V2, &args);
	if (ret < 0) {
		/* LCOV_EXCL_START */
		close(fd_source);
		close(fd_dest_parent);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	close(fd_source);
	close(fd_dest_parent);

	return 0;
}
#endif

#if HAVE_LINUX_DEVICE
static int fssnapshot_bcachefs_create(const struct fssnapshot_struct* fss, const char* name)
{
	char target_path[PATH_MAX];
	int ret;

	const char* bcachefs = find_bcachefs();
	if (!bcachefs) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	pathcpy(target_path, sizeof(target_path), fss->snapshot_dir);
	pathcat(target_path, sizeof(target_path), name);

	if (os_validate_exec_input(fss->root_dir) != 0 || os_validate_exec_input(target_path) != 0) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	const char* argv[] = {
		bcachefs,
		"subvolume",
		"snapshot",
		"-r",
		fss->root_dir,
		target_path,
		0
	};

	ret = os_spawn_and_wait(argv);
	if (ret != 0) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	return 0;
}
#endif

#if HAVE_LINUX_DEVICE
static int fssnapshot_zfs_create(const struct fssnapshot_struct* fss, const char* name)
{
	char snapshot[PATH_MAX + 64];
	int ret;

	const char* zfs = find_zfs();
	if (!zfs) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	snprintf(snapshot, sizeof(snapshot), "%s@%s", fss->dataset, name);

	if (os_validate_exec_input(snapshot) != 0) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	const char* argv[] = {
		zfs,
		"snapshot",
		snapshot,
		0
	};

	ret = os_spawn_and_wait(argv);
	if (ret != 0) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	return 0;
}
#endif

int fssnapshot_create(const struct fssnapshot_struct* fss, const char* name)
{
#if HAVE_LINUX_DEVICE
	if (fss->magic == BTRFS_SUPER_MAGIC) {
		return fssnapshot_btrfs_create(fss, name);
	} else if (fss->magic == BCACHEFS_SUPER_MAGIC) {
		return fssnapshot_bcachefs_create(fss, name);
	} else if (fss->magic == ZFS_SUPER_MAGIC) {
		return fssnapshot_zfs_create(fss, name);
	} else {
		return -1;
	}
#else
	(void)fss;
	(void)name;
	return -1;
#endif
}

#if HAVE_LINUX_DEVICE
int fssnapshot_btrfs_delete(const struct fssnapshot_struct* fss, const char* name)
{
	struct btrfs_ioctl_vol_args args;
	int fd_parent;
	int ret;

	fd_parent = open(fss->snapshot_dir, O_RDONLY | O_DIRECTORY);
	if (fd_parent < 0) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	memset(&args, 0, sizeof(args));
	pathcpy(args.name, BTRFS_PATH_NAME_MAX, name);

	ret = ioctl(fd_parent, BTRFS_IOC_SNAP_DESTROY, &args);
	if (ret < 0) {
		/* LCOV_EXCL_START */
		close(fd_parent);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	close(fd_parent);
	return 0;
}
#endif

#if HAVE_LINUX_DEVICE
static int fssnapshot_bcachefs_delete(const struct fssnapshot_struct* fss, const char* name)
{
	char target_path[PATH_MAX];
	int ret;

	const char* bcachefs = find_bcachefs();
	if (!bcachefs) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	pathcpy(target_path, sizeof(target_path), fss->snapshot_dir);
	pathcat(target_path, sizeof(target_path), name);

	if (os_validate_exec_input(target_path) != 0) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	const char* argv[] = {
		bcachefs,
		"subvolume",
		"delete",
		target_path,
		0,
	};

	ret = os_spawn_and_wait(argv);
	if (ret != 0) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	return 0;
}
#endif

#if HAVE_LINUX_DEVICE
static int fssnapshot_zfs_delete(const struct fssnapshot_struct* fss, const char* name)
{
	char snapshot[PATH_MAX + 64];
	int ret;

	const char* zfs = find_zfs();
	if (!zfs) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	snprintf(snapshot, sizeof(snapshot), "%s@%s", fss->dataset, name);

	if (os_validate_exec_input(snapshot) != 0) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	const char* argv[] = {
		zfs,
		"destroy",
		snapshot,
		0
	};

	ret = os_spawn_and_wait(argv);
	if (ret != 0) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	return 0;
}
#endif

int fssnapshot_delete(const struct fssnapshot_struct* fss, const char* name)
{
#if HAVE_LINUX_DEVICE
	if (fss->magic == BTRFS_SUPER_MAGIC) {
		return fssnapshot_btrfs_delete(fss, name);
	} else if (fss->magic == BCACHEFS_SUPER_MAGIC) {
		return fssnapshot_bcachefs_delete(fss, name);
	} else if (fss->magic == ZFS_SUPER_MAGIC) {
		return fssnapshot_zfs_delete(fss, name);
	} else {
		return -1;
	}
#else
	(void)fss;
	(void)name;
	return -1;
#endif
}

#if HAVE_LINUX_DEVICE
static int fssnapshot_btrfs_rename(const struct fssnapshot_struct* fss, const char* old_name, const char* new_name)
{
	char old_path[PATH_MAX];
	char new_path[PATH_MAX];

	pathcpy(old_path, sizeof(old_path), fss->snapshot_dir);
	pathcat(old_path, sizeof(old_path), old_name);
	pathcpy(new_path, sizeof(new_path), fss->snapshot_dir);
	pathcat(new_path, sizeof(new_path), new_name);

	if (rename(old_path, new_path) < 0) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	return 0;
}
#endif

#if HAVE_LINUX_DEVICE
static int fssnapshot_bcachefs_rename(const struct fssnapshot_struct* fss, const char* old_name, const char* new_name)
{
	if (fssnapshot_bcachefs_create(fss, new_name) != 0) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	if (fssnapshot_bcachefs_delete(fss, old_name) != 0) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	return 0;
}
#endif

#if HAVE_LINUX_DEVICE
static int fssnapshot_zfs_rename(const struct fssnapshot_struct* fss, const char* old_name, const char* new_name)
{
	char old_snapshot[PATH_MAX + 64];
	char new_snapshot[PATH_MAX + 64];
	int ret;

	const char* zfs = find_zfs();
	if (!zfs) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	snprintf(old_snapshot, sizeof(old_snapshot), "%s@%s", fss->dataset, old_name);
	snprintf(new_snapshot, sizeof(new_snapshot), "%s@%s", fss->dataset, new_name);

	if (os_validate_exec_input(old_snapshot) != 0 || os_validate_exec_input(new_snapshot) != 0) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	const char* argv[] = {
		zfs,
		"rename",
		old_snapshot,
		new_snapshot,
		0
	};

	ret = os_spawn_and_wait(argv);
	if (ret != 0) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	return 0;
}
#endif

int fssnapshot_rename(const struct fssnapshot_struct* fss, const char* old_name, const char* new_name)
{
#if HAVE_LINUX_DEVICE
	if (fss->magic == BTRFS_SUPER_MAGIC) {
		return fssnapshot_btrfs_rename(fss, old_name, new_name);
	} else if (fss->magic == BCACHEFS_SUPER_MAGIC) {
		return fssnapshot_bcachefs_rename(fss, old_name, new_name);
	} else if (fss->magic == ZFS_SUPER_MAGIC) {
		return fssnapshot_zfs_rename(fss, old_name, new_name);
	} else {
		return -1;
	}
#else
	(void)fss;
	(void)old_name;
	(void)new_name;
	return -1;
#endif
}

void fssnapshot_unmount(const struct fssnapshot_struct* fss)
{
	(void)fss;
}

/****************************************************************************/
/* dev */

/**
 * Read a file extracting the contained device number in %u:%u format.
 * Return 0 on error.
 */
#if HAVE_LINUX_DEVICE
static dev_t devread(const char* path)
{
	int f;
	int ret;
	ssize_t len;
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

	ma = strtou(buf, &e, 10);
	if (*e != ':') {
		/* LCOV_EXCL_START */
		log_fatal(EEXTERNAL, "Invalid format in '%s' for '%s'.\n", path, buf);
		return 0;
		/* LCOV_EXCL_STOP */
	}

	mi = strtou(e + 1, &e, 10);
	if (*e != 0 && !isspace((unsigned char)*e)) {
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
		if (eaccess(path, F_OK) != 0) {
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
 * Compute disk usage by aggregating access statistics.
 */
#if HAVE_LINUX_DEVICE
static int devstat(dev_t device, uint64_t* count)
{
	char path[PATH_MAX];
	char buf[512];
	int token;
	ssize_t ret;
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
		while (*i && isspace((unsigned char)*i))
			++i;

		/* read digits */
		n = i;
		while (*i && isdigit((unsigned char)*i))
			++i;

		if (i == n) /* if no digit, abort */
			break;
		if (*i == 0 || !isspace((unsigned char)*i)) /* if no space, abort */
			break;
		*i++ = 0; /* put a terminator */

		v = strtoull(n, &e, 10);
		if (*e != 0) {
			/* LCOV_EXCL_START */
			break;
			/* LCOV_EXCL_STOP */
		}

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
 * Get SMART attributes.
 */
#if HAVE_LINUX_DEVICE
static int devsmart(dev_t device, const char* name, const char* smartctl, struct smart_attr* smart, uint64_t* info, char* serial, char* family, char* model, char* interface)
{
	char extra_args[PATH_MAX];
	char file[PATH_MAX];
	OS_FILE* f;
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
	const char* args = "-a";
	if (smartctl[0]) {
		pathprint(extra_args, sizeof(extra_args), smartctl, file);
		const char* argv[] = {
			x,
			"-a",
			0
		};
		log_tag("smartctl:%s:%s:run: %s %s %s\n", file, name, x, args, extra_args);
		f = os_popen(argv, extra_args);
	} else {
		pathprint(extra_args, sizeof(extra_args), "%s", file);
		const char* argv[] = {
			x,
			"-a",
			file,
			0
		};
		log_tag("smartctl:%s:%s:run: %s %s %s\n", file, name, x, args, extra_args);
		f = os_popen(argv, 0);
	}
	if (!f) {
		/* LCOV_EXCL_START */
		log_tag("device:%s:%s:spawn\n", file, name);
		log_fatal(EEXTERNAL, "Failed to run '%s %s %s'.\n", x, args, extra_args);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	if (smartctl_attribute(f, file, name, smart, info, serial, family, model, interface) != 0) {
		/* LCOV_EXCL_START */
		os_pclose(f);
		log_tag("device:%s:%s:spawn\n", file, name);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	ret = os_pclose(f);

	log_tag("smartctl:%s:%s:ret: %x\n", file, name, ret);

	if (!WIFEXITED(ret)) {
		/* LCOV_EXCL_START */
		log_tag("device:%s:%s:abort\n", file, name);
		log_fatal(EEXTERNAL, "Failed to run '%s %s %s' (not exited).\n", x, args, extra_args);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	/* store the return smartctl return value */
	smart[SMART_FLAGS].raw = WEXITSTATUS(ret);

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
	ssize_t ret;

	(void)family; /* not available, smartctl uses an internal database to get it */

	if (info[INFO_SIZE] == SMART_UNASSIGNED) {
		pathprint(path, sizeof(path), "/sys/dev/block/%u:%u/size", major(device), minor(device));
		if (sysattr(path, buf, sizeof(buf)) == 0) {
			char* e;
			uint64_t v;
			v = strtoul(buf, &e, 10);
			if (*e == 0)
				info[INFO_SIZE] = v * 512;
		}
	}

	if (info[INFO_ROTATION_RATE] == SMART_UNASSIGNED) {
		pathprint(path, sizeof(path), "/sys/dev/block/%u:%u/queue/rotational", major(device), minor(device));
		if (sysattr(path, buf, sizeof(buf)) == 0) {
			char* e;
			uint64_t v;
			v = strtoul(buf, &e, 10);
			if (*e == 0)
				info[INFO_ROTATION_RATE] = v;
		}
	}

	if (*model == 0) {
		pathprint(path, sizeof(path), "/sys/dev/block/%u:%u/device/model", major(device), minor(device));
		if (sysattr(path, buf, sizeof(buf)) == 0) {
			if (buf[0] != 0)
				pathcpy(model, SMART_MAX, buf);
		}
	}

	if (*serial == 0) {
		pathprint(path, sizeof(path), "/sys/dev/block/%u:%u/device/serial", major(device), minor(device));
		if (sysattr(path, buf, sizeof(buf)) == 0) {
			if (buf[0] != 0)
				pathcpy(serial, SMART_MAX, buf);
		}
	}

	if (*serial == 0) {
		pathprint(path, sizeof(path), "/sys/dev/block/%u:%u/device/vpd_pg80", major(device), minor(device));
		if (sysattr_vpd_pg80(path, buf, sizeof(buf)) == 0) {
			if (buf[0] != 0)
				pathcpy(serial, SMART_MAX, buf);
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
static int devprobe(dev_t device, const char* name, const char* smartctl, int* power, struct smart_attr* smart, uint64_t* info, char* serial, char* family, char* model, char* interface)
{
	char extra_args[PATH_MAX];
	char file[PATH_MAX];
	OS_FILE* f;
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
	const char* args = "-n standby,3 -a";
	if (smartctl[0]) {
		pathprint(extra_args, sizeof(extra_args), smartctl, file);
		const char* argv[] = {
			x,
			"-n",
			"standby,3",
			"-a",
			0
		};
		log_tag("smartctl:%s:%s:run: %s %s %s\n", file, name, x, args, extra_args);
		f = os_popen(argv, extra_args);
	} else {
		pathprint(extra_args, sizeof(extra_args), "%s", file);
		const char* argv[] = {
			x,
			"-n",
			"standby,3",
			"-a",
			file,
			0
		};
		log_tag("smartctl:%s:%s:run: %s %s %s\n", file, name, x, args, extra_args);
		f = os_popen(argv, 0);
	}
	if (!f) {
		/* LCOV_EXCL_START */
		log_tag("device:%s:%s:spawn\n", file, name);
		log_fatal(EEXTERNAL, "Failed to run '%s %s %s'.\n", x, args, extra_args);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	if (smartctl_attribute(f, file, name, smart, info, serial, family, model, interface) != 0) {
		/* LCOV_EXCL_START */
		os_pclose(f);
		log_tag("device:%s:%s:spawn\n", file, name);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	ret = os_pclose(f);

	log_tag("smartctl:%s:%s:ret: %x\n", file, name, ret);

	if (!WIFEXITED(ret)) {
		/* LCOV_EXCL_START */
		log_tag("device:%s:%s:abort\n", file, name);
		log_fatal(EEXTERNAL, "Failed to run '%s %s %s' (not exited).\n", x, args, extra_args);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	if (WEXITSTATUS(ret) == 3) {
		log_tag("attr:%s:%s:power:standby\n", file, name);
		*power = POWER_STANDBY;
	} else {
		log_tag("attr:%s:%s:power:active\n", file, name);
		*power = POWER_ACTIVE;

		/* store the smartctl return value */
		if (smart)
			smart[SMART_FLAGS].raw = WEXITSTATUS(ret);
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
	char extra_args[PATH_MAX];
	char file[PATH_MAX];
	OS_FILE* f;
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
	const char* args = "-s standby,now";
	if (smartctl[0]) {
		pathprint(extra_args, sizeof(extra_args), smartctl, file);
		const char* argv[] = {
			x,
			"-s",
			"standby,now",
			0
		};
		log_tag("smartctl:%s:%s:run: %s %s %s\n", file, name, x, args, extra_args);
		f = os_popen(argv, extra_args);
	} else {
		pathprint(extra_args, sizeof(extra_args), "%s", file);
		const char* argv[] = {
			x,
			"-s",
			"standby,now",
			file,
			0
		};
		log_tag("smartctl:%s:%s:run: %s %s %s\n", file, name, x, args, extra_args);
		f = os_popen(argv, 0);
	}
	if (!f) {
		/* LCOV_EXCL_START */
		log_tag("device:%s:%s:spawn\n", file, name);
		log_fatal(EEXTERNAL, "Failed to run '%s %s %s'.\n", x, args, extra_args);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	if (smartctl_flush(f, file, name) != 0) {
		/* LCOV_EXCL_START */
		os_pclose(f);
		log_tag("device:%s:%s:spawn\n", file, name);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	ret = os_pclose(f);

	log_tag("smartctl:%s:%s:ret: %x\n", file, name, ret);

	if (!WIFEXITED(ret)) {
		/* LCOV_EXCL_START */
		log_tag("device:%s:%s:abort\n", file, name);
		log_fatal(EEXTERNAL, "Failed to run '%s %s %s' (not exited).\n", x, args, extra_args);
		return -1;
		/* LCOV_EXCL_STOP */
	}

	if (WEXITSTATUS(ret) != 0) {
		/* LCOV_EXCL_START */
		log_tag("device:%s:%s:exit:%d\n", file, name, WEXITSTATUS(ret));
		log_fatal(EEXTERNAL, "Failed to run '%s %s %s' with return code %xh.\n", x, args, extra_args, WEXITSTATUS(ret));
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
	ssize_t ret;
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
	uint64_t blocks = size / 4096;
	offset = blocks > 0 ? (random_u64() % blocks) * 4096 : 0;

#if HAVE_POSIX_FADVISE
	/* clear cache */
	ret = posix_fadvise_wrapper(f, offset, 4096, POSIX_FADV_DONTNEED);
	if (ret == ENOSYS) {
		/* call is not supported */
		ret = 0;
	}
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

	start = os_tick_ms();

	if (devup(devinfo->device, devinfo->name) != 0) {
		/* LCOV_EXCL_START */
		return (void*)-1;
		/* LCOV_EXCL_STOP */
	}

	msg_status("Spunup device '%s' for disk '%s' in %" PRIu64 " ms.\n", devinfo->file, devinfo->name, os_tick_ms() - start);

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

	start = os_tick_ms();

	if (devdown(devinfo->device, devinfo->name, devinfo->smartctl) != 0) {
		/* LCOV_EXCL_START */
		return (void*)-1;
		/* LCOV_EXCL_STOP */
	}

	msg_status("Spundown device '%s' for disk '%s' in %" PRIu64 " ms.\n", devinfo->file, devinfo->name, os_tick_ms() - start);

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

	start = os_tick_ms();

	if (devdownifup(devinfo->device, devinfo->name, devinfo->smartctl, &power) != 0) {
		/* LCOV_EXCL_START */
		return (void*)-1;
		/* LCOV_EXCL_STOP */
	}

	if (power == POWER_ACTIVE)
		msg_status("Spundown device '%s' for disk '%s' in %" PRIu64 " ms.\n", devinfo->file, devinfo->name, os_tick_ms() - start);

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

int devquery(tommy_list* high, tommy_list* low, int operation)
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

	/* for each high device */
	for (i = tommy_list_head(high); i != 0; i = i->next) {
		devinfo_t* devinfo = i->data;
		uint64_t device = devinfo->device;

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

		tommy_list devlist;
		tommy_list_init(&devlist);

		/* obtain the real devices */
		if (devdereference(device, devinfo->mount, &devlist) != 0) {
			/* LCOV_EXCL_START */
			log_fatal(EEXTERNAL, "Failed to dereference device '%u:%u' at '%s'.\n", major(device), minor(device), devinfo->mount);
			return -1;
			/* LCOV_EXCL_STOP */
		}

		devinfo->file[0] = 0;
		for (tommy_node* j = tommy_list_head(&devlist); j != 0; j = j->next) {
			struct dev_struct* dev = j->data;
			uint64_t access_stat;

			/* retrieve access stat for the high level device */
			if (devstat(dev->device, &access_stat) == 0) {
				/* cumulate access stat in the first split */
				if (devinfo->split)
					devinfo->split->access_stat += access_stat;
				else
					devinfo->access_stat += access_stat;
			}

			/* get the device file */
			char file[PATH_MAX];
			if (devresolve(dev->device, file, sizeof(file)) != 0) {
				/* LCOV_EXCL_START */
				log_fatal(EEXTERNAL, "Failed to resolve device '%u:%u'.\n", major(dev->device), minor(dev->device));
				return -1;
				/* LCOV_EXCL_STOP */
			}

			/* add to the list of device files */
			if (devinfo->file[0] != 0)
				pathcat(devinfo->file, sizeof(devinfo->file), ",");
			pathcat(devinfo->file, sizeof(devinfo->file), file);

			/* expand the tree of devices */
			if (devtree(devinfo, dev->device, low) != 0) {
				/* LCOV_EXCL_START */
				log_fatal(EEXTERNAL, "Failed to expand device '%u:%u'.\n", major(dev->device), minor(dev->device));
				return -1;
				/* LCOV_EXCL_STOP */
			}
		}

		tommy_list_foreach(&devlist, free);
	}
#else
	(void)high;
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

int devmap(void)
{
#if HAVE_LINUX_DEVICE
	char path[PATH_MAX];
	DIR* d = opendir("/sys/block");
	if (!d) {
		/* LCOV_EXCL_START */
		return -1;
		/* LCOV_EXCL_STOP */
	}

	struct dirent* dd;
	while (1) {
		char buf[256];

		dd = readdir(d);
		if (dd == 0)
			break;

		if (dd->d_name[0] == '.')
			continue;

		/* verify it has a hardware device link (excludes virtual disks) */
		pathprint(path, sizeof(path), "/sys/block/%s/device", dd->d_name);
		if (eaccess(path, F_OK) != 0)
			continue;

		/* device/serial */
		pathprint(path, sizeof(path), "/sys/block/%s/device/serial", dd->d_name);
		if (sysattr(path, buf, sizeof(buf)) == 0 && buf[0] != 0) {
			log_tag("map:/dev/%s:%s\n", dd->d_name, esc_tag(buf));
		}

		/* device/vpd_pg80 */
		pathprint(path, sizeof(path), "/sys/block/%s/device/vpd_pg80", dd->d_name);
		if (sysattr_vpd_pg80(path, buf, sizeof(buf)) == 0 && buf[0] != 0) {
			log_tag("map:/dev/%s:%s\n", dd->d_name, esc_tag(buf));
		}

		/* device/vpd_pg83 */
		pathprint(path, sizeof(path), "/sys/block/%s/device/vpd_pg83", dd->d_name);
		if (sysattr_vpd_pg83(path, buf, sizeof(buf)) == 0 && buf[0] != 0) {
			log_tag("map:/dev/%s:naa.%s\n", dd->d_name, esc_tag(buf));
		}

		/* device/WWN */
		pathprint(path, sizeof(path), "/sys/block/%s/device/wwn", dd->d_name);
		if (sysattr(path, buf, sizeof(buf)) == 0 && buf[0] != 0) {
			log_tag("map:/dev/%s:%s\n", dd->d_name, esc_tag(buf));
		}

		/* device/WWID */
		pathprint(path, sizeof(path), "/sys/block/%s/device/wwid", dd->d_name);
		if (sysattr(path, buf, sizeof(buf)) == 0 && buf[0] != 0) {
			log_tag("map:/dev/%s:%s\n", dd->d_name, esc_tag(buf));
		}

		/* WWN */
		pathprint(path, sizeof(path), "/sys/block/%s/wwn", dd->d_name);
		if (sysattr(path, buf, sizeof(buf)) == 0 && buf[0] != 0) {
			log_tag("map:/dev/%s:%s\n", dd->d_name, esc_tag(buf));
		}

		/* WWID */
		pathprint(path, sizeof(path), "/sys/block/%s/wwid", dd->d_name);
		if (sysattr(path, buf, sizeof(buf)) == 0 && buf[0] != 0) {
			log_tag("map:/dev/%s:%s\n", dd->d_name, esc_tag(buf));
		}

		/* uuid */
		pathprint(path, sizeof(path), "/sys/block/%s/uuid", dd->d_name);
		if (sysattr(path, buf, sizeof(buf)) == 0 && buf[0] != 0) {
			log_tag("map:/dev/%s:%s\n", dd->d_name, esc_tag(buf));
		}
	}

	closedir(d);
#endif
	return 0;
}

/****************************************************************************/
/* temperature */

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
	if (!dir) {
		/* LCOV_EXCL_START */
		return 0;
		/* LCOV_EXCL_STOP */
	}

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
		if (!hwmon_dir) {
			/* LCOV_EXCL_START */
			continue;
			/* LCOV_EXCL_STOP */
		}

		while ((hwmon_entry = readdir(hwmon_dir)) != NULL) {
			char hwmon_name[PATH_MAX];
			char value[128];
			char name[128];
			char label[128];
			char* dash;
			char* e;
			int temp;

			if (strncmp(hwmon_entry->d_name, "temp", 4) != 0)
				continue;

			/* make a copy to allow modifications in place */
			pathcpy(hwmon_name, sizeof(hwmon_name), hwmon_entry->d_name);

			dash = strrchr(hwmon_name, '_');
			if (dash == 0) {
				/* LCOV_EXCL_START */
				continue;
				/* LCOV_EXCL_STOP */
			}

			if (strcmp(dash, "_input") != 0)
				continue;

			/* read the temperature */
			pathprint(path, sizeof(path), "/sys/class/hwmon/%s/%s", entry->d_name, hwmon_name);

			if (sysattr(path, value, sizeof(value)) != 0) {
				/* LCOV_EXCL_START */
				continue;
				/* LCOV_EXCL_STOP */
			}

			temp = strtoi(value, &e, 10) / 1000;
			if (*e != 0 && !isspace((unsigned char)*e)) {
				/* LCOV_EXCL_START */
				continue;
				/* LCOV_EXCL_STOP */
			}

			/* cut the file name at "_input" */
			*dash = 0;

			/* read the corresponding name */
			pathprint(path, sizeof(path), "/sys/class/hwmon/%s/name", entry->d_name);
			if (sysattr(path, name, sizeof(name)) != 0) {
				/* LCOV_EXCL_START */
				/* fallback to using the hwmon name */
				pathcpy(name, sizeof(name), entry->d_name);
				/* LCOV_EXCL_STOP */
			}

			/* read the corresponding label file */
			pathprint(path, sizeof(path), "/sys/class/hwmon/%s/%s_label", entry->d_name, hwmon_name);
			if (sysattr(path, label, sizeof(label)) != 0) {
				/* LCOV_EXCL_START */
				/* fallback to using the temp* name (e.g., temp1, temp2) */
				pathcpy(label, sizeof(label), hwmon_name);
				/* LCOV_EXCL_STOP */
			}

			log_tag("thermal:ambient:device:%s:%s:%s:%s:%d\n", entry->d_name, name, hwmon_name, label, temp);

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

/****************************************************************************/
/* app */

void app_init(void)
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

	/* set LC_ALL=C to make smartctl ignoring the locale when printing info */
	setenv("LC_ALL", "C", 1);
}

void app_done(void)
{
#if HAVE_BLKID
	if (cache != 0)
		blkid_put_cache(cache);
#endif
}

void app_default_conf(char* conf, size_t conf_size, const char* argv0)
{
	(void)argv0;

#ifdef SYSCONFDIR
	/* if it exists, give precedence at sysconfdir, usually /usr/local/etc */
	if (eaccess(SYSCONFDIR "/" PACKAGE ".conf", F_OK) == 0) {
		pathcpy(conf, conf_size, SYSCONFDIR "/" PACKAGE ".conf");
	} else /* otherwise fallback to plain /etc */
#endif
	{
		pathcpy(conf, conf_size, "/etc/" PACKAGE ".conf");
	}
}

#endif

