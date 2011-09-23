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

/****************************************************************************/
/* dry */

static int state_dry_process(struct snapraid_state* state, int parity_f, int qarity_f, block_off_t blockstart, block_off_t blockmax)
{
	struct snapraid_handle* handle;
	unsigned diskmax;
	block_off_t i;
	unsigned j;
	unsigned char* buffer;
	int ret;
	data_off_t countsize;
	block_off_t countpos;
	block_off_t countmax;
	unsigned error;

	handle = handle_map(state, &diskmax);

	buffer = malloc_nofail(state->block_size);

	error = 0;

	countmax = blockmax - blockstart;
	countsize = 0;
	countpos = 0;
	state_progress_begin(state, blockstart, blockmax, countmax);
	for(i=blockstart;i<blockmax;++i) {
		/* for each disk, process the block */
		for(j=0;j<diskmax;++j) {
			int read_size;
			struct snapraid_block* block;

			block = disk_block_get(handle[j].disk, i);
			if (!block)
				continue;

			/* if the file is different than the current one, close it */
			if (handle[j].file != block_file_get(block)) {
				ret = handle_close(&handle[j]);
				if (ret == -1) {
					printf("Stopping at block %u\n", i);
					++error;
					goto bail;
				}
			}

			/* open the file for reading */
			ret = handle_open(&handle[j], block_file_get(block));
			if (ret == -1) {
				fprintf(stderr, "error:%u:%s:%s: Open error at position %u\n", i, handle[j].disk->name, block_file_get(block)->sub, block_file_pos(block));
				++error;
				continue;
			}

			read_size = handle_read(&handle[j], block, buffer, state->block_size);
			if (read_size == -1) {
				fprintf(stderr, "error:%u:%s:%s: Read error at position %u\n", i, handle[j].disk->name, block_file_get(block)->sub, block_file_pos(block));
				++error;
				continue;
			}

			countsize += read_size;
		}

		/* read the parity */
		if (parity_f != -1) {
			ret = parity_read(state->parity, parity_f, i, buffer, state->block_size);
			if (ret == -1) {
				fprintf(stderr, "error:%u:parity: Read error\n", i);
				++error;
			}
		}

		/* read the qarity */
		if (state->level >= 2) {
			if (qarity_f != -1) {
				ret = parity_read(state->qarity, qarity_f, i, buffer, state->block_size);
				if (ret == -1) {
					fprintf(stderr, "error:%u:qarity: Read error\n", i);
					++error;
				}
			}
		}

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
			++error;
		}
	}

	if (error) {
		if (error)
			printf("%u read errors\n", error);
		else
			printf("No read errors\n");
	} else {
		printf("No error\n");
	}

	free(handle);
	free(buffer);

	if (error != 0)
		return -1;
	return 0;
}

void state_dry(struct snapraid_state* state, block_off_t blockstart, block_off_t blockcount)
{
	char parity_path[PATH_MAX];
	char qarity_path[PATH_MAX];
	block_off_t blockmax;
	int ret;
	int parity_f;
	int qarity_f;
	unsigned error;

	printf("Drying...\n");

	blockmax = parity_resize(state);

	if (blockstart > blockmax) {
		fprintf(stderr, "Error in the specified starting block %u. It's bigger than the parity size %u.\n", blockstart, blockmax);
		exit(EXIT_FAILURE);
	}

	/* adjust the number of block to process */
	if (blockcount != 0 && blockstart + blockcount < blockmax) {
		blockmax = blockstart + blockcount;
	}

	pathcpy(parity_path, sizeof(parity_path), state->parity);
	pathcpy(qarity_path, sizeof(qarity_path), state->qarity);

	/* open the file for reading */
	/* it may fail if the file doesn't exist, in this case we continue to dry the files */
	parity_f = parity_open(parity_path);
	if (parity_f == -1) {
		printf("No accessible Parity file.\n");
		/* continue anyway */
	}

	if (state->level >= 2) {
		qarity_f = parity_open(qarity_path);
		if (qarity_f == -1) {
			printf("No accessible Q-Parity file.\n");
			/* continue anyway */
		}
	} else {
		qarity_f = -1;
	}

	error = 0;

	/* skip degenerated cases of empty parity, or skipping all */
	if (blockstart < blockmax) {
		ret = state_dry_process(state, parity_f, qarity_f, blockstart, blockmax);
		if (ret == -1) {
			++error;
			/* continue, as we are already exiting */
		}
	}

	/* try to close only if opened */
	if (parity_f != -1) {
		ret = parity_close(parity_path, parity_f);
		if (ret == -1) {
			++error;
			/* continue, as we are already exiting */
		}
	}

	if (state->level >= 2) {
		if (qarity_f != -1) {
			ret = parity_close(qarity_path, qarity_f);
			if (ret == -1) {
				++error;
				/* continue, as we are already exiting */
			}
		}
	}

	/* abort if required */
	if (error != 0)
		exit(EXIT_FAILURE);
}

