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

struct snapraid_scan {
	unsigned count_equal;
	unsigned count_change;
	unsigned count_remove;
	unsigned count_insert;
};

/**
 * Remove the specified file from the data set.
 */
static void scan_file_remove(struct snapraid_state* state, struct snapraid_disk* disk, struct snapraid_file* file)
{
	block_off_t i;

	/* free all the blocks of the file */
	for(i=0;i<file->blockmax;++i) {
		block_off_t block_pos = file->blockvec[i].parity_pos;

		/* adjust the first free position */
		if (disk->first_free_block > block_pos)
			disk->first_free_block = block_pos;

		tommy_array_set(&disk->blockarr, block_pos, 0);
	}

	/* remove the file from the file containers */
	tommy_hashdyn_remove_existing(&disk->fileset, &file->nodeset);
	tommy_list_remove_existing(&disk->filelist, &file->nodelist);

	/* deallocate */
	file_free(file);
}

/**
 * Insert the specified file in the data set.
 */
static void scan_file_insert(struct snapraid_state* state, struct snapraid_disk* disk, struct snapraid_file* file)
{
	block_off_t i;
	block_off_t block_max;
	block_off_t block_pos;

	/* allocate the blocks of the file */
	block_pos = disk->first_free_block;
	block_max = tommy_array_size(&disk->blockarr);
	for(i=0;i<file->blockmax;++i) {
		/* find a free block */
		while (block_pos < block_max && tommy_array_get(&disk->blockarr, block_pos) != 0)
			++block_pos;

		/* if not found, allocate a new one */
		if (block_pos == block_max) {
			++block_max;
			tommy_array_grow(&disk->blockarr, block_max);
		}

		/* set it */
		file->blockvec[i].parity_pos = block_pos;
		tommy_array_set(&disk->blockarr, block_pos, &file->blockvec[i]);
	}
	if (file->blockmax) {
		/* set the new free position, but only if allocated something */
		disk->first_free_block = block_pos + 1;
	}

	/* insert the file in the file containers */
	tommy_hashdyn_insert(&disk->fileset, &file->nodeset, file, file_hash(file->sub));
	tommy_list_insert_tail(&disk->filelist, &file->nodelist, file);
}

static void scan_file(struct snapraid_scan* scan, struct snapraid_state* state, struct snapraid_disk* disk, const char* path, const char* sub, const struct stat* st)
{
	struct snapraid_file* file;

	/* check if the file already exists */
	file = tommy_hashdyn_search(&disk->fileset, file_compare, sub, file_hash(sub));
	if (file) {
		/* check if the file is the same */
		if (file->size == st->st_size && file->mtime == st->st_mtime) {
			/* mark as present */
			++scan->count_equal;
			file->is_present = 1;
			return;
		} else {
			/* remove and reinsert it */
			++scan->count_change;
			--scan->count_insert;
			scan_file_remove(state, disk, file);
			/* continue to insert it */
		}
	}

	/* create the new file */
	++scan->count_insert;
	file = file_alloc(state->block_size, sub, st->st_size, st->st_mtime);
	file->is_present = 1;

	/* insert it */
	scan_file_insert(state, disk, file);
}

static void scan_dir(struct snapraid_scan* scan, struct snapraid_state* state, struct snapraid_disk* disk, const char* dir, const char* sub)
{
	DIR* d;
	struct dirent* dd;

	d = opendir(dir);
	if (!d) {
		fprintf(stderr, "Error accessing directory '%s'. %s.\n", dir, strerror(errno));
		exit(EXIT_FAILURE);
	}

	while ((dd = readdir(d)) != 0) {
		char path[PATH_MAX];
		struct stat st;

		snprintf(path, sizeof(path), "%s%s", dir, dd->d_name);

		if (stat(path, &st) != 0) {
			fprintf(stderr, "Error in stat file '%s'\n", path);
			exit(EXIT_FAILURE);
		}

		if (S_ISREG(st.st_mode)) {
			char sub_next[PATH_MAX];
			snprintf(sub_next, sizeof(sub_next), "%s%s", sub, dd->d_name);
			scan_file(scan, state, disk, path, sub_next, &st);
		} else if (S_ISDIR(st.st_mode)) {
			if (strcmp(dd->d_name, ".") != 0 && strcmp(dd->d_name, "..") != 0 && strcmp(dd->d_name, "lost+found") != 0
			) {
				char dir_next[PATH_MAX];
				char sub_next[PATH_MAX];
				snprintf(dir_next, sizeof(dir_next), "%s%s/", dir, dd->d_name);
				snprintf(sub_next, sizeof(sub_next), "%s%s/", sub, dd->d_name);
				scan_dir(scan, state, disk, dir_next, sub_next);
			}
		} else {
			if (state->verbose) {
				printf("warning: File '%s/%s' not processed\n", dir, dd->d_name);
			}
		}
	}

	closedir(d);
}

void state_scan(struct snapraid_state* state)
{
	unsigned diskmax = tommy_array_size(&state->diskarr);
	unsigned i;
	struct snapraid_scan* scan;

	scan = malloc_nofail(diskmax * sizeof(struct snapraid_scan));
	for(i=0;i<diskmax;++i) {
		scan[i].count_equal = 0;
		scan[i].count_change = 0;
		scan[i].count_remove = 0;
		scan[i].count_insert = 0;
	}

	for(i=0;i<diskmax;++i) {
		struct snapraid_disk* disk = tommy_array_get(&state->diskarr, i);
		tommy_node* node;

		printf("Scanning disk %s...\n", disk->name);

		scan_dir(&scan[i], state, disk, disk->dir, "");

		/* check for removed files */
		node = disk->filelist;
		while (node) {
			struct snapraid_file* file = node->data;

			/* next node */
			node = node->next;

			/* remove if not present */
			if (!file->is_present) {
				scan_file_remove(state, disk, file);
				++scan[i].count_remove;
			}
		}

		/* if all the previous file were removed */
		if (scan[i].count_equal == 0 && scan[i].count_remove != 0) {
			if (!state->force) {
				fprintf(stderr, "All the file in disk '%s' at dir '%s' are missing!\n", disk->name, disk->dir);
				fprintf(stderr, "If it's really what you want to do, use 'snapraid -f sync\n");
				exit(EXIT_FAILURE);
			}
		}
	}

	if (state->verbose) {
		struct snapraid_scan total;

		total.count_equal = 0;
		total.count_change = 0;
		total.count_remove = 0;
		total.count_insert = 0;

		for(i=0;i<diskmax;++i) {
			total.count_equal += scan[i].count_equal;
			total.count_change += scan[i].count_change;
			total.count_remove += scan[i].count_remove;
			total.count_insert += scan[i].count_insert;
		}

		printf("\tequal %d\n", total.count_equal);
		printf("\tchanged %d\n", total.count_change);
		printf("\tremoved %d\n", total.count_remove);
		printf("\tadded %d\n", total.count_insert);
	}
}

