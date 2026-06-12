// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Andrea Mazzoleni

#ifndef __APP_H
#define __APP_H

/**
 * Include list support to have tommy_node.
 */
#include "tommyds/tommylist.h"

/**
 * Basic block position type.
 */
typedef uint64_t block_off_t;

/**
 * Basic data position type.
 * It's signed as file size and offset are usually signed.
 */
typedef int64_t data_off_t;

/**
 * Another name for link() to avoid confusion with local variables called "link".
 */
static inline int hardlink(const char* a, const char* b)
{
	return link(a, b);
}

/**
 * Get the device UUID.
 * Return 0 on success.
 */
int devuuid(uint64_t device_id, const char* device_path, char* uuid, size_t size);

/**
 * Physical offset not yet read.
 */
#define FILEPHY_UNREAD_OFFSET 0

/**
 * Special value returned when the file-system doesn't report any offset for unknown reason.
 */
#define FILEPHY_UNREPORTED_OFFSET 1

/**
 * Special value returned when the file doesn't have a real offset.
 * For example, because it's stored in the NTFS MFT.
 */
#define FILEPHY_WITHOUT_OFFSET 2

/**
 * Value indicating real offsets. All offsets greater or equal at this one are real.
 */
#define FILEPHY_REAL_OFFSET 3

/**
 * Get the physical address of the specified file.
 * This is expected to be just a hint and not necessarily correct or unique.
 * Return 0 on success.
 */
int filephy(const char* path, uint64_t size, uint64_t* physical);

/**
 * Check if the underline file-system support persistent inodes.
 * Return -1 on error, 0 on success.
 */
int fsinfo(const char* path, int* has_persistent_inode, int* has_syncronized_hardlinks, uint64_t* total_space, uint64_t* free_space, char* fstype, size_t fstype_size, char* fslabel, size_t fslabel_size);

/**
 * Where snapshot are accessible
 */
#define SNAPSHOT_CONTAINER ".snapraid"

/**
 * Snapshots names
 */
#define SNAPSHOT_PENDING "pending"
#define SNAPSHOT_STABLE "stable"

/*
 * Snapshots context
 */
struct fssnapshot_struct {
	/**
	 * Filesystem magic number
	 * 0 if no snapshot support
	 */
	uint32_t magic;

	/**
	 * Root directory of the volume/dataset mountpoint.
	 * It ALWAYS terminates with /
	 * - Btrfs/Bcachefs: subvolume root
	 * - ZFS: dataset mountpoint
	 */
	char root_dir[PATH_MAX];

	/**
	 * Directory where snapshots are accessible as subdirectories.
	 * It ALWAYS terminates with /
	 * - Btrfs/Bcachefs: real directory (e.g. <root>/.snapraid/)
	 * - ZFS: virtual directory (e.g. <root>/.zfs/snapshot/)
	 */
	char snapshot_dir[PATH_MAX];

	/*
	 * Dataset name for ZFS (e.g. "pool/data")
	 * or VolumeName in Windows.
	 * Empty string for filesystems where not applicable.
	 */
	char dataset[PATH_MAX];
};

/**
 * Initialize snapshot context from an arbitrary path.
 *
 * @param dir Full path inside the filesystem (must end with '/')
 * @param fss Output snapshot context
 * @return 0 on success, -1 on failure
 */
int fssnapshot_mount(const char* dir, struct fssnapshot_struct* fss);

/**
 * Unmount the snapshots.
 *
 * Required in Windows to remove the temporary symlinks.
 *
 * @param fss Snapshot context
 */
void fssnapshot_unmount(const struct fssnapshot_struct* fss);

/**
 * Stat the snapshot directory.
 *
 * @param fss Snapshot context
 * @param name Snapshot name
 * @param st Set the stat info
 * @return 0 on success, -1 on failure
 */
int fssnapshot_stat(struct fssnapshot_struct* fss, const char* name, struct stat* st);

/**
 * Create a snapshot of the filesystem root/dataset.
 *
 * @param fss Snapshot context
 * @param name Snapshot name
 * @return 0 on success, -1 on failure
 */
int fssnapshot_create(const struct fssnapshot_struct* fss, const char* name);

/**
 * Delete a snapshot.
 *
 * @param fss  Snapshot context
 * @param name Snapshot name
 * @return 0 on success, -1 on failure
 */
int fssnapshot_delete(const struct fssnapshot_struct* fss, const char* name);

/**
 * Rename a snapshot.
 *
 * @param fss Snapshot context
 * @param old_name Existing snapshot name
 * @param new_name New snapshot name
 * @return 0 on success, -1 on failure
 */
int fssnapshot_rename(const struct fssnapshot_struct* fss, const char* old_name, const char* new_name);

/*
 * Log file.
 *
 * This stream if fully buffered.
 *
 * If no log file is selected, it's 0.
 */
extern FILE* stdlog;

/**
 * Exit codes for testing.
 */
extern int exit_success;
extern int exit_failure;
extern int exit_sync_needed;
#undef EXIT_SUCCESS
#undef EXIT_FAILURE
#define EXIT_SUCCESS exit_success
#define EXIT_FAILURE exit_failure
#define EXIT_SYNC_NEEDED exit_sync_needed

/**
 * Standard SMART attributes.
 */
#define SMART_REALLOCATED_SECTOR_COUNT 5
#define SMART_POWER_ON_HOURS 9
#define SMART_POWER_CYCLE_COUNT 12
#define SMART_UNCORRECTABLE_ERROR_CNT 187
#define SMART_COMMAND_TIMEOUT 188
#define SMART_CURRENT_PENDING_SECTOR 197
#define SMART_OFFLINE_UNCORRECTABLE 198
#define SMART_START_STOP_COUNT 4
#define SMART_POWER_ON_HOURS 9
#define SMART_AIRFLOW_TEMPERATURE_CELSIUS 190
#define SMART_LOAD_CYCLE_COUNT 193
#define SMART_TEMPERATURE_CELSIUS 194

/**
 * Flags returned by smartctl via exit code
 */
#define SMART_FLAGS 256

/*
 * Counts command, transport, or controller-level errors reported by the device.
 * These reflect failed I/O operations not directly caused by media defects
 * (for example interface, firmware, or power-related errors).
 * This counter is cumulative and never resets to zero, even if the underlying
 * error condition is resolved.
 */
#define SMART_ERROR_PROTOCOL 257

/*
 * Counts media-level errors where data could not be reliably read or written.
 * These indicate actual storage surface or flash failures and may imply data loss.
 * This counter is cumulative and never resets to zero, even if the underlying
 * error condition is resolved.
 */
#define SMART_ERROR_MEDIUM 258

/**
 * Unified Wear Level Metric (0-100)
 *
 * Represents the remaining life of an SSD.
 * - 0:   Brand new drive (0% wear).
 * - 100: Drive has reached or exceeded its manufacturer-rated design life.
 */
#define SMART_WEAR_LEVEL 259

/**
 * SMART attributes count.
 */
#define SMART_COUNT 260

/**
 * NVME custom SMART attributes
 *
 * The numbers are just arbitrary to put them in unused space
 */
#define SMART_NVME_CRITICAL_WARNING 100
#define SMART_NVME_AVAILABLE_SPARE 101
#define SMART_NVME_DATA_UNITS_READ 102
#define SMART_NVME_DATA_UNITS_WRITTEN 103
#define SMART_NVME_HOST_READ_COMMANDS 104
#define SMART_NVME_HOST_WRITE_COMMANDS 105
#define SMART_NVME_CONTROLLER_BUSY_TIME 106
#define SMART_NVME_UNSAFE_SHUTDOWNS 107
#define SMART_NVME_WARNING_COMP_TEMPERATURE_TIME 108
#define SMART_NVME_CRITICAL_COMP_TEMPERATURE_TIME 109

/**
 * Info attributes.
 */
#define INFO_SIZE 0 /**< Size in bytes. */
#define INFO_ROTATION_RATE 1 /**< Rotation speed. 0 for SSD. */

/**
 * Info attributes count.
 */
#define INFO_COUNT 2

/**
 * Max number of ignored smart attributes
 */
#define SMART_IGNORE_MAX 4

/**
 * Flags returned by smartctl.
 */
#define SMARTCTL_FLAG_UNSUPPORTED (1 << 0) /**< Device not recognized, requiring the -d option. */
#define SMARTCTL_FLAG_OPEN (1 << 1) /**< Device open or identification failed. */
#define SMARTCTL_FLAG_COMMAND (1 << 2) /**< Some SMART or ATA commands failed. This is a common error, also happening with full info gathering. */
#define SMARTCTL_FLAG_FAIL (1 << 3) /**< SMART status check returned "DISK FAILING". */
#define SMARTCTL_FLAG_PREFAIL (1 << 4) /**< We found prefail Attributes <= threshold. */
#define SMARTCTL_FLAG_PREFAIL_LOGGED (1 << 5) /**< SMART status check returned "DISK OK" but we found that some (usage or prefail) Attributes have been <= threshold at some time in the past. */
#define SMARTCTL_FLAG_ERROR_LOGGED (1 << 6) /**< The device error log contains records of errors. */
#define SMARTCTL_FLAG_SELFERROR_LOGGED (1 << 7) /**< The device self-test log contains records of errors. */

/**
 * SMART max attribute length.
 */
#define SMART_MAX 64

/**
 * Value for unassigned SMART attribute.
 */
#define SMART_UNASSIGNED 0xFFFFFFFFFFFFFFFFULL

/**
 * SMART Attribute flags
 */
#define SMART_ATTR_TYPE_PREFAIL 1
#define SMART_ATTR_TYPE_OLDAGE 2
#define SMART_ATTR_UPDATE_ALWAYS 4
#define SMART_ATTR_UPDATE_OFFLINE 8
#define SMART_ATTR_WHEN_FAILED_NOW 16
#define SMART_ATTR_WHEN_FAILED_PAST 32
#define SMART_ATTR_WHEN_FAILED_NEVER 64

struct smart_attr {
	char name[128]; /**< SMART attribute name. */
	uint64_t raw; /**< SMART attributes raw. It can be SMART_UNASSIGNED. */
	uint64_t norm; /**< SMART attributes normalized. It can be SMART_UNASSIGNED even if raw is not. */
	uint64_t worst; /**< SMART attributes worst. It can be SMART_UNASSIGNED even if raw/norm is not. */
	uint64_t thresh; /**< SMART attributes threshold. It can be SMART_UNASSIGNED even if raw/norm/worst is not. */
	int flags; /**< SMART_ATTR_* flags */
};

/**
 * Power mode
 */
#define POWER_STANDBY 0
#define POWER_ACTIVE 1
#define POWER_UNKNOWN -1

/**
 * Device info entry.
 */
struct devinfo_struct {
	uint64_t device; /**< Device ID. */
	char name[PATH_MAX]; /**< Name of the disk combined with the split index if any */
	char mount[PATH_MAX]; /**< Mount point or other contained directory. */
	char smartctl[PATH_MAX]; /**< Options for smartctl. */
	int smartignore[SMART_IGNORE_MAX]; /**< Attribues to ignore */
	char file[PATH_MAX]; /**< File device. */
#ifdef _WIN32
	char wfile[PATH_MAX]; /**< File device in Windows format. Like \\.\PhysicalDriveX, or \\?\Volume{X}. */
#endif
	struct devinfo_struct* parent; /**< Pointer at the parent if any. */
	struct devinfo_struct* split; /**< Pointer at first split if this one is not the first. */
	struct smart_attr smart[SMART_COUNT]; /**< All smart values. */
	uint64_t info[INFO_COUNT]; /**< Informational attributes not related to SMART telemetry. */
	uint64_t access_stat; /**< Access stat info. */
	char serial[SMART_MAX]; /**< Serial number. */
	char family[SMART_MAX]; /**< Family. */
	char model[SMART_MAX]; /**< Model. */
	char interf[SMART_MAX]; /**< Interface of the device: ata, sata, pata, nvme, usb */
	int power; /**< POWER mode. */
#if HAVE_THREAD
	thread_id_t thread;
#endif
	tommy_node node;
};
typedef struct devinfo_struct devinfo_t;

void device_name_set(devinfo_t* dev, const char* name, int index);

#define DEVICE_LIST 0
#define DEVICE_DOWN 1
#define DEVICE_UP 2
#define DEVICE_SMART 3
#define DEVICE_PROBE 4
#define DEVICE_DOWNIFUP 5

/**
 * Query all the "high" level devices with the specified operation,
 * and produces a list of "low" level devices to operate on.
 *
 * The passed "low" device list must be already initialized.
 */
int devquery(tommy_list* high, tommy_list* low, int operation);

/**
 * Fill with fake data the device list.
 */
int devtest(tommy_list* high, tommy_list* low, int operation);

/**
 * Query all the "low" devices and log their unique identifiers
 */
int devmap(void);

/**
 * Get the ambient temperature in degree
 *
 * Return 0 if not available.
 */
int ambient_temperature(void);

/**
 * Size of the spaceholder file for Windows to avoid the message of low disk space
 */
#define WINDOWS_SPACEHOLDER_SIZE (256 * 1024 * 1024)

/**
 * Generic errors
 */
#define EINTERNAL EFAULT /**< Internal assertion failed. */
#define EDATA EIO /**< Silent data corruption. */
#define ESOFT EINVAL /**< Software error, like permission denied. */
#define EUSER EINVAL /**< Invalid value specified by the user. */
#define EEXTERNAL EINVAL /**< Invalid external interface behaviour. */
#define ECONTENT EINVAL /**< Invalid content file. */
#define EENVIRONMENT EINVAL /**< Invalid physical environment, like temperature too high. */

/****************************************************************************/
/* app */

/**
 * Initializes the application.
 */
void app_init(void);

/**
 * Deinitializes the application.
 */
void app_done(void);

/**
 * Default paths.
 */
void app_default_conf(char* dst, size_t dst_size, const char* argv0);

/**
 * Signal handler.
 */
void app_signal_handler(int signum);

/**
 * Global variable to identify if Ctrl+C is pressed.
 */
int app_global_interrupt(void);

#endif

