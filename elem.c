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

#include "elem.h"
#include "util.h"

/****************************************************************************/
/* snapraid */

block_off_t block_file_pos(struct snapraid_block* block)
{
	struct snapraid_file* file = block->file;

	if (block < file->blockvec || block >= file->blockvec + file->blockmax) {
		fprintf(stderr, "Internal inconsistency in block %u ownership\n", block->parity_pos);
		exit(EXIT_FAILURE);
	}

	return block - file->blockvec;
}

unsigned block_file_size(struct snapraid_block* block, unsigned block_size)
{
	block_off_t pos = block_file_pos(block);

	/* if it's the last block */
	if (pos + 1 == block->file->blockmax) {
		unsigned remainder;
		if (block->file->size == 0)
			return 0;
		remainder = block->file->size % block_size;
		if (remainder == 0)
			remainder = block_size;
		return remainder;
	}

	return block_size;
}

struct snapraid_file* file_alloc(unsigned block_size, const char* sub, data_off_t size, time_t mtime)
{
	struct snapraid_file* file;
	block_off_t i;

	file = malloc_nofail(sizeof(struct snapraid_file));
	snprintf(file->sub, sizeof(file->sub), "%s", sub);
	file->size = size;
	file->blockmax = (size + block_size - 1) / block_size;
	file->mtime = mtime;
	file->is_present = 0;
	file->blockvec = malloc_nofail(file->blockmax * sizeof(struct snapraid_block));

	/* set the back pointer */
	for(i=0;i<file->blockmax;++i) {
		file->blockvec[i].parity_pos = POS_INVALID;
		file->blockvec[i].is_hashed = 0;
		file->blockvec[i].file = file;
	}

	return file;
}

void file_free(struct snapraid_file* file)
{
	free(file->blockvec);
	free(file);
}

int file_compare(const void* void_arg, const void* void_data)
{
	const char* arg = void_arg;
	const struct snapraid_file* file = void_data;
	return strcmp(arg, file->sub);
}

struct snapraid_disk* disk_alloc(const char* name, const char* dir)
{
	struct snapraid_disk* disk;

	disk = malloc_nofail(sizeof(struct snapraid_disk));
	snprintf(disk->name, sizeof(disk->name), "%s", name);
	snprintf(disk->dir, sizeof(disk->dir), "%s", dir);
	disk->first_free_block = 0;
	tommy_list_init(&disk->filelist);
	tommy_hashdyn_init(&disk->fileset);
	tommy_array_init(&disk->blockarr);

	return disk;
}

void disk_free(struct snapraid_disk* disk)
{
	tommy_node* node = disk->filelist;
	while (node) {
		struct snapraid_file* file = node->data;
		node = node->next;
		file_free(file);
	}
	tommy_hashdyn_done(&disk->fileset);
	tommy_array_done(&disk->blockarr);
	free(disk);
}

