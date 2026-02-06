/*
 * Copyright (C) 2026 Andrea Mazzoleni
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

#include "elem.h"
#include "parity.h"
#include "state.h"
#include "stream.h"
#include "locate.h"

struct snapraid_parity_entry {
	block_off_t block_size;
	block_off_t low; /**< Lower position in the parity */
	block_off_t high; /**< Higher position in the parity */
	block_off_t fragments; /**< Number of fragments in the parity */
	struct snapraid_file* file;
	struct snapraid_disk* disk;
	tommy_node node;
};

struct snapraid_locate_info{
    data_off_t block_max;
	data_off_t parity_size;
	block_off_t tail_block;
    block_off_t min_occupied_block_number;
};

static int parity_entry_compare(const void* void_a, const void* void_b)
{
	const struct snapraid_parity_entry* entry_a = void_a;
	const struct snapraid_parity_entry* entry_b = void_b;

	if (entry_a->high < entry_b->high)
		return -1;
	if (entry_a->high > entry_b->high)
		return 1;

	if (entry_a->low < entry_b->low)
		return -1;
	if (entry_a->low > entry_b->low)
		return 1;

	return strcmp(entry_a->file->sub, entry_b->file->sub);
}

static void dump_entry(void* void_entry)
{
	char esc_buffer[ESC_MAX];
	struct snapraid_parity_entry* entry = void_entry;
	printf("%12" PRIu64 " ", entry->low * (uint64_t)entry->block_size);
	printf("%12" PRIu64 " ", (entry->high - entry->low + 1) * (uint64_t)entry->block_size);
	printf("%8u ", entry->fragments);
	printf("%s\n", fmt_term(entry->disk, entry->file->sub, esc_buffer));
}

static void add_size_to_sum(void* arg, void* void_entry)
{
	struct snapraid_parity_entry* entry = void_entry;
	data_off_t* size_sum = arg;

	*size_sum += entry->file->size;
}

static void collect_parity_block_file(uint32_t block_size, struct snapraid_disk* disk, struct snapraid_file* file, tommy_list* file_list, block_off_t low_occupied_block_number)
{
	block_off_t parity_low = 0; /* lower block */
	block_off_t parity_high = 0; /* higher block */
	block_off_t parity_fragments = 0; /* number of fragments */

	for (block_off_t i = 0; i < file->blockmax; ++i) {
		block_off_t parity_pos = fs_file2par_find(disk, file, i);

		/* check if a valid position is found */
		if (parity_pos == POS_NULL) {
			/* block not yet allocated */
			continue;
		}

		if (parity_fragments == 0) {
			parity_low = parity_pos;
			parity_high = parity_pos;
			parity_fragments = 1;
		} else {
			if (parity_pos != parity_high + 1)
				++parity_fragments;
			parity_high = parity_pos;
		}
	}

	if (parity_fragments == 0)
		return; /* not allocated */

	if (low_occupied_block_number > 0 && parity_high < low_occupied_block_number)
		return; /* entry not relevant */

	/* found a relevant block so add the corresponding file */
	struct snapraid_parity_entry* entry = malloc_nofail(sizeof(struct snapraid_parity_entry));
	entry->block_size = block_size;
	entry->file = file;
	entry->disk = disk;
	entry->low = parity_low;
	entry->high = parity_high;
	entry->fragments = parity_fragments;
	tommy_list_insert_tail(file_list, &entry->node, entry);
}

void state_locate_info(struct snapraid_state* state, uint64_t parity_tail, struct snapraid_locate_info* info)
{
	uint64_t block_size = state->block_size;

	info->block_max = parity_allocated_size(state);
	info->parity_size = info->block_max * block_size;
	info->tail_block = (parity_tail + block_size - 1) / block_size;
	info->min_occupied_block_number = info->block_max - info->tail_block;
}

void state_locate(struct snapraid_state* state, uint64_t parity_tail)
{
	char buf[64];
	uint64_t block_size = state->block_size;

	block_off_t min_occupied_block_number;

	printf("SnapRAID locate report:\n");
	printf("\n");

	if (parity_tail == 0) {
		printf("Locate all files\n\n");
		min_occupied_block_number = 0;
	} else {
		printf("Locate files within the tail of %sB of the parity\n\n", fmt_size(parity_tail, buf, sizeof(buf)));
		struct snapraid_locate_info info;
		state_locate_info(state, parity_tail, &info);
		
		printf("Current parity size is %sB\n", fmt_size(info.parity_size, buf, sizeof(buf)));	
		if (info.tail_block >= info.block_max) {
			printf("Specified tail greater than the parity size!\n");
			return;
		}

		min_occupied_block_number = info.min_occupied_block_number;
	}

	msg_progress("Collecting files with offset greater or equal to %" PRIu64 "\n", min_occupied_block_number * block_size);

	tommy_list files;
	tommy_list_init(&files);
	for (tommy_node* i = tommy_list_head(&state->disklist); i != 0; i = i->next) {
		struct snapraid_disk* disk = i->data;
		for (tommy_node* j = tommy_list_head(&disk->filelist); j != 0; j = j->next) {
			struct snapraid_file* file = j->data;
			collect_parity_block_file(block_size, disk, file, &files, min_occupied_block_number);
		}
	}

	printf("\n");

	if (tommy_list_count(&files) == 0) {
		printf("No files located in the specified parity tail.\n");
	} else {
		tommy_list_sort(&files, parity_entry_compare);

		data_off_t total_size_located = 0;

		tommy_list_foreach_arg(&files, add_size_to_sum, &total_size_located);

		printf("Located data in this range: %sB\n", fmt_size(total_size_located, buf, sizeof(buf)));

		printf("\n");

		/*      |<##################################################################72>|####80>| */
		printf("       Offset         Span    Frags\n");

		tommy_list_foreach(&files, dump_entry);
	}

	tommy_list_foreach(&files, free);
}

void state_locate_mark_tail_blocks_for_resync(struct snapraid_state* state, uint64_t parity_tail)
{
	struct snapraid_locate_info info;
	state_locate_info(state, parity_tail, &info);
	block_off_t min_occupied_block_number = info.min_occupied_block_number;				
	printf("Forcing reallocation of all tail blocks from block number %u onwards\n", min_occupied_block_number);

	for (tommy_node* i = tommy_list_head(&state->disklist); i != 0; i = i->next) {
		struct snapraid_disk* disk = i->data;
		for (tommy_node* j = tommy_list_head(&disk->filelist); j != 0; j = j->next) {
			struct snapraid_file* file = j->data;
			for (block_off_t f = 0; f < file->blockmax; ++f) {
				block_off_t parity_pos = fs_file2par_find(disk, file, f);
				if (parity_pos == POS_NULL)
					continue; /* not allocated */
				if (parity_pos < min_occupied_block_number)
					continue; /* not relevant */

				/* mark the file for reallocation */
				struct snapraid_block* block = fs_file2block_get(file, f);
				
				// TODO: check: is condition correct or should we always set BLOCK_STATE_REP?
				if (block_state_get(block) == BLOCK_STATE_BLK) {
					block_state_set(block, BLOCK_STATE_REP);
				}
			}
		}
	}
}