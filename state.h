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

/****************************************************************************/
/* state */

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
	int verbose; /**< Verbose output. */
	int gui; /**< Gui output. */
	int force_zero; /**< Forced dangerous operations of synching files now with zero size. */
	int force_empty; /**< Forced dangerous operations of synching disks now empty. */
	int force_uuid; /**< Forced dangerous operations of synching disks with uuid changed. */
	int force_device; /**< Forced dangerous operations of using disks with save device id. */
	int expect_unrecoverable; /**< Expect presence of unrecoverable error in checking or fixing. */
	int expect_recoverable; /**< Expect presence of recoverable error in checking. */
	int skip_device; /**< Skip devices matching checks. */
	int skip_sign; /**< Skip the sign check for content files. */
	int skip_fallocate; /**< Skip the use of fallocate(). */
	int skip_sequential; /**< Skip sequential hint. */
	int skip_lock; /**< Skip the lock file protection. */
	int skip_self; /**< Skip the self-test. */
	int kill_after_sync; /**< Kill the process after sync without saving the final state. */
	int force_murmur3; /**< Force Murmur3 choice. */
	int force_spooky2; /**< Force Spooky2 choice. */
	int force_order; /**< Force sorting order. One of the SORT_* defines. */
	unsigned force_scrub; /**< Force scrub for the specified number of blocks. */
	int force_scrub_even; /**< Force scrub of all the even blocks. */
	int force_content_write; /**< Force the update of the content file. */
	int force_content_text; /**< Force the use of text version of content file. */
};

struct snapraid_state {
	struct snapraid_option opt; /**< Setup options. */
	int filter_hidden; /**< Filter out hidden files. */
	uint64_t autosave; /**< Autosave after the specified amount of data. 0 to disable. */
	int need_write; /**< If the state is changed. */
	uint32_t block_size; /**< Block size in bytes. */
	char parity[PATH_MAX]; /**< Path of the parity file. */
	uint64_t parity_device; /**< Device identifier of the parity. */
	char qarity[PATH_MAX]; /**< Path of the qarity file. */
	uint64_t qarity_device; /**< Device identifier of the qarity. */
	char pool[PATH_MAX]; /**< Path of the pool tree. */
	uint64_t pool_device; /**< Device identifier of the pool. */
	unsigned char hashseed[HASH_SIZE]; /**< Hash seed. Just after a uint64 to provide a minimal alignment. */
	unsigned char prevhashseed[HASH_SIZE]; /**< Previous hash seed. In case of rehash. */
	char lockfile[PATH_MAX]; /**< Path of the lock file to use. */
	unsigned level; /**< Number of parity levels. 1 for RAID5, 2 for RAID6. */
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
	tommy_arrayof infoarr; /**< Block information array. */
	
	/**
	 * Required size of the parity file, computed from the loaded state.
	 * This size only counts BLK blocks, ignoring NEW, CHG and DELETED ones.
	 *
	 * In normal case it's also the blockmax size returned by parity_size().
	 * In case of interrupted sync, this is the position + 1 of the last BLK block.
	 * Potentionally smaller than parity_size().
	 */
	block_off_t loaded_paritymax;

	time_t progress_start; /**< Start of processing for progress visualization. */
	time_t progress_last; /**< Last update of progress visualization. */
	time_t progress_interruption; /**< Start of the measure interruption. */
	time_t progress_subtract; /**< Time to subtract for the interruptions. */
	int no_conf; /**< Automatically add missing info. Used to load content without a configuration file. */
};

/**
 * Initializes the state.
 */
void state_init(struct snapraid_state* state);

/**
 * Deinitializes the state.
 */
void state_done(struct snapraid_state* state);

/**
 * Reads the configuration file.
 */
void state_config(struct snapraid_state* state, const char* path, const char* command, struct snapraid_option* opt);

/**
 * Reads the state.
 */
void state_read(struct snapraid_state* state);

/**
 * Writes the new state.
 */
void state_write(struct snapraid_state* state);

/**
 * Scans all the disks to update the state.
 */
void state_scan(struct snapraid_state* state, int output);

/**
 * Syncs the parity data.
 */
int state_sync(struct snapraid_state* state, block_off_t blockstart, block_off_t blockcount);

/**
 * Checks (and fixes) all the files and parity data.
 * \param check If we have to check also the parity.
 * \param fix If we have to fix, after checking. It requires also check==1.
 */
int state_check(struct snapraid_state* state, int check, int fix, block_off_t blockstart, block_off_t blockcount);

/**
 * Dry the files.
 */
void state_dry(struct snapraid_state* state, block_off_t blockstart, block_off_t blockcount);

/**
 * Rehash the files.
 */
void state_rehash(struct snapraid_state* state);

/**
 * Scrub the files.
 */
int state_scrub(struct snapraid_state* state, int percentage, int olderthan);

/**
 * Print the status.
 */
int state_status(struct snapraid_state* state);

/**
 * Finds duplicates.
 */
void state_dup(struct snapraid_state* state);

/**
 * Creates pool tree.
 */
void state_pool(struct snapraid_state* state);

/**
 * Filter files, symlinks and dirs.
 * Apply an additional filter to the list currently loaded.
 */
void state_filter(struct snapraid_state* state, tommy_list* filterlist_file, tommy_list* filterlist_disk, int filter_missing, int filter_error);

/**
 * Begins the progress visualization.
 */
int state_progress_begin(struct snapraid_state* state, block_off_t blockstart, block_off_t blockmax, block_off_t countmax);

/**
 * Ends the progress visualization.
 */
void state_progress_end(struct snapraid_state* state, block_off_t countpos, block_off_t countmax, data_off_t countsize);

/**
 * Writes the progress.
 */
int state_progress(struct snapraid_state* state, block_off_t blockpos, block_off_t countpos, block_off_t countmax, data_off_t countsize);

/**
 * Stop temporarely the progress.
 */
void state_progress_stop(struct snapraid_state* state);

/**
 * Restart the progress.
 */
void state_progress_restart(struct snapraid_state* state);

/****************************************************************************/
/* misc */

/**
 * Generate a dummy configuration file from a content file.
 */
void generate_configuration(const char* content);

#endif

