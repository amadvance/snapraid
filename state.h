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

struct snapraid_state {
	int verbose; /**< Verbose output. */
	int force_zero; /**< Forced dangerous operations of synching file now with zero size. */
	int force_empty; /**< Forced dangerous operations of synching disk now empty. */
	int need_write; /**< If the state is changed. */
	uint32_t block_size; /**< Block size in bytes. */
	char content[PATH_MAX]; /**< Path of the content file. */
	char parity[PATH_MAX]; /**< Path of the parity file. */
	tommy_array diskarr; /**< Disk array. */
	tommy_list excludelist; /**< List of exclusion. */
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
void state_config(struct snapraid_state* state, const char* path, int verbose, int force_zero, int force_empty);

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
 * Writes the progress.
 */
int state_progress(time_t* start, time_t* last, block_off_t blockpos, block_off_t blockmax, data_off_t count_block, data_off_t count_size);

#endif

