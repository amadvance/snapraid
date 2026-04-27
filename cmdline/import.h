// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2013 Andrea Mazzoleni

#ifndef __IMPORT_H
#define __IMPORT_H

#include "elem.h"
#include "state.h"

/****************************************************************************/
/* import */

/**
 * Import block.
 * Block used to import data external when recovering by hash.
 */
struct snapraid_import_block {
	struct snapraid_import_file* file; /**< Back pointer to the file owning this block. */
	unsigned size; /**< Size of the block. */
	data_off_t offset; /**< Position of the block in the file. */
	unsigned char hash[HASH_MAX]; /**< Hash of the block. */
	unsigned char prevhash[HASH_MAX]; /**< Previous hash of the block. Valid only if we are in rehash state. */

	/* nodes for data structures */
	tommy_hashdyn_node nodeset;
	tommy_hashdyn_node prevnodeset;
};

/**
 * Import file.
 * File used to import data external when recovering by hash.
 */
struct snapraid_import_file {
	data_off_t size; /**< Size of the file. */
	struct snapraid_import_block* blockimp; /**< All the blocks of the file. */
	block_off_t blockmax; /**< Number of blocks. */
	char* path; /**< Full path of the file. */
	int is_runtime; /**< If the hash was obtained at runtime */

	/* nodes for data structures */
	tommy_node nodelist;
};

/**
 * Deallocate an import file.
 */
void import_file_free(struct snapraid_import_file* file);

/**
 * Fetch a block from the specified hash.
 * Return ==0 if the block is found, and copied into buffer.
 */
int state_import_fetch(struct snapraid_state* state, int prevhash, struct snapraid_block* missing_block, unsigned char* buffer);

/**
 * Import files from the specified directory.
 */
void state_import(struct snapraid_state* state, const char* dir);

/**
 * Import files from the dealloc list.
 */
void state_dealloc(struct snapraid_state* state, const char* dir, tommy_list* dealloclist);

#endif

