// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2014 Andrea Mazzoleni

#ifndef __SEARCH_H
#define __SEARCH_H

#include "elem.h"
#include "state.h"

/****************************************************************************/
/* search */

/**
 * Search file.
 * File used to search for moved data.
 */
struct snapraid_search_file {
	char* path; /**< Full path of the file. */
	char* name; /**< Pointer of the name inside the path. */
	data_off_t size;
	int64_t mtime_sec;
	int mtime_nsec;

	/* nodes for data structures */
	tommy_node node;
};

/**
 * Deallocate a search file.
 */
void search_file_free(struct snapraid_search_file* file);

/**
 * Fetch a file from the size, timestamp and name.
 * Return ==0 if the block is found, and copied into buffer.
 */
int state_search_fetch(struct snapraid_state* state, int prevhash, struct snapraid_file* missing_file, block_off_t missing_file_pos, struct snapraid_block* missing_block, unsigned char* buffer);

/**
 * Import files from the specified directory.
 */
void state_search(struct snapraid_state* state, const char* dir);

/**
 * Import files from all the data disks.
 */
void state_search_array(struct snapraid_state* state);

#endif

