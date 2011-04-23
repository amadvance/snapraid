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
	unsigned diskmax = tommy_array_size(&state->diskarr);
	block_off_t i;
	unsigned j;
	unsigned char* buffer;
	int ret;
	data_off_t countsize;
	block_off_t countpos;
	block_off_t countmax;
	time_t start;
	time_t last;
	unsigned error;

	buffer = malloc_nofail(state->block_size);

	handle = malloc_nofail(diskmax * sizeof(struct snapraid_handle));
	for(i=0;i<diskmax;++i) {
		handle[i].disk = tommy_array_get(&state->diskarr, i);
		handle[i].file = 0;
		handle[i].f = -1;
	}
	error = 0;

	countmax = blockmax - blockstart;
	countsize = 0;
	countpos = 0;
	start = time(0);
	last = start;
	for(i=blockstart;i<blockmax;++i) {
		/* for each disk, process the block */
		for(j=0;j<diskmax;++j) {
			int read_size;
			struct snapraid_block* block;

			block = disk_block_get(handle[j].disk, i);
			if (!block)
				continue;

			ret = handle_close_if_different(&handle[j], block->file);
			if (ret == -1) {
				fprintf(stderr, "Stopping at block %u\n", i);
				++error;
				goto bail;
			}

			/* open the file for reading */
			ret = handle_open(&handle[j], block->file);
			if (ret == -1) {
				fprintf(stderr, "%u: Open error for file %s at position %u\n", i, block->file->sub, block_file_pos(block));
				++error;
				continue;
			}

			read_size = handle_read(&handle[j], block, buffer, state->block_size);
			if (read_size == -1) {
				fprintf(stderr, "%u: Read error for file %s at position %u\n", i, block->file->sub, block_file_pos(block));
				++error;
				continue;
			}

			countsize += read_size;
		}

		/* read the parity */
		ret = parity_read(state->parity, parity_f, i, buffer, state->block_size);
		if (ret == -1) {
			fprintf(stderr, "%u: Parity read error\n", i);
			++error;
		}

		/* read the qarity */
		if (state->level >= 2) {
			ret = parity_read(state->qarity, qarity_f, i, buffer, state->block_size);
			if (ret == -1) {
				fprintf(stderr, "%u: Syndrome read error\n", i);
				++error;
			}
		}
		

		/* count the number of processed block */
		++countpos;

		/* progress */
		if (state_progress(&start, &last, countpos, countmax, countsize)) {
			printf("Stopping for interruption at block %u\n", i);
			break;
		}
	}

	if (countmax)
		printf("%u%% completed, %u MiB processed\n", countpos * 100 / countmax, (unsigned)(countsize / (1024*1024)));
	else
		printf("Nothing to do\n");

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
	data_off_t size;
	int ret;
	int parity_f;
	int qarity_f;
	unsigned error;

	printf("Drying...\n");

	blockmax = parity_resize(state);
	size = blockmax * (data_off_t)state->block_size;

	if (blockstart > blockmax) {
		fprintf(stderr, "Error in the specified starting block %u. It's bigger than the parity size %u.\n", blockstart, blockmax);
		exit(EXIT_FAILURE);
	}

	/* adjust the number of block to process */
	if (blockcount != 0 && blockstart + blockcount < blockmax) {
		blockmax = blockstart + blockcount;
	}

	pathcpy(parity_path, sizeof(parity_path), state->parity);
	/* if drying, open the file for reading */
	/* it may fail if the file doesn't exist, in this case we continue to dry the files */
	parity_f = parity_open(parity_path);
	if (parity_f == -1) {
		printf("No accessible parity file.\n");
		/* continue anyway */
	}

	if (state->level >= 2) {
		pathcpy(qarity_path, sizeof(qarity_path), state->qarity);
		qarity_f = parity_open(qarity_path);
		if (qarity_f == -1) {
			printf("No accessible qarity file.\n");
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

