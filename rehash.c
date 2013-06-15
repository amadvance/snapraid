/*
 * Copyright (C) 2013 Andrea Mazzoleni
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

#include "portable.h"

#include "util.h"
#include "elem.h"
#include "import.h"
#include "state.h"
#include "parity.h"
#include "handle.h"
#include "raid.h"

/****************************************************************************/
/* rehash */

static int state_rehash_process(struct snapraid_state* state, block_off_t blockstart, block_off_t blockmax)
{
	struct snapraid_handle* handle;
	unsigned diskmax;
	block_off_t i;
	unsigned j;
	void* buffer_alloc;
	unsigned char* buffer_aligned;
	int ret;
	data_off_t countsize;
	block_off_t countpos;
	block_off_t countmax;
	unsigned error;
	unsigned char seed[HASH_SIZE];

	handle = handle_map(state, &diskmax);

	buffer_aligned = malloc_nofail_align(state->block_size, &buffer_alloc);

	error = 0;

	/* set a random seed */
	randomize(seed, HASH_SIZE);

	/* check that we are in a sync complete state */
	for(i=blockstart;i<blockmax;++i) {
		/* for each disk, process the block */
		for(j=0;j<diskmax;++j) {
			struct snapraid_block* block;
			unsigned block_state;

			/* if the disk position is not used */
			if (!handle[j].disk) {
				/* skip it */
				continue;
			}

			/* if the disk block is not used */
			block = disk_block_get(handle[j].disk, i);
			if (block == BLOCK_EMPTY) {
				/* skip it */
				continue;
			}

			/* get the state of the block */
			block_state = block_state_get(block);

			/* if the block is deleted */
			if (block_state != BLOCK_STATE_BLK) {
				fprintf(stderr, "You can rehash only after a complete sync.\n");
				++error;
				goto bail;
			}
		}
	}

	/* rehash all the blocks in files */
	countmax = blockmax - blockstart;
	countsize = 0;
	countpos = 0;
	state_progress_begin(state, blockstart, blockmax, countmax);
	for(i=blockstart;i<blockmax;++i) {
		/* for each disk, process the block */
		for(j=0;j<diskmax;++j) {
			int read_size;
			unsigned char hash[HASH_SIZE];
			unsigned char hash_new[HASH_SIZE];
			struct snapraid_block* block;

			/* if the disk position is not used */
			if (!handle[j].disk) {
				/* skip it */
				continue;
			}

			/* if the disk block is not used */
			block = disk_block_get(handle[j].disk, i);
			if (block == BLOCK_EMPTY) {
				/* skip it */
				continue;
			}

			/* here we can have only BLOCK_STATE_BLK */

			/* if the file is closed or different than the current one */
			if (handle[j].file == 0 || handle[j].file != block_file_get(block)) {
				/* close the old one, if any */
				ret = handle_close(&handle[j]);
				if (ret == -1) {
					fprintf(stderr, "DANGER! Unexpected close error in a data disk, it isn't possible to rehash.\n");
					printf("Stopping at block %u\n", i);
					++error;
					goto bail;
				}

				/* open the file only for reading */
				ret = handle_open(&handle[j], block_file_get(block), stdlog, state->skip_sequential);
				if (ret == -1) {
					fprintf(stderr, "DANGER! Unexpected open error in a data disk, it isn't possible to rehash.\n");
					printf("Stopping at block %u\n", i);
					++error;
					goto bail;
				}
			}

			/* read from the file */
			read_size = handle_read(&handle[j], block, buffer_aligned, state->block_size, stdlog);
			if (read_size == -1) {
				fprintf(stderr, "DANGER! Unexpected read error in a data disk, it isn't possible to rehash.\n");
				printf("Stopping at block %u\n", i);
				++error;
				goto bail;
			}

			countsize += read_size;

			/* compute the new hash */
			memhash(state->besthash, seed, hash_new, buffer_aligned, read_size);

			/* compute the old hash */
			/* note that we intentionally compute the new one before the old */
			/* to ensure to detect any random bit flip in the memory */
			memhash(state->hash, state->hashseed, hash, buffer_aligned, read_size);

			/* compare the hash */
			if (memcmp(hash, block->hash, HASH_SIZE) != 0) {
				fprintf(stderr, "DANGER! Unexpected data error in a data disk, it isn't possible to rehash.\n");
				printf("Stopping at block %u\n", i);
				++error;
			}

			/* store the new hash */
			memcpy(block->hash, hash_new, HASH_SIZE);
		}

		/* count the number of processed block */
		++countpos;

		/* progress */
		if (state_progress(state, i, countpos, countmax, countsize)) {
			break;
		}
	}

	state_progress_end(state, countpos, countmax, countsize);

	/* set the new hash and seed */
	state->hash = state->besthash;
	memcpy(state->hashseed, seed, HASH_SIZE);

	/* mark the state as needing write */
	state->need_write = 1;

bail:
	/* close all the files left open */
	for(j=0;j<diskmax;++j) {
		ret = handle_close(&handle[j]);
		if (ret == -1) {
			fprintf(stderr, "DANGER! Unexpected close error in a data disk.\n");
			++error;
			/* continue, as we are already exiting */
		}
	}

	if (error) {
		printf("Rehash aborted\n");
	} else {
		printf("No error\n");
	}

	free(handle);
	free(buffer_alloc);

	if (error != 0)
		return -1;
	return 0;
}

void state_rehash(struct snapraid_state* state)
{
	block_off_t blockmax;
	int ret;
	unsigned error;

	if (state->hash == state->besthash) {
		fprintf(stderr, "You don't need to rehash because you are already using the best hash.\n");
		exit(EXIT_FAILURE);
	}

	printf("Initializing...\n");

	blockmax = parity_size(state);

	printf("Rehashing...\n");

	error = 0;

	ret = state_rehash_process(state, 0, blockmax);
	if (ret == -1) {
		++error;
		/* continue, as we are already exiting */
	}

	/* abort if errors are present */
	if (error != 0)
		exit(EXIT_FAILURE);
}

