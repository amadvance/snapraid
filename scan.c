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
	/**
	 * Counters of changes.
	 */
	unsigned count_equal;
	unsigned count_moved;
	unsigned count_change;
	unsigned count_remove;
	unsigned count_insert;

	tommy_list file_insert_list; /**< Files to insert. */
	tommy_list link_insert_list; /**< Links to insert. */

	/* nodes for data structures */
	tommy_node node;
};

/**
 * Removes the specified file from the data set.
 */
static void scan_file_remove(struct snapraid_state* state, struct snapraid_disk* disk, struct snapraid_file* file)
{
	block_off_t i;

	/* state changed */
	state->need_write = 1;

	/* free all the blocks of the file */
	for(i=0;i<file->blockmax;++i) {
		block_off_t block_pos = file->blockvec[i].parity_pos;
		tommy_node* j;

		/* adjust the first free position */
		if (disk->first_free_block > block_pos)
			disk->first_free_block = block_pos;

		/* set the block as deleted */
		tommy_array_set(&disk->blockarr, block_pos, BLOCK_DELETED);

		/* invalidate the block of all the other disks */
		for(j=state->disklist;j!=0;j=j->next) {
			struct snapraid_disk* oth_disk = j->data;
			struct snapraid_block* oth_block = disk_block_get(oth_disk, block_pos);

			if (block_is_valid(oth_block)) {
				/* remove the parity info for this block */
				block_clear_parity(oth_block);
			}
		}
	}

	/* remove the file from the file containers */
	tommy_hashdyn_remove_existing(&disk->inodeset, &file->nodeset);
	tommy_hashdyn_remove_existing(&disk->pathset, &file->pathset);
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
		while (block_pos < block_max && block_is_valid(tommy_array_get(&disk->blockarr, block_pos)))
			++block_pos;

		/* if not found, allocate a new one */
		if (block_pos == block_max) {
			++block_max;
			tommy_array_grow(&disk->blockarr, block_max);
		}

		/* set the position */
		file->blockvec[i].parity_pos = block_pos;

		/* set the state depending if the used block was EMPTY or DELETED */
		if (tommy_array_get(&disk->blockarr, block_pos) == BLOCK_EMPTY)
			block_state_set(&file->blockvec[i], BLOCK_STATE_NEW);
		else
			block_state_set(&file->blockvec[i], BLOCK_STATE_CHG);

		/* store in the disk map */
		tommy_array_set(&disk->blockarr, block_pos, &file->blockvec[i]);
	}
	if (file->blockmax) {
		/* set the new free position, but only if allocated something */
		disk->first_free_block = block_pos + 1;
	}

	/* insert the file in the file containers */
	tommy_hashdyn_insert(&disk->inodeset, &file->nodeset, file, file_inode_hash(file->inode));
	tommy_hashdyn_insert(&disk->pathset, &file->pathset, file, file_path_hash(file->sub));
	tommy_list_insert_tail(&disk->filelist, &file->nodelist, file);
}

/**
 * Processes a file.
 */
static void scan_file(struct snapraid_scan* scan, struct snapraid_state* state, int output, struct snapraid_disk* disk, const char* sub, const struct stat* st)
{
	struct snapraid_file* file;

	if (state->find_by_name) {
		/* check if the file path already exists */
		file = tommy_hashdyn_search(&disk->pathset, file_path_compare, sub, file_path_hash(sub));
	} else {
		/* check if the file inode already exists */
		uint64_t inode = st->st_ino;
		file = tommy_hashdyn_search(&disk->inodeset, file_inode_compare, &inode, file_inode_hash(inode));
	}

	if (file) {
		/* check if multiple files have the same inode */
		if (file_flag_has(file, FILE_IS_PRESENT)) {
			if (st->st_nlink > 1) {
				printf("warning: Ignored hardlink '%s%s'\n", disk->dir, sub);
				return;
			} else {
				fprintf(stderr, "Internal inode '%"PRIu64"' inconsistency for file '%s%s'\n", st->st_ino, disk->dir, sub);
				exit(EXIT_FAILURE);
			}
		}

		/* check if the file is not changed */
		if (file->size == st->st_size && file->mtime == st->st_mtime) {
			/* mark as present */
			file_flag_set(file, FILE_IS_PRESENT);

			if (strcmp(file->sub, sub) != 0) {
				/* if the path is different, it means a moved file with the same inode */
				++scan->count_moved;

				if (file->inode != st->st_ino) {
					fprintf(stderr, "Internal inode inconsistency for file '%s%s'\n", disk->dir, sub);
					exit(EXIT_FAILURE);
				}

				if (state->gui) {
					fprintf(stderr, "scan:move:%s:%s:%s\n", disk->name, file->sub, sub);
					fflush(stderr);
				}
				if (output) {
					printf("Move '%s%s' '%s%s'\n", disk->dir, file->sub, disk->dir, sub);
				}

				/* remove from the set */
				tommy_hashdyn_remove_existing(&disk->pathset, &file->pathset);

				/* save the new name */
				pathcpy(file->sub, sizeof(file->sub), sub);

				/* reinsert in the set */
				tommy_hashdyn_insert(&disk->pathset, &file->pathset, file, file_path_hash(file->sub));

				/* we have to save the new name */
				state->need_write = 1;
			} else if (file->inode != st->st_ino) {
				/* if the inode is different, it means a rewritten file with the same path */
				++scan->count_moved;

				if (state->gui) {
					fprintf(stderr, "scan:move:%s:%s:%s\n", disk->name, file->sub, sub);
					fflush(stderr);
				}
				if (output) {
					printf("Move '%s%s' '%s%s'\n", disk->dir, file->sub, disk->dir, sub);
				}

				/* remove from the set */
				tommy_hashdyn_remove_existing(&disk->inodeset, &file->nodeset);

				/* save the new inode */
				file->inode = st->st_ino;

				/* reinsert in the set */
				tommy_hashdyn_insert(&disk->inodeset, &file->nodeset, file, file_inode_hash(file->inode));

				/* we have to save the new name */
				state->need_write = 1;
			} else {
				/* otherwise it's equal */
				++scan->count_equal;

				if (state->gui) {
					fprintf(stderr, "scan:equal:%s:%s\n", disk->name, file->sub);
					fflush(stderr);
				}
			}

			/* nothing more to do */
			return;
		} else {
			/* here if the file is changed */
		
			/* do a safety check to ensure that the common ext4 case of zeroing */
			/* the size of a file after a crash doesn't propagate to the backup */
			if (file->size != 0 && st->st_size == 0) {
				/* do the check ONLY if the name is the same */
				/* otherwise it could be a deleted and recreated file */
				if (strcmp(file->sub, sub) == 0) {
					if (!state->force_zero) {
						fprintf(stderr, "The file '%s%s' has now zero size!\n", disk->dir, sub);
						fprintf(stderr, "If you really want to sync, use 'snapraid --force-zero sync'\n");
						exit(EXIT_FAILURE);
					}
				}
			}

			if (strcmp(file->sub, sub) == 0) {
				/* if the name is the same, it's an update */
				if (state->gui) {
					fprintf(stderr, "scan:update:%s:%s\n", disk->name, file->sub);
					fflush(stderr);
				}
				if (output) {
					printf("Update '%s%s'\n", disk->dir, file->sub);
				}

				++scan->count_change;
			} else {
				/* if the name is different, it's an inode reuse */
				if (state->gui) {
					fprintf(stderr, "scan:remove:%s:%s\n", disk->name, file->sub);
					fprintf(stderr, "scan:add:%s:%s\n", disk->name, sub);
					fflush(stderr);
				}
				if (output) {
					printf("Remove '%s%s'\n", disk->dir, file->sub);
					printf("Add '%s%s'\n", disk->dir, sub);
				}

				++scan->count_remove;
				++scan->count_insert;
			}

			/* remove it */
			scan_file_remove(state, disk, file);

			/* and continue to reinsert it */
		}
	} else {
		/* create the new file */
		++scan->count_insert;

		if (state->gui) {
			fprintf(stderr, "scan:add:%s:%s\n", disk->name, sub);
			fflush(stderr);
		}
		if (output) {
			printf("Add '%s%s'\n", disk->dir, sub);
		}

		/* and continue to insert it */
	}

	/* insert it */
	file = file_alloc(state->block_size, sub, st->st_size, st->st_mtime, st->st_ino);

	/* mark it as present */
	file_flag_set(file, FILE_IS_PRESENT);

	/* insert it in the delayed insert list */
	tommy_list_insert_tail(&scan->file_insert_list, &file->nodelist, file);
}

/**
 * Removes the specified link from the data set.
 */
static void scan_link_remove(struct snapraid_state* state, struct snapraid_disk* disk, struct snapraid_link* link)
{
	/* state changed */
	state->need_write = 1;

	/* remove the file from the link containers */
	tommy_hashdyn_remove_existing(&disk->linkset, &link->nodeset);
	tommy_list_remove_existing(&disk->linklist, &link->nodelist);

	/* deallocate */
	link_free(link);
}

/**
 * Inserts the specified link in the data set.
 */
static void scan_link_insert(struct snapraid_state* state, struct snapraid_disk* disk, struct snapraid_link* link)
{
	/* state changed */
	state->need_write = 1;

	/* insert the link in the link containers */
	tommy_hashdyn_insert(&disk->linkset, &link->nodeset, link, link_name_hash(link->sub));
	tommy_list_insert_tail(&disk->linklist, &link->nodelist, link);
}

/**
 * Processes a symbolic link.
 */
static void scan_link(struct snapraid_scan* scan, struct snapraid_state* state, int output, struct snapraid_disk* disk, const char* sub, const char* linkto)
{
	struct snapraid_link* link;

	/* check if the link already exists */
	link = tommy_hashdyn_search(&disk->linkset, link_name_compare, sub, link_name_hash(sub));
	if (link) {
		/* check if multiple files have the same name */
		if (link_flag_has(link, FILE_IS_PRESENT)) {
			fprintf(stderr, "Internal inconsistency for symlink '%s%s'\n", disk->dir, sub);
			exit(EXIT_FAILURE);
		}

		/* mark as present */
		link_flag_set(link, FILE_IS_PRESENT);

		/* check if the link is not changed */
		if (strcmp(link->linkto, linkto) == 0) {
			/* it's equal */
			++scan->count_equal;

			if (state->gui) {
				fprintf(stderr, "scan:equal:%s:%s\n", disk->name, link->sub);
				fflush(stderr);
			}

			/* nothing more to do */
			return;
		} else {
			/* it's an update */
			if (state->gui) {
				fprintf(stderr, "scan:update:%s:%s\n", disk->name, link->sub);
				fflush(stderr);
			}
			if (output) {
				printf("Update '%s%s'\n", disk->dir, link->sub);
			}

			++scan->count_change;

			/* update it */
			pathcpy(link->linkto, sizeof(link->linkto), linkto);

			/* nothing more to do */
			return;
		}
	} else {
		/* create the new link */
		++scan->count_insert;

		if (state->gui) {
			fprintf(stderr, "scan:add:%s:%s\n", disk->name, sub);
			fflush(stderr);
		}
		if (output) {
			printf("Add '%s%s'\n", disk->dir, sub);
		}

		/* and continue to insert it */
	}

	/* insert it */
	link = link_alloc(sub, linkto);

	/* mark it as present */
	link_flag_set(link, FILE_IS_PRESENT);

	/* insert it in the delayed insert list */
	tommy_list_insert_tail(&scan->link_insert_list, &link->nodelist, link);
}

/**
 * Processes a directory.
 */
static void scan_dir(struct snapraid_scan* scan, struct snapraid_state* state, int output, struct snapraid_disk* disk, const char* dir, const char* sub)
{
	DIR* d;

	d = opendir(dir);
	if (!d) {
		fprintf(stderr, "Error opening directory '%s'. %s.\n", dir, strerror(errno));
		fprintf(stderr, "You can exclude it in the config file with:\n\texclude /%s/\n", sub);
		exit(EXIT_FAILURE);
	}
   
	while (1) { 
		char path_next[PATH_MAX];
		char sub_next[PATH_MAX];
		struct stat st;
		const char* name;
		struct dirent* dd;

		/* clear errno to detect errneous conditions */
		errno = 0;
		dd = readdir(d);
		if (dd == 0 && errno != 0) {
			fprintf(stderr, "Error reading directory '%s'. %s.\n", dir, strerror(errno));
			fprintf(stderr, "You can exclude it in the config file with:\n\texclude /%s/\n", sub);
			exit(EXIT_FAILURE);
		}
		if (dd == 0 && errno == 0) {
			break; /* finished */
		}

		/* skip "." and ".." files */
		name = dd->d_name;
		if (name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0)))
			continue;

		pathprint(path_next, sizeof(path_next), "%s%s", dir, name);
		pathprint(sub_next, sizeof(sub_next), "%s%s", sub, name);

		/* check for not supported file names, limitation derived from the content file format */
		if (name[0] == 0 || strchr(name, '\n') != 0 || name[strlen(name)-1] == '\r') {
			fprintf(stderr, "Unsupported name '%s' in file '%s'.\n", name, path_next);
			exit(EXIT_FAILURE);
		}

		/* get info about the file */
		if (lstat(path_next, &st) != 0) {
			fprintf(stderr, "Error in stat file/directory '%s'. %s.\n", path_next, strerror(errno));
			exit(EXIT_FAILURE);
		}

		if (S_ISREG(st.st_mode)) {
			if (filter_hidden(state->filter_hidden, dd, &st) == 0
				&& filter_content(&state->contentlist, path_next) == 0
				&& filter_path(&state->filterlist, sub_next) == 0
			) {
				/* check for read permission */
				if (access(path_next, R_OK) != 0) {
					fprintf(stderr, "warning: Ignoring, for missing read permission, file '%s'\n", path_next);
					continue;
				}

#if HAVE_LSTAT_EX
				/* get inode info about the file, Windows needs an additional step */
				if (lstat_ex(path_next, &st) != 0) {
					fprintf(stderr, "Error in stat_inode file '%s'. %s.\n", path_next, strerror(errno));
					exit(EXIT_FAILURE);
				}
#endif

				scan_file(scan, state, output, disk, sub_next, &st);
			} else {
				if (state->verbose) {
					printf("Excluding file '%s'\n", path_next);
				}
			}
		} else if (S_ISLNK(st.st_mode)) {
			if (filter_hidden(state->filter_hidden, dd, &st) == 0
				&& filter_path(&state->filterlist, sub_next) == 0
			) {
				char subnew[PATH_MAX];
				int ret;

				ret = readlink(path_next, subnew, sizeof(subnew));
				if (ret >= PATH_MAX) {
					fprintf(stderr, "Error in readlink file '%s'. Symlink too long.\n", path_next);
					exit(EXIT_FAILURE);
				}
				if (ret < 0) {
					fprintf(stderr, "Error in readlink file '%s'. %s.\n", path_next, strerror(errno));
					exit(EXIT_FAILURE);
				}

				/* readlink doesn't put the final 0 */
				subnew[ret] = 0;

				scan_link(scan, state, output, disk, sub_next, subnew);
			} else {
				if (state->verbose) {
					printf("Excluding file '%s'\n", path_next);
				}
			}
		} else if (S_ISDIR(st.st_mode)) {
			if (filter_hidden(state->filter_hidden, dd, &st) == 0
				&& filter_dir(&state->filterlist, sub_next) == 0
			) {
				pathslash(path_next, sizeof(path_next));
				pathslash(sub_next, sizeof(sub_next));
				scan_dir(scan, state, output, disk, path_next, sub_next);
			} else {
				if (state->verbose) {
					printf("Excluding directory '%s'\n", path_next);
				}
			}
		} else {
			if (filter_hidden(state->filter_hidden, dd, &st) == 0
				&& filter_content(&state->contentlist, path_next) == 0
				&& filter_path(&state->filterlist, sub_next) == 0
			) {
				fprintf(stderr, "warning: Ignoring special file '%s'\n", path_next);
			} else {
				if (state->verbose) {
					printf("Excluding special file '%s'\n", path_next);
				}
			}
		}
	}

	if (closedir(d) != 0) {
		fprintf(stderr, "Error closing directory '%s'. %s.\n", dir, strerror(errno));
		exit(EXIT_FAILURE);
	}
}

void state_scan(struct snapraid_state* state, int output)
{
	tommy_node* i;
	tommy_node* j;
	tommy_list scanlist;

	tommy_list_init(&scanlist);

	for(i=state->disklist;i!=0;i=i->next) {
		struct snapraid_disk* disk = i->data;
		struct snapraid_scan* scan;
		tommy_node* node;

		scan = malloc_nofail(sizeof(struct snapraid_scan));
		scan->count_equal = 0;
		scan->count_moved = 0;
		scan->count_change = 0;
		scan->count_remove = 0;
		scan->count_insert = 0;
		tommy_list_init(&scan->file_insert_list);
		tommy_list_init(&scan->link_insert_list);

		tommy_list_insert_tail(&scanlist, &scan->node, scan);

		printf("Scanning disk %s...\n", disk->name);

		scan_dir(scan, state, output, disk, disk->dir, "");

		/* check for removed files */
		node = disk->filelist;
		while (node) {
			struct snapraid_file* file = node->data;

			/* next node */
			node = node->next;

			/* remove if not present */
			if (!file_flag_has(file, FILE_IS_PRESENT)) {
				++scan->count_remove;

				if (state->gui) {
					fprintf(stderr, "scan:remove:%s:%s\n", disk->name, file->sub);
					fflush(stderr);
				}
				if (output) {
					printf("Remove '%s%s'\n", disk->dir, file->sub);
				}

				scan_file_remove(state, disk, file);
			}
		}

		/* check for removed links */
		node = disk->linklist;
		while (node) {
			struct snapraid_link* link = node->data;

			/* next node */
			node = node->next;

			/* remove if not present */
			if (!link_flag_has(link, FILE_IS_PRESENT)) {
				++scan->count_remove;

				if (state->gui) {
					fprintf(stderr, "scan:remove:%s:%s\n", disk->name, link->sub);
					fflush(stderr);
				}
				if (output) {
					printf("Remove '%s%s'\n", disk->dir, link->sub);
				}

				scan_link_remove(state, disk, link);
			}
		}

		/* insert all the new files, we insert them only after the deletion */
		/* to reuse the just freed space */
		node = scan->file_insert_list;
		while (node) {
			struct snapraid_file* file = node->data;

			/* next node */
			node = node->next;

			/* insert it */
			scan_file_insert(state, disk, file);
		}

		/* insert all the new links */
		node = scan->link_insert_list;
		while (node) {
			struct snapraid_link* link = node->data;

			/* next node */
			node = node->next;

			/* insert it */
			scan_link_insert(state, disk, link);
		}
	}

	/* checks for disks where all the previously existing files where removed */
	if (!state->force_empty) {
		int has_empty = 0;
		for(i=state->disklist,j=scanlist;i!=0;i=i->next,j=j->next) {
			struct snapraid_disk* disk = i->data;
			struct snapraid_scan* scan = j->data;

			if (scan->count_equal == 0 && scan->count_moved == 0 && scan->count_remove != 0) {
				if (!has_empty) {
					has_empty = 1;
					fprintf(stderr, "All the files previously present in disk '%s' at dir '%s'", disk->name, disk->dir);
				} else {
					fprintf(stderr, ", disk '%s' at dir '%s'", disk->name, disk->dir);
				}
			}
		}
		if (has_empty) {
			fprintf(stderr, " are now missing or rewritten!\n");
			fprintf(stderr, "This happens with an empty disk or when all the files are recreated after a 'fix' command.\n");
			fprintf(stderr, "If you really want to sync, use 'snapraid --force-empty sync'.\n");
			exit(EXIT_FAILURE);
		}
	}

	if (state->verbose || output) {
		struct snapraid_scan total;

		total.count_equal = 0;
		total.count_moved = 0;
		total.count_change = 0;
		total.count_remove = 0;
		total.count_insert = 0;

		for(i=scanlist;i!=0;i=i->next) {
			struct snapraid_scan* scan = i->data;
			total.count_equal += scan->count_equal;
			total.count_moved += scan->count_moved;
			total.count_change += scan->count_change;
			total.count_remove += scan->count_remove;
			total.count_insert += scan->count_insert;
		}

		if (state->verbose) {
			printf("\tequal %d\n", total.count_equal);
			printf("\tmoved %d\n", total.count_moved);
			printf("\tchanged %d\n", total.count_change);
			printf("\tremoved %d\n", total.count_remove);
			printf("\tadded %d\n", total.count_insert);
		}

		if (output) {
			if (!total.count_moved && !total.count_change && !total.count_remove && !total.count_insert) {
				printf("No difference.\n");
			}
		}
	}

	tommy_list_foreach(&scanlist, (tommy_foreach_func*)free);

	printf("Using %u MiB of memory.\n", (unsigned)(malloc_counter() / 1024 / 1024));
}

