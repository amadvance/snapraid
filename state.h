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
	int force_zero; /**< Forced dangerous operations of synching file now with zero size. */
	int force_empty; /**< Forced dangerous operations of synching disk now empty. */
	int expect_unrecoverable; /**< Expect presence of unrecoverable error in checking or fixing. */
	int expect_recoverable; /**< Expect presence of recoverable error in checking. */
	int need_write; /**< If the state is changed. */
	uint32_t block_size; /**< Block size in bytes. */
	char parity[PATH_MAX]; /**< Path of the parity file. */
	char qarity[PATH_MAX]; /**< Path of the qarity file. */
	unsigned level; /**< Number of parity levels. 1 for RAID5, 2 for RAID6. */
	unsigned hash; /**< Hash kind used. */
	tommy_list contentlist; /**< List of content files. */
	tommy_array diskarr; /**< Disk array. */
	tommy_list filterlist; /**< List of inclusion/exclusion. */
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
 * Read the configuration file.
 */
void state_config(struct snapraid_state* state, const char* path, int verbose, int force_zero, int force_empty, int expect_unrecoverable, int expect_recoverable);

/**
 * Read the state.
 */
void state_read(struct snapraid_state* state);

/**
 * Writes the new state.
 */
void state_write(struct snapraid_state* state);

/**
 * Scans all the disks to update the state.
 */
void state_scan(struct snapraid_state* state);

/**
 * Syncs the parity data.
 */
int state_sync(struct snapraid_state* state, block_off_t blockstart, block_off_t blockcount);

/**
 * Checks (and fixes) all the files and the parity data.
 */
void state_check(struct snapraid_state* state, int fix, block_off_t blockstart, block_off_t blockcount);

/**
 * Read the files.
 */
void state_dry(struct snapraid_state* state, block_off_t blockstart, block_off_t blockcount);

/**
 * Filter files.
 */
void state_filter(struct snapraid_state* state, tommy_list* filterlist);

/**
 * Writes the progress.
 */
int state_progress(time_t* start, time_t* last, block_off_t countpos, block_off_t countmax, data_off_t countsize);

#endif

