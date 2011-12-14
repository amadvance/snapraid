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

#define HASH_MURMUR3 0
#define HASH_MD5 1

struct snapraid_state {
	int verbose; /**< Verbose output. */
	int gui; /**< Gui output. */
	int force_zero; /**< Forced dangerous operations of synching file now with zero size. */
	int force_empty; /**< Forced dangerous operations of synching disk now empty. */
	int filter_hidden; /**< Filter out hidden files. */
	int find_by_name; /**< Forced dangerous operations of synching a rewritten disk. */
	int expect_unrecoverable; /**< Expect presence of unrecoverable error in checking or fixing. */
	int expect_recoverable; /**< Expect presence of recoverable error in checking. */
	int need_write; /**< If the state is changed. */
	uint32_t block_size; /**< Block size in bytes. */
	char parity[PATH_MAX]; /**< Path of the parity file. */
	uint64_t parity_device; /**< Device identifier of the parity. */
	char qarity[PATH_MAX]; /**< Path of the qarity file. */
	uint64_t qarity_device; /**< Device identifier of the qarity. */
	unsigned level; /**< Number of parity levels. 1 for RAID5, 2 for RAID6. */
	unsigned hash; /**< Hash kind used. */
	tommy_list contentlist; /**< List of content files. */
	tommy_list disklist; /**< List of all the disks. */
	tommy_list maplist; /**< List of all the disk mappings. */
	tommy_list filterlist; /**< List of inclusion/exclusion. */
	time_t progress_start; /**< Start of processing for progress visualization. */
	time_t progress_last; /**< Last update of progress visualization. */
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
void state_config(struct snapraid_state* state, const char* path, int verbose, int gui, int force_zero, int force_empty, int filter_hidden, int find_by_name, int expect_unrecoverable, int expect_recoverable, int skip_device);

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
 * Checks (and fixes) all the files and the parity data.
 */
void state_check(struct snapraid_state* state, int fix, block_off_t blockstart, block_off_t blockcount);

/**
 * Dry the files.
 */
void state_dry(struct snapraid_state* state, block_off_t blockstart, block_off_t blockcount);

/**
 * Find duplicates.
 */
void state_dup(struct snapraid_state* state);

/**
 * Filter files.
 */
void state_filter(struct snapraid_state* state, tommy_list* filterlist);

/**
 * Begins the progress visualization.
 */
void state_progress_begin(struct snapraid_state* state, block_off_t blockstart, block_off_t blockmax, block_off_t countmax);

/**
 * Ends the progress visualization.
 */
void state_progress_end(struct snapraid_state* state, block_off_t countpos, block_off_t countmax, data_off_t countsize);

/**
 * Writes the progress.
 */
int state_progress(struct snapraid_state* state, block_off_t blockpos, block_off_t countpos, block_off_t countmax, data_off_t countsize);

#endif

