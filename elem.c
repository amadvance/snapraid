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

struct snapraid_filter* filter_alloc(const char* pattern)
{
	struct snapraid_filter* filter;
	char* i;
	char* first;
	char* last;

	filter = malloc_nofail(sizeof(struct snapraid_filter));
	pathcpy(filter->pattern, sizeof(filter->pattern), pattern);

	/* find first and last slash */
	first = 0;
	last = 0;
	for(i=filter->pattern;*i;++i) {
		if (*i == '/') {
			if (!first)
				first = i;
			last = i;
		}
	}

	if (first == 0) {
		/* no slash */
		filter->is_path = 0;
		filter->is_dir = 0;
	} else if (first == last && last[1] == 0) {
		/* one slash at the end */
		filter->is_path = 0;
		filter->is_dir = 1;
		last[0] = 0;
	} else {
		/* at least a slash not at the end */
		filter->is_path = 1;
		if (last[1] == 0) {
			filter->is_dir = 1;
			last[0] = 0;
		} else {
			filter->is_dir = 0;
		}

		/* a slash must be the first char, as we reject PATH/DIR/FILE */
		if (filter->pattern[0] != '/') {
			free(filter);
			return 0;
		}
	}

	return filter;
}

void filter_free(struct snapraid_filter* filter)
{
	free(filter);
}

int filter_path(struct snapraid_filter* filter, const char* path, const char* name, int is_dir)
{
	/* matches dirs with dirs and files with files */
	if (filter->is_dir && !is_dir)
		return -1;
	if (!filter->is_dir && is_dir)
		return -1;

	if (filter->is_path) {
		/* skip initial slash, as always missing from the path */
		if (fnmatch(filter->pattern + 1, path, FNM_PATHNAME) == 0)
			return 0;
	} else {
		if (fnmatch(filter->pattern, name, 0) == 0)
			return 0;
	}

	return -1;
}

int filter_file(struct snapraid_filter* filter, struct snapraid_file* file)
{
	/* only partially correct as we are not filtering dirs */
	return filter_path(filter, file->sub, file_name(file), 0);
}

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

struct snapraid_file* file_alloc(unsigned block_size, const char* sub, uint64_t size, uint64_t mtime, uint64_t inode)
{
	struct snapraid_file* file;
	block_off_t i;

	file = malloc_nofail(sizeof(struct snapraid_file));
	pathcpy(file->sub, sizeof(file->sub), sub);
	file->size = size;
	file->blockmax = (size + block_size - 1) / block_size;
	file->mtime = mtime;
	file->inode = inode;
	file->is_present = 0;
	file->is_filtered = 0;
	file->blockvec = malloc_nofail(file->blockmax * sizeof(struct snapraid_block));

	/* set the back pointer */
	for(i=0;i<file->blockmax;++i) {
		file->blockvec[i].parity_pos = POS_INVALID;
		file->blockvec[i].flag = 0;
		file->blockvec[i].file = file;
	}

	return file;
}

void file_free(struct snapraid_file* file)
{
	free(file->blockvec);
	free(file);
}

const char* file_name(struct snapraid_file* file)
{
	const char* r = strrchr(file->sub, '/');
	if (!r)
		r = file->sub;
	else
		++r;
	return r;
}

int file_inode_compare(const void* void_arg, const void* void_data)
{
	const uint64_t* arg = void_arg;
	const struct snapraid_file* file = void_data;
	if (*arg < file->inode)
		return -1;
	if (*arg > file->inode)
		return 1;
	return 0;
}

struct snapraid_disk* disk_alloc(const char* name, const char* dir)
{
	struct snapraid_disk* disk;

	disk = malloc_nofail(sizeof(struct snapraid_disk));
	pathcpy(disk->name, sizeof(disk->name), name);
	pathcpy(disk->dir, sizeof(disk->dir), dir);

	/* ensure that the dir terminate with "/" if it isn't empty */
	pathslash(disk->dir, sizeof(disk->dir));

	disk->first_free_block = 0;
	tommy_list_init(&disk->filelist);
	tommy_hashdyn_init(&disk->inodeset);
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
	tommy_hashdyn_done(&disk->inodeset);
	tommy_array_done(&disk->blockarr);
	free(disk);
}

