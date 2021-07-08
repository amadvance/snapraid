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

#ifndef __STATE_H
#define __STATE_H

#include "elem.h"

struct snapraid_handle;
struct snapraid_io;

/****************************************************************************/
/* parity level */

/**
 * Max level of parity supported.
 */
#define LEV_MAX 6

/**
 * Return the parity name: Parity, 2-Parity, 3-Parity, 4-Parity, 5-Parity, 6-Parity.
 */
const char* lev_name(unsigned level);

/**
 * Return the parity name used in the config file: parity, 2-parity, 3-parity, 4-parity, 5-parity, 6-parity.
 */
const char* lev_config_name(unsigned level);

/****************************************************************************/
/* state */

/**
 * Units for disk space.
 */
#define KILO (1000)
#define MEGA (1000 * 1000)
#define GIGA (1000 * 1000 * 1000)
#define TERA (1000 * 1000 * 1000 * 1000LL)
#define KIBI (1024)
#define MEBI (1024 * 1024)
#define GIBI (1024 * 1024 * 1024)
#define TEBI (1024 * 1024 * 1024 * 1024LL)

/**
 * Global variable to identify if Ctrl+C is pressed.
 */
extern volatile int global_interrupt;

#define SORT_PHYSICAL 1 /**< Sort by physical order. */
#define SORT_INODE 2 /**< Sort by inode. */
#define SORT_ALPHA 3 /**< Sort by alphabetic order. */
#define SORT_DIR 4 /**< Sort by directory order. */

/**
 * Options set only at startup.
 * For all these options a value of 0 means nothing set, and to use the default.
 */
struct snapraid_option {
	int gui; /**< Gui output. */
	int auditonly; /**< In check, checks only the hash and not the parity. */
	int badfileonly; /**< In fix, fixes only files marked as bad. */
	int badblockonly; /**< In fix, fixes only the blocks marked as bad. */
	int syncedonly; /**< In fix, fixes only files that are synced. */
	int prehash; /**< Enables the prehash mode for sync. */
	unsigned io_error_limit; /**< Max number of input/output errors before aborting. */
	int force_zero; /**< Forced dangerous operations of syncing files now with zero size. */
	int force_empty; /**< Forced dangerous operations of syncing disks now empty. */
	int force_uuid; /**< Forced dangerous operations of syncing disks with uuid changed. */
	int force_device; /**< Forced dangerous operations of using disks with save device id. */
	int force_nocopy; /**< Force dangerous operations of syncing files without using copy detection. */
	int force_full; /**< Force a full parity update. */
	int force_realloc; /**< Force a full reallocation and parity update. */
	int expect_unrecoverable; /**< Expect presence of unrecoverable error in checking or fixing. */
	int expect_recoverable; /**< Expect presence of recoverable error in checking. */
	int skip_device; /**< Skip devices matching checks. */
	int skip_sign; /**< Skip the sign check for content files. */
	int skip_fallocate; /**< Skip the use of fallocate(). */
	int skip_space_holder; /**< Skip the use of spaceholder file. */
	int file_mode; /**< File mode. Mask of ADVISE_* flags. */
	int skip_lock; /**< Skip the lock file protection. */
	int skip_self; /**< Skip the self-test. */
	int skip_content_check; /**< Relax some content file checks. */
	int skip_parity_access; /**< Skip the parity access for commands that don't need it. */
	int skip_disk_access; /**< Skip the data disk access for commands that don't need it. */
	int skip_content_access; /**< Skip the content access for commands that don't need it. */
	int kill_after_sync; /**< Kill the process after sync without saving the final state. */
	int force_murmur3; /**< Force Murmur3 choice. */
	int force_spooky2; /**< Force Spooky2 choice. */
	int force_order; /**< Force sorting order. One of the SORT_* defines. */
	unsigned force_scrub_at; /**< Force scrub for the specified number of blocks. */
	int force_scrub_even; /**< Force scrub of all the even blocks. */
	int force_content_write; /**< Force the update of the content file. */
	int skip_content_write; /**< Skip the update of the content file. */
	int force_scan_winfind; /**< Force the use of FindFirst/Next in Windows to list directories. */
	int force_progress; /**< Force the use of the progress status. */
	unsigned force_autosave_at; /**< Force autosave at the specified block. */
	int fake_device; /**< Fake device data. */
	int no_warnings; /**< Remove some warning messages. */
	int expected_missing; /**< If missing files are expected and should not be reported. */
	int fake_uuid; /**< Set fakes UUID for testing. */
	int match_first_uuid; /**< Force the matching of the first UUID. */
	int force_parity_update; /**< Force parity update even if data is not changed. */
	unsigned io_cache; /**< Number of IO buffers to use. 0 for default. */
	int auto_conf; /**< Allow to run without configuration file. */
	int force_stats; /**< Force stats print during process. */
	uint64_t parity_limit_size; /**< Test limit for parity files. */
};

struct snapraid_state {
	struct snapraid_option opt; /**< Setup options. */
	int filter_hidden; /**< Filter out hidden files. */
	uint64_t autosave; /**< Autosave after the specified amount of data. 0 to disable. */
	int need_write; /**< If the state is changed. */
	int checked_read; /**< If the state was read and checked. */
	uint32_t block_size; /**< Block size in bytes. */
	unsigned raid_mode; /**< Raid mode to use. RAID_MODE_DEFAULT or RAID_MODE_ALTERNATE. */
	int file_mode; /**< File access mode. Combination of ADVISE_* flags. */
	struct snapraid_parity parity[LEV_MAX]; /**< Parity vector. */
	char share[PATH_MAX]; /**< Path of the share tree. If !=0 pool links are created in a different way. */
	char pool[PATH_MAX]; /**< Path of the pool tree. */
	uint64_t pool_device; /**< Device identifier of the pool. */
	unsigned char hashseed[HASH_MAX]; /**< Hash seed. Just after a uint64 to provide a minimal alignment. */
	unsigned char prevhashseed[HASH_MAX]; /**< Previous hash seed. In case of rehash. */
	char lockfile[PATH_MAX]; /**< Path of the lock file to use. */
	unsigned level; /**< Number of parity levels. 1 for PAR1, 2 for PAR2. */
	unsigned hash; /**< Hash kind used. */
	unsigned prevhash; /**< Previous hash kind used.  In case of rehash. */
	unsigned besthash; /**< Best hash suggested. */
	const char* command; /**< Command running. */
	tommy_list contentlist; /**< List of content files. */
	tommy_list disklist; /**< List of all the disks. */
	tommy_list maplist; /**< List of all the disk mappings. */
	tommy_list filterlist; /**< List of inclusion/exclusion. */
	tommy_list importlist; /**< List of import file. */
	tommy_hashdyn importset; /**< Hashtable by hash of all the import blocks. */
	tommy_hashdyn previmportset; /**< Hashtable by prevhash of all the import blocks. Valid only if we are in a rehash state. */
	tommy_hashdyn searchset; /**< Hashtable by timestamp of all the search files. */
	tommy_arrayblkof infoarr; /**< Block information array. */

	/**
	 * Cumulative time used for computations.
	 */
	uint64_t tick_misc;
	uint64_t tick_sched;
	uint64_t tick_raid;
	uint64_t tick_hash;

	/**
	 * Cumulative time used for all io operations of disks.
	 */
	uint64_t tick_io;

	/**
	 * Last time used for time measure.
	 */
	uint64_t tick_last;

	int clear_past_hash; /**< Clear all the hash from CHG and DELETED blocks when reading the state from an incomplete sync. */

	time_t progress_whole_start; /**< Initial start of the whole process. */
	time_t progress_interruption; /**< Time of the start of the progress interruption. */
	time_t progress_wasted; /**< Time wasted in interruptions. */

	time_t progress_time[PROGRESS_MAX]; /**< Last times of progress. */
	block_off_t progress_pos[PROGRESS_MAX]; /**< Last positions of progress. */
	data_off_t progress_size[PROGRESS_MAX]; /**< Last sizes of progress. */
	uint64_t progress_tick_misc[PROGRESS_MAX]; /**< Last cpu ticks of progress. */
	uint64_t progress_tick_sched[PROGRESS_MAX]; /**< Last scheduling ticks of progress. */
	uint64_t progress_tick_raid[PROGRESS_MAX]; /**< Last raid ticks of progress. */
	uint64_t progress_tick_hash[PROGRESS_MAX]; /**< Last hash ticks of progress. */
	uint64_t progress_tick_io[PROGRESS_MAX]; /**< Last io ticks of progress. */

	int progress_ptr; /**< Pointer to the next position to fill. Rolling over. */
	int progress_tick; /**< Number of measures done. */

	int no_conf; /**< Automatically add missing info. Used to load content without a configuration file. */
};

/**
 * Initialize the state.
 */
void state_init(struct snapraid_state* state);

/**
 * Deinitialize the state.
 */
void state_done(struct snapraid_state* state);

/**
 * Read the configuration file.
 */
void state_config(struct snapraid_state* state, const char* path, const char* command, struct snapraid_option* opt, tommy_list* filterlist_disk);

/**
 * Read the state.
 */
void state_read(struct snapraid_state* state);

/**
 * Write the new state.
 */
void state_write(struct snapraid_state* state);

/**
 * Diff all the disks.
 */
int state_diff(struct snapraid_state* state);

/**
 * Scan all the disks to update the state.
 */
void state_scan(struct snapraid_state* state);

/**
 * Set the nanosecond timestamp of all files that have a zero value.
 */
void state_touch(struct snapraid_state* state);

/**
 * Devices operations.
 */
void state_device(struct snapraid_state* state, int operation, tommy_list* filterlist_disk);

/**
 * Sync the parity data.
 */
int state_sync(struct snapraid_state* state, block_off_t blockstart, block_off_t blockcount);

/**
 * Check (and fixes) all the files and parity data.
 * \param fix If we have to fix, after checking.
 */
int state_check(struct snapraid_state* state, int fix, block_off_t blockstart, block_off_t blockcount);

/**
 * Dry the files.
 */
void state_dry(struct snapraid_state* state, block_off_t blockstart, block_off_t blockcount);

/**
 * Rehash the files.
 */
void state_rehash(struct snapraid_state* state);

/**
 * Scrub levels.
 */
#define SCRUB_AUTO -1 /**< Automatic selection. */
#define SCRUB_BAD -2 /**< Scrub only the bad blocks. */
#define SCRUB_NEW -3 /**< Scrub the new blocks. */
#define SCRUB_FULL -4 /**< Scrub everything. */
#define SCRUB_EVEN -5 /**< Even blocks. */

/**
 * Scrub the files.
 */
int state_scrub(struct snapraid_state* state, int plan, int olderthan);

/**
 * Print the status.
 */
int state_status(struct snapraid_state* state);

/**
 * Find duplicates.
 */
void state_dup(struct snapraid_state* state);

/**
 * List content.
 */
void state_list(struct snapraid_state* state);

/**
 * Create pool tree.
 */
void state_pool(struct snapraid_state* state);

/**
 * Refresh the free space info.
 *
 * Note that it requires disks access.
 */
void state_refresh(struct snapraid_state* state);

/**
 * Skip files, symlinks and dirs.
 * Apply any skip access disk.
 */
void state_skip(struct snapraid_state* state);

/**
 * Filter files, symlinks and dirs.
 * Apply an additional filter to the list currently loaded.
 */
void state_filter(struct snapraid_state* state, tommy_list* filterlist_file, tommy_list* filterlist_disk, int filter_missing, int filter_error);

/**
 * Begin the progress visualization.
 */
int state_progress_begin(struct snapraid_state* state, block_off_t blockstart, block_off_t blockmax, block_off_t countmax);

/**
 * End the progress visualization.
 */
void state_progress_end(struct snapraid_state* state, block_off_t countpos, block_off_t countmax, data_off_t countsize);

/**
 * Write the progress.
 */
int state_progress(struct snapraid_state* state, struct snapraid_io* io, block_off_t blockpos, block_off_t countpos, block_off_t countmax, data_off_t countsize);

/**
 * Stop temporarily the progress.
 */
void state_progress_stop(struct snapraid_state* state);

/**
 * Restart the progress.
 */
void state_progress_restart(struct snapraid_state* state);

/**
 * Set the usage time as wasted one not counted.
 */
void state_usage_waste(struct snapraid_state* state);

/**
 * Set the usage time for CPU.
 */
void state_usage_misc(struct snapraid_state* state);
void state_usage_sched(struct snapraid_state* state);
void state_usage_raid(struct snapraid_state* state);
void state_usage_hash(struct snapraid_state* state);

/**
 * Set the last file used
 */
void state_usage_file(struct snapraid_state* state, struct snapraid_disk* disk, struct snapraid_file* file);

/**
 * Set the usage time for a set of data disks.
 */
void state_usage_disk(struct snapraid_state* state, struct snapraid_handle* handle_map, unsigned* waiting_map, unsigned waiting_mac);

/**
 * Set the usage time for a set of parity disk.
 */
void state_usage_parity(struct snapraid_state* state, unsigned* waiting_map, unsigned waiting_mac);

/**
 * Print the stats of the usage time.
 */
void state_usage_print(struct snapraid_state* state);

/**
 * Check the file-system on all disks.
 * On error it aborts.
 */
void state_fscheck(struct snapraid_state* state, const char* ope);

/****************************************************************************/
/* misc */

/**
 * Generate a dummy configuration file from a content file.
 */
void generate_configuration(const char* content);

#endif

