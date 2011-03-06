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
/* sync */

static void state_sync_process(struct snapraid_state* state, int parity_f, pos_t blockstart, pos_t blockmax)
{
	struct snapraid_handle* handle;
	unsigned diskmax = tommy_array_size(&state->diskarr);
	pos_t i;
	unsigned j;
	unsigned char* block_buffer;
	unsigned char* xor_buffer;
	uint64_t count_size;
	uint64_t count_block;
	time_t start;
	time_t last;

	block_buffer = malloc_nofail(state->block_size);
	xor_buffer = malloc_nofail(state->block_size);

	handle = malloc_nofail(diskmax * sizeof(struct snapraid_handle));
	for(i=0;i<diskmax;++i) {
		handle[i].disk = tommy_array_get(&state->diskarr, i);
		handle[i].file = 0;
		handle[i].f = -1;
	}

	count_size = 0;
	count_block = 0;
	start = time(0);
	last = start;

	for(i=blockstart;i<blockmax;++i) {
		int unhashed;

		/* for each disk, search for an unhashed block */
		unhashed = 0;
		for(j=0;j<diskmax;++j) {
			struct snapraid_block* block = disk_block_get(handle[j].disk, i);
			if (block && !block->is_hashed) {
				unhashed = 1;
				break;
			}
		}

		if (!unhashed)
			continue;

		/* start with 0 */
		memset(xor_buffer, 0, state->block_size);

		/* for each disk, process the block */
		for(j=0;j<diskmax;++j) {
			int read_size;
			struct md5_t md5;
			unsigned char hash[HASH_MAX];
			struct snapraid_block* block;

			block = disk_block_get(handle[j].disk, i);
			if (!block)
				continue;

			handle_open(0, &handle[j], block->file);

			read_size = handle_read(0, &handle[j], block, block_buffer, state->block_size);

			/* now compute the hash */
			md5_init(&md5);
			md5_update(&md5, block_buffer, read_size);
			md5_final(&md5, hash);

			if (block->is_hashed) {
				/* compare the hash */
				if (memcmp(hash, block->hash, HASH_MAX) != 0) {
					fprintf(stderr, "%u: Data error for file %s at position %u\n", i, block->file->sub, block_file_pos(block));
					fprintf(stderr, "Stopping to allow recovery. Try with 'snapraid -s %u fix'\n", i);
					exit(EXIT_FAILURE);
				}
			} else {
				/* copy the hash */
				memcpy(block->hash, hash, HASH_MAX);

				/* mark the block as hashed */
				block->is_hashed = 1;
			}

			/* compute the parity */
			memxor(xor_buffer, block_buffer, read_size);

			count_size += read_size;
		}

		/* write the parity */
		parity_write(state->parity, parity_f, i, xor_buffer, state->block_size);

		/* count the number of processed block */
		++count_block;

		/* progress */
		if (state_progress(&start, &last, i, blockmax, count_block, count_size))
			break;
	}

	printf("%u%% completed, %u MB processed\n", i * 100 / blockmax, (unsigned)(count_size / (1024*1024)));

	printf("Done\n");

	for(j=0;j<diskmax;++j) {
		handle_close(&handle[j]);
	}

	free(handle);
	free(block_buffer);
	free(xor_buffer);
}

void state_sync(struct snapraid_state* state, pos_t blockstart)
{
	char path[PATH_MAX];
	pos_t blockmax;
	off_t size;
	int f;

	printf("Syncing...\n");

	blockmax = parity_resize(state);
	size = blockmax * (off_t)state->block_size;

	if (blockstart >= blockmax) {
		fprintf(stderr, "The specified starting block %u is bigger than the parity size %u.\n", blockstart, blockmax);
		exit(EXIT_FAILURE);
	}

	snprintf(path, sizeof(path), "%s", state->parity);
	f = parity_create(path, size);

	state_sync_process(state, f, blockstart, blockmax);

	parity_close(path, f);
}

