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
	unsigned count_moved;
	unsigned count_change;
	unsigned count_remove;
	unsigned count_insert;
};

/**
 * Removes the specified file from the data set.
 */
static void scan_file_remove(struct snapraid_state* state, struct snapraid_disk* disk, struct snapraid_file* file)
{
	block_off_t i, j;
	unsigned diskmax = tommy_array_size(&state->diskarr);

	/* state changed */
	state->need_write = 1;

	/* free all the blocks of the file */
	for(i=0;i<file->blockmax;++i) {
		block_off_t block_pos = file->blockvec[i].parity_pos;

		/* adjust the first free position */
		if (disk->first_free_block > block_pos)
			disk->first_free_block = block_pos;

		/* clear the block */
		tommy_array_set(&disk->blockarr, block_pos, 0);

		/* invalidate the block of all the other disks */
		for(j=0;j<diskmax;++j) {
			struct snapraid_disk* oth_disk = tommy_array_get(&state->diskarr, j);
			struct snapraid_block* oth_block = disk_block_get(oth_disk, block_pos);
			if (oth_block) {
				/* remove the parity info for this block */
				block_flag_clear(oth_block, BLOCK_HAS_PARITY);
			}
		}
	}

	/* remove the file from the file containers */
	tommy_hashdyn_remove_existing(&disk->inodeset, &file->nodeset);
	tommy_list_remove_existing(&disk->filelist, &file->nodelist);

	/* deallocate */
	file_free(file);
}

/**
 * Inserts the specified file in the data set.
 */
static void scan_file_insert(struct snapraid_state* state, struct snapraid_disk* disk, struct snapraid_file* file)
{
	block_off_t i;
	block_off_t block_max;
	block_off_t block_pos;

	/* state changed */
	state->need_write = 1;

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
	tommy_hashdyn_insert(&disk->inodeset, &file->nodeset, file, file_inode_hash(file->inode));
	tommy_list_insert_tail(&disk->filelist, &file->nodelist, file);
}

/**
 * Processes a file.
 */
static void scan_file(struct snapraid_scan* scan, struct snapraid_state* state, struct snapraid_disk* disk, const char* sub, const struct stat* st)
{
	struct snapraid_file* file;
	uint64_t inode;

	/* check if the file already exists */
	inode = st->st_ino;
	file = tommy_hashdyn_search(&disk->inodeset, file_inode_compare, &inode, file_inode_hash(inode));
	if (file) {
		/* check if multiple files have the same inode */
		if (file_flag_has(file, FILE_IS_PRESENT)) {
			if (st->st_nlink > 1) {
				printf("warning: Ignored hardlink '/%s'\n", sub);
				return;
			} else {
				fprintf(stderr, "Internal inode '%llu' inconsistency for file '%s'\n", inode, sub);
				exit(EXIT_FAILURE);
			}
		}

		/* check if the file is not changed */
		if (file->size == st->st_size && file->mtime == st->st_mtime) {
			/* mark as present */
			file_flag_set(file, FILE_IS_PRESENT);

			/* if the path is different, it means a moved file */
			if (strcmp(file->sub, sub) != 0) {
				++scan->count_moved;

				/* save the new name */
				pathcpy(file->sub, sizeof(file->sub), sub);

				/* we have to save the new name */
				state->need_write = 1;
			} else {
				/* otherwise it's equal */
				++scan->count_equal;
			}
			return;
		} else {
			/* do a safety check to ensure that the common ext4 case of zeroing */
			/* the size of a file after a crash doesn't propagate to the backup */
			if (file->size != 0 && st->st_size == 0) {
				/* do the check ONLY if the name is the same */
				/* otherwise it could be a deleted and recreated file */
				if (strcmp(file->sub, sub) == 0) {
					if (!state->force_zero) {
						fprintf(stderr, "The file '%s' in disk '%s' at dir '%s' has now zero size!\n", sub, disk->name, disk->dir);
						fprintf(stderr, "If it's really what you want to sync it, use 'snapraid --force-zero sync\n");
						exit(EXIT_FAILURE);
					}
				}
			}

			/* remove and reinsert it */
			++scan->count_change;
			--scan->count_insert;
			scan_file_remove(state, disk, file);
			/* continue to insert it */
		}
	}

	/* create the new file */
	++scan->count_insert;
	file = file_alloc(state->block_size, sub, st->st_size, st->st_mtime, st->st_ino);

	/* mark it as present */
	file_flag_set(file, FILE_IS_PRESENT);

	/* insert it */
	scan_file_insert(state, disk, file);
}

/**
 * Processes a directory.
 */
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
		char path_next[PATH_MAX];
		char sub_next[PATH_MAX];
		struct stat st;
		const char* name = dd->d_name;

		/* skip "." and ".." files */
		if (name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0)))
			continue;

		pathprint(path_next, sizeof(path_next), "%s%s", dir, name);
		pathprint(sub_next, sizeof(sub_next), "%s%s", sub, name);

		if (stat(path_next, &st) != 0) {
			fprintf(stderr, "Error in stat file '%s'\n", path_next);
			exit(EXIT_FAILURE);
		}

		/* check for not supported file names, mainly derived from the content file format */
		if (name[0] == 0 || isspace(name[0]) || isspace(name[strlen(name)-1])) {
			fprintf(stderr, "Unsupported name '%s' in file '%s'\n", name, path_next);
			exit(EXIT_FAILURE);
		}

		if (S_ISREG(st.st_mode)) {
			if (filter_path(&state->filterlist, sub_next, 0) == 0) {
				scan_file(scan, state, disk, sub_next, &st);
			} else {
				if (state->verbose) {
					printf("Excluding file '/%s'\n", sub_next);
				}
			}
		} else if (S_ISDIR(st.st_mode)) {
			if (filter_path(&state->filterlist, sub_next, 1) == 0) {
				pathslash(path_next, sizeof(path_next));
				pathslash(sub_next, sizeof(sub_next));
				scan_dir(scan, state, disk, path_next, sub_next);
			} else {
				if (state->verbose) {
					printf("Excluding directory '/%s'\n", sub_next);
				}
			}
		} else {
			if (state->verbose) {
				printf("warning: Ignored special file '/%s'\n", sub_next);
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
		scan[i].count_moved = 0;
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
			if (!file_flag_has(file, FILE_IS_PRESENT)) {
				scan_file_remove(state, disk, file);
				++scan[i].count_remove;
			}
		}

		/* if all the previous file were removed */
		if (scan[i].count_equal == 0 && scan[i].count_moved == 0 && scan[i].count_remove != 0) {
			if (!state->force_empty) {
				fprintf(stderr, "All the files in disk '%s' at dir '%s' are missing!\n", disk->name, disk->dir);
				fprintf(stderr, "If it's really what you want to sync it, use 'snapraid --force-empty sync\n");
				exit(EXIT_FAILURE);
			}
		}
	}

	if (state->verbose) {
		struct snapraid_scan total;

		total.count_equal = 0;
		total.count_moved = 0;
		total.count_change = 0;
		total.count_remove = 0;
		total.count_insert = 0;

		for(i=0;i<diskmax;++i) {
			total.count_equal += scan[i].count_equal;
			total.count_moved += scan[i].count_moved;
			total.count_change += scan[i].count_change;
			total.count_remove += scan[i].count_remove;
			total.count_insert += scan[i].count_insert;
		}

		printf("\tequal %d\n", total.count_equal);
		printf("\tmoved %d\n", total.count_moved);
		printf("\tchanged %d\n", total.count_change);
		printf("\tremoved %d\n", total.count_remove);
		printf("\tadded %d\n", total.count_insert);
	}

	free(scan);
}

