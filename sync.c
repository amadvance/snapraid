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

#include "portable.h"

#include "util.h"
#include "elem.h"
#include "state.h"
#include "parity.h"
#include "handle.h"
#include "raid.h"

/****************************************************************************/
/* sync */

static int state_sync_process(struct snapraid_state* state, int parity_f, int qarity_f, block_off_t blockstart, block_off_t blockmax)
{
	struct snapraid_handle* handle;
	unsigned diskmax;
	block_off_t i;
	unsigned j;
	void* buffer_alloc;
	unsigned char* buffer_aligned;
	unsigned char** buffer;
	unsigned buffermax;
	data_off_t countsize;
	block_off_t countpos;
	block_off_t countmax;
	int ret;
	unsigned unrecoverable_error;

	/* maps the disks to handles */
	handle = handle_map(state, &diskmax);

	/* we need disk + 1 for each parity level buffers */
	buffermax = diskmax + state->level;

	buffer_aligned = malloc_nofail_align(buffermax * state->block_size, &buffer_alloc);
	buffer = malloc_nofail(buffermax * sizeof(void*));
	for(i=0;i<buffermax;++i) {
		buffer[i] = buffer_aligned + i * state->block_size;
	}

	unrecoverable_error = 0;

	/* first count the number of blocks to process */
	countmax = 0;
	for(i=blockstart;i<blockmax;++i) {
		int one_invalid;

		/* for each disk */
		one_invalid = 0;
		for(j=0;j<diskmax;++j) {
			struct snapraid_block* block = 0;
			if (handle[j].disk)
				block = disk_block_get(handle[j].disk, i);
			if (block && !block_flag_has(block, BLOCK_HAS_HASH | BLOCK_HAS_PARITY)) {
				one_invalid = 1;
				break;
			}
		}

		/* if no invalid block skip */
		if (!one_invalid)
			continue;

		++countmax;
	}

	countsize = 0;
	countpos = 0;
	state_progress_begin(state, blockstart, blockmax, countmax);
	for(i=blockstart;i<blockmax;++i) {
		int one_invalid;
		int skip_this_block;
		int ret;

		/* for each disk */
		one_invalid = 0;
		for(j=0;j<diskmax;++j) {
			struct snapraid_block* block = 0;
			if (handle[j].disk)
				block = disk_block_get(handle[j].disk, i);
			if (block && !block_flag_has(block, BLOCK_HAS_HASH | BLOCK_HAS_PARITY)) {
				one_invalid = 1;
				break;
			}
		}

		/* if no invalid block skip */
		if (!one_invalid)
			continue;

		/* by default process the block, and skip it if something go wrong */
		skip_this_block = 0;

		/* for each disk, process the block */
		for(j=0;j<diskmax;++j) {
			int read_size;
			unsigned char hash[HASH_SIZE];
			struct snapraid_block* block;

			/* if the disk position is not used */
			if (!handle[j].disk) {
				/* use an empty block */
				memset(buffer[j], 0, state->block_size);
				continue;
			}

			/* if the disk block is not used */
			block = disk_block_get(handle[j].disk, i);
			if (!block) {
				/* use an empty block */
				memset(buffer[j], 0, state->block_size);
				continue;
			}

			/* if the file is different than the current one, close it */
			if (handle[j].file != block_file_get(block)) {
				ret = handle_close(&handle[j]);
				if (ret == -1) {
					/* This one is really an unexpected error, because we are only reading */
					/* and closing a descriptor should never fail */
					fprintf(stderr, "DANGER! Unexpected close error in a data disk, it isn't possible to sync.\n");
					printf("Stopping at block %u\n", i);
					++unrecoverable_error;
					goto bail;
				}
			}

			ret = handle_open(&handle[j], block_file_get(block));
			if (ret == -1) {
				if (errno == ENOENT) {
					fprintf(stderr, "Missing file '%s'.\n", handle[j].path);
					fprintf(stderr, "WARNING! You cannot modify data disk during a sync. Rerun the sync command when finished.\n");

					++unrecoverable_error;

					/* if the file is missing, it means that it was removed during sync */
					/* this isn't a serious error, so we skip this block, and continue with others */
					skip_this_block = 1;
					continue;
				} else if (errno == EACCES) {
					fprintf(stderr, "No access at file '%s'.\n", handle[j].path);
					fprintf(stderr, "WARNING! Please fix the access permission in the data disk.\n");
					printf("Stopping at block %u\n", i);
				} else {
					fprintf(stderr, "DANGER! Unexpected open error in a data disk, it isn't possible to sync.\n");
					printf("Stopping to allow recovery. Try with 'snapraid check'\n");
				}

				++unrecoverable_error;
				goto bail;
			}

			/* check if the file is changed */
			if (handle[j].st.st_size != block_file_get(block)->size
				|| handle[j].st.st_mtime != block_file_get(block)->mtime
				|| handle[j].st.st_ino != block_file_get(block)->inode
			) {
				if (handle[j].st.st_size != block_file_get(block)->size)
					fprintf(stderr, "Unexpected size change at file '%s'.\n", handle[j].path);
				else if (handle[j].st.st_mtime != block_file_get(block)->mtime)
					fprintf(stderr, "Unexpected time change at file '%s'.\n", handle[j].path);
				else
					fprintf(stderr, "Unexpected inode change from %"PRIu64" to %"PRIu64" at file '%s'.\n", block_file_get(block)->inode, handle[j].st.st_ino, handle[j].path);
				fprintf(stderr, "WARNING! You cannot modify files during a sync. Rerun the sync command when finished.\n");

				++unrecoverable_error;

				/* if the file is charnged, it means that it was modified during sync */
				/* this isn't a serious error, so we skip this block, and continue with others */
				skip_this_block = 1;
				continue;
			}

			read_size = handle_read(&handle[j], block, buffer[j], state->block_size);
			if (read_size == -1) {
				fprintf(stderr, "DANGER! Unexpected read error in a data disk, it isn't possible to sync.\n");
				printf("Stopping to allow recovery. Try with 'snapraid check'\n");
				++unrecoverable_error;
				goto bail;
			}

			/* now compute the hash */
			memhash(state->hash, hash, buffer[j], read_size);

			if (block_flag_has(block, BLOCK_HAS_HASH)) {
				/* compare the hash */
				if (memcmp(hash, block->hash, HASH_SIZE) != 0) {
					fprintf(stderr, "%u: Data error for file %s at position %u\n", i, block_file_get(block)->sub, block_file_pos(block));
					fprintf(stderr, "DANGER! Unexpected data error in a data disk, it isn't possible to sync.\n");
					printf("Stopping to allow recovery. Try with 'snapraid -s %u check'\n", i);
					++unrecoverable_error;
					goto bail;
				}
			} else {
				/* copy the hash, but doesn't mark the block as hashed */
				/* this allow on error to do not save the failed computation */
				memcpy(block->hash, hash, HASH_SIZE);
			}

			countsize += read_size;
		}

		/* if we have read all the data required, proceed with the parity */
		if (!skip_this_block) {

			/* compute the parity */
			raid_gen(state->level, buffer, diskmax, state->block_size);

			/* write the parity */
			ret = parity_write(state->parity, parity_f, i, buffer[diskmax], state->block_size);
			if (ret == -1) {
				fprintf(stderr, "DANGER! Write error in the Parity disk, it isn't possible to sync.\n");
				printf("Stopping at block %u\n", i);
				++unrecoverable_error;
				goto bail;
			}

			/* write the qarity */
			if (state->level >= 2) {
				ret = parity_write(state->qarity, qarity_f, i, buffer[diskmax+1], state->block_size);
				if (ret == -1) {
					fprintf(stderr, "DANGER! Write error in the Q-Parity disk, it isn't possible to sync.\n");
					printf("Stopping at block %u\n", i);
					++unrecoverable_error;
					goto bail;
				}
			}

			/* for each disk, mark the blocks as processed */
			for(j=0;j<diskmax;++j) {
				struct snapraid_block* block = 0;
				if (handle[j].disk)
					block = disk_block_get(handle[j].disk, i);
				if (!block)
					continue;

				/* now all the blocks have the hash and the parity computed */
				block_flag_set(block, BLOCK_HAS_HASH | BLOCK_HAS_PARITY);
			}
		}

		/* mark the state as needing write */
		state->need_write = 1;

		/* count the number of processed block */
		++countpos;

		/* progress */
		if (state_progress(state, i, countpos, countmax, countsize)) {
			break;
		}
	}

	state_progress_end(state, countpos, countmax, countsize);

bail:
	for(j=0;j<diskmax;++j) {
		ret = handle_close(&handle[j]);
		if (ret == -1) {
			fprintf(stderr, "DANGER! Unexpected close error in a data disk.\n");
			++unrecoverable_error;
			/* continue, as we are already exiting */
		}
	}

	free(handle);
	free(buffer_alloc);
	free(buffer);

	if (unrecoverable_error != 0)
		return -1;
	return 0;
}

int state_sync(struct snapraid_state* state, block_off_t blockstart, block_off_t blockcount)
{
	char parity_path[PATH_MAX];
	char qarity_path[PATH_MAX];
	block_off_t blockmax;
	data_off_t size;
	int ret;
	int parity_f;
	int qarity_f;
	unsigned unrecoverable_error;

	printf("Syncing...\n");

	blockmax = parity_resize(state);
	size = blockmax * (data_off_t)state->block_size;

	if (blockstart > blockmax) {
		fprintf(stderr, "Error in the starting block %u. It's bigger than the parity size %u.\n", blockstart, blockmax);
		exit(EXIT_FAILURE);
	}

	/* adjust the number of block to process */
	if (blockcount != 0 && blockstart + blockcount < blockmax) {
		blockmax = blockstart + blockcount;
	}

	pathcpy(parity_path, sizeof(parity_path), state->parity);
	pathcpy(qarity_path, sizeof(qarity_path), state->qarity);

	/* create the file and open for writing */
	parity_f = parity_create(parity_path, size);
	if (parity_f == -1) {
		fprintf(stderr, "WARNING! Without an accessible Parity file, it isn't possible to sync.\n");
		exit(EXIT_FAILURE);
	}

	if (state->level >= 2) {
		qarity_f = parity_create(qarity_path, size);
		if (qarity_f == -1) {
			fprintf(stderr, "WARNING! Without an accessible Q-Parity file, it isn't possible to sync.\n");
			exit(EXIT_FAILURE);
		}
	} else {
		qarity_f = -1;
	}

	unrecoverable_error = 0;

	/* skip degenerated cases of empty parity, or skipping all */
	if (blockstart < blockmax) {
		ret = state_sync_process(state, parity_f, qarity_f, blockstart, blockmax);
		if (ret == -1) {
			++unrecoverable_error;
			/* continue, as we are already exiting */
		}
	}

	ret = parity_sync(parity_path, parity_f);
	if (ret == -1) {
		fprintf(stderr, "DANGER! Unexpected sync error in Parity disk.\n");
		++unrecoverable_error;
		/* continue, as we are already exiting */
	}

	ret = parity_close(parity_path, parity_f);
	if (ret == -1) {
		fprintf(stderr, "DANGER! Unexpected close error in Parity disk.\n");
		++unrecoverable_error;
		/* continue, as we are already exiting */
	}

	if (state->level >= 2) {
		ret = parity_sync(qarity_path, qarity_f);
		if (ret == -1) {
			fprintf(stderr, "DANGER! Unexpected sync error in Q-Parity disk.\n");
			++unrecoverable_error;
			/* continue, as we are already exiting */
		}

		ret = parity_close(qarity_path, qarity_f);
		if (ret == -1) {
			fprintf(stderr, "DANGER! Unexpected close error in Q-Parity disk.\n");
			++unrecoverable_error;
			/* continue, as we are already exiting */
		}
	}

	/* abort if required */
	if (unrecoverable_error != 0)
		return -1;
	return 0;
}

