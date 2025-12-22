#include <stdio.h>
#include <stdlib.h> /* On many systems (e.g., Darwin), `stdio.h' is a prerequisite. */
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <limits.h>
#include <stdint.h>

/**
 * Include list support to have tommy_node.
 */
#include "../tommyds/tommylist.h"


/**
 * Max UUID length.
 */
#define UUID_MAX 128

/**
 * Standard SMART attributes.
 */
#define SMART_REALLOCATED_SECTOR_COUNT 5
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
 * SMART attributes count.
 */
#define SMART_COUNT 256

/**
 * Flags returned by smartctl.
 */
#define SMARTCTL_FLAG_UNSUPPORTED (1 << 0) /**< Device not recognized, requiring the -d option. */
#define SMARTCTL_FLAG_OPEN (1 << 1) /**< Device open or identification failed. */
#define SMARTCTL_FLAG_COMMAND (1 << 2) /**< Some SMART or ATA commands failed. This is a common error, also happening with full info gathering. */
#define SMARTCTL_FLAG_FAIL (1 << 3) /**< SMART status check returned "DISK FAILING". */
#define SMARTCTL_FLAG_PREFAIL (1 << 4) /**< We found prefail Attributes <= threshold. */
#define SMARTCTL_FLAG_PREFAIL_LOGGED (1 << 5) /**< SMART status check returned "DISK OK" but we found that some (usage or prefail) Attributes have been <= threshold at some time in the past. */
#define SMARTCTL_FLAG_ERROR (1 << 6) /**< The device error log contains records of errors. */
#define SMARTCTL_FLAG_ERROR_LOGGED (1 << 7) /**< The device self-test log contains records of errors. */

/**
 * SMART max attribute length.
 */
#define SMART_MAX 64

/**
 * Value for unassigned SMART attribute.
 */
#define SMART_UNASSIGNED 0xFFFFFFFFFFFFFFFFULL

/**
 * Power mode
 */
#define POWER_STANDBY 0
#define POWER_ACTIVE 1

/**
 * Health
 */
#define HEALTH_PASSED 0
#define HEALTH_FAILING 1
    
/**
 * Device info entry.
 */
struct snapraid_device {
	char file[PATH_MAX]; /**< File device. */
	char serial[SMART_MAX]; /**< Serial number. */
	char family[SMART_MAX]; /**< Vendor and model family. */
	char model[SMART_MAX]; /**< Model. */
	uint64_t smart[SMART_COUNT]; /**< SMART attributes. */
	uint64_t size;
	uint64_t rotational;
	uint64_t error;
	uint64_t flags;
	uint64_t power; /**< POWER mode. */
	uint64_t health; /**< HEALTH code. */

	tommy_node node;
};

struct snapraid_data {
	char name[PATH_MAX]; /**< Name of the disk. */
	char dir[PATH_MAX]; /**< Mount point */
	char uuid[UUID_MAX]; /**< Current UUID. */
	char content_uuid[UUID_MAX]; /**< UUID stored in the content file. */
	uint64_t content_size; /**< Size of the disk stored in the content file. */
	uint64_t content_free; /**< Free size of the disk stored in the content file. */
	tommy_list device_list; /**< Lis of snapraid_devices */
	tommy_node node;
};

struct snapraid_split {
	int index; /**< Index of the split */
	char path[PATH_MAX]; /**< Parity file */
	char uuid[UUID_MAX]; /**< Current UUID. */
	char content_path[PATH_MAX]; /**< Parity file stored in the content file. */
	uint64_t content_size; /**< Size of the parity file stored in the content file. */
	char content_uuid[UUID_MAX]; /**< UUID stored in the content file. */
	tommy_list device_list; /**< Lis of snapraid_devices */
	tommy_node node;
};

struct snapraid_parity {
	char name[PATH_MAX]; /**< Name of the parity. */
	tommy_list split_list; /**< Lis of snapraid_splits */
	uint64_t content_size; /**< Size of the disk stored in the content file. */
	uint64_t content_free; /**< Free size of the disk stored in the content file. */
	tommy_node node;
};

struct snapraid_state {
	char conf[PATH_MAX]; /**< Configuration file. */
	tommy_list data_list;
	tommy_list parity_list;
};

int runner(const char* cmd, struct snapraid_state* state);

