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
#include "state.h"
#include "util.h"

void state_init(struct snapraid_state* state)
{
	state->verbose = 0;
	state->gui = 0;
	state->force_zero = 0;
	state->force_empty = 0;
	state->filter_hidden = 0;
	state->find_by_name = 0;
	state->autosave = 0;
	state->expect_unrecoverable = 0;
	state->expect_recoverable = 0;
	state->need_write = 0;
	state->block_size = 256 * 1024; /* default 256 KiB */
	state->parity[0] = 0;
	state->parity_device = 0;
	state->qarity[0] = 0;
	state->qarity_device = 0;
	state->level = 1; /* default is the lowest protection */
	state->hash = HASH_MURMUR3; /* default is the fastest */
	tommy_list_init(&state->disklist);
	tommy_list_init(&state->maplist);
	tommy_list_init(&state->contentlist);
	tommy_list_init(&state->filterlist);
	state->loaded_blockmax = 0;
}

void state_done(struct snapraid_state* state)
{
	tommy_list_foreach(&state->disklist, (tommy_foreach_func*)disk_free);
	tommy_list_foreach(&state->maplist, (tommy_foreach_func*)map_free);
	tommy_list_foreach(&state->contentlist, (tommy_foreach_func*)content_free);
	tommy_list_foreach(&state->filterlist, (tommy_foreach_func*)filter_free);
}

/**
 * Checks the configuration.
 */
static void state_config_check(struct snapraid_state* state, const char* path, int skip_device)
{
	tommy_node* i;

	if (state->parity[0] == 0) {
		fprintf(stderr, "No 'parity' specification in '%s'\n", path);
		exit(EXIT_FAILURE);
	}

	if (tommy_list_empty(&state->contentlist)) {
		fprintf(stderr, "No 'content' specification in '%s'\n", path);
		exit(EXIT_FAILURE);
	}

	/* checks for equal paths */
	for(i=state->contentlist;i!=0;i=i->next) {
		struct snapraid_content* content = i->data;

		if (pathcmp(state->parity, content->content) == 0) {
			fprintf(stderr, "Same path used for 'parity' and 'content' as '%s'\n", content->content);
			exit(EXIT_FAILURE);
		}

		if (state->qarity[0] != 0 && pathcmp(state->qarity, content->content) == 0) {
			fprintf(stderr, "Same path used for 'qarity' and 'content' as '%s'\n", content->content);
			exit(EXIT_FAILURE);
		}
	}

	/* check if all the data and parity disks are different */
	if (!skip_device) {
		unsigned diskcount = 0;

		for(i=state->disklist;i!=0;i=i->next) {
			tommy_node* j;
			struct snapraid_disk* disk = i->data;

#ifdef _WIN32
			if (disk->device == 0) {
				fprintf(stderr, "Disk '%s' has a zero serial number.\n", disk->dir);
				fprintf(stderr, "This is not necessarely wrong, but for using SnapRAID\n");
				fprintf(stderr, "it's better to change the serial number of the disk.\n");
				fprintf(stderr, "Try using the 'VolumeID' tool by 'Mark Russinovich'.\n");
				exit(EXIT_FAILURE);
			}
#endif

			for(j=i->next;j!=0;j=j->next) {
				struct snapraid_disk* other = j->data;
				if (disk->device == other->device) {
					fprintf(stderr, "Disks '%s' and '%s' are on the same device.\n", disk->dir, other->dir);
					exit(EXIT_FAILURE);
				}
			}

			if (disk->device == state->parity_device) {
				fprintf(stderr, "Disk '%s' and parity '%s' are on the same device.\n", disk->dir, state->parity);
				exit(EXIT_FAILURE);
			}

			if (state->qarity[0] != 0 && disk->device == state->qarity_device) {
				fprintf(stderr, "Disk '%s' and parity '%s' are on the same device.\n", disk->dir, state->qarity);
				exit(EXIT_FAILURE);
			}

			++diskcount;
		}

		if (diskcount > 255) {
			/* RAID6 P/Q parity works for up to 255 drives, no more */
			fprintf(stderr, "Too many disks. No more than 255.\n");
			exit(EXIT_FAILURE);
		}

#ifdef _WIN32
		if (state->parity_device == 0) {
			fprintf(stderr, "Disk '%s' has a zero serial number.\n", state->parity);
			fprintf(stderr, "This is not necessarely wrong, but for using SnapRAID\n");
			fprintf(stderr, "it's better to change the serial number of the disk.\n");
			fprintf(stderr, "Try using the 'VolumeID' tool by 'Mark Russinovich'.\n");
			exit(EXIT_FAILURE);
		}

		if (state->qarity[0] != 0 && state->qarity_device == 0) {
			fprintf(stderr, "Disk '%s' has a zero serial number.\n", state->qarity);
			fprintf(stderr, "This is not necessarely wrong, but for using SnapRAID\n");
			fprintf(stderr, "it's better to change the serial number of the disk.\n");
			fprintf(stderr, "Try using the 'VolumeID' tool by 'Mark Russinovich'.\n");
			exit(EXIT_FAILURE);
		}
#endif

		if (state->qarity[0] != 0 && state->parity_device == state->qarity_device) {
			fprintf(stderr, "Parity '%s' and '%s' are on the same device.\n", state->parity, state->qarity);
			exit(EXIT_FAILURE);
		}
	}

	/* count the content files */
	if (!skip_device) {
		unsigned content_count;

		content_count = 0;
		for(i=state->contentlist;i!=0;i=i->next) {
			tommy_node* j;
			struct snapraid_content* content = i->data;

			/* check if there are others in the same disk */
			for(j=i->next;j!=0;j=j->next) {
				struct snapraid_content* other = j->data;
				if (content->device == other->device) {
					break;
				}
			}
			if (j != 0) {
				/* skip it */
				continue;
			}

			++content_count;
		}

		if (content_count < state->level+1) {
			fprintf(stderr, "You must have at least %d 'content' files in different disks.\n", state->level+1);
			exit(EXIT_FAILURE);
		}
	}
	
}

void state_config(struct snapraid_state* state, const char* path, int verbose, int gui, int force_zero, int force_empty, int filter_hidden, int find_by_name, int expect_unrecoverable, int expect_recoverable, int skip_device)
{
	STREAM* f;
	unsigned line;

	state->verbose = verbose;
	state->gui = gui;
	state->force_zero = force_zero;
	state->force_empty = force_empty;
	state->filter_hidden = filter_hidden;
	state->find_by_name = find_by_name;
	state->expect_unrecoverable = expect_unrecoverable;
	state->expect_recoverable = expect_recoverable;

	if (state->gui) {
		fprintf(stderr, "version:%s\n", PACKAGE_VERSION);
		fprintf(stderr, "conf:%s\n", path);
		fflush(stderr);
	}

	f = sopen_read(path);
	if (!f) {
		if (errno == ENOENT) {
			fprintf(stderr, "No configuration file found at '%s'\n", path);
		} else if (errno == EACCES) {
			fprintf(stderr, "You do not have rights to access the configuration file '%s'\n", path);
		} else {
			fprintf(stderr, "Error opening the configuration file '%s'. %s.\n", path, strerror(errno));
		}
		exit(EXIT_FAILURE);
	}

	line = 1;
	while (1) {
		char tag[PATH_MAX];
		char buffer[PATH_MAX];
		int ret;
		int c;

		/* skip initial spaces */
		sgetspace(f);

		/* read the command */
		ret = sgettok(f, tag, sizeof(tag));
		if (ret < 0) {
			fprintf(stderr, "Error reading the configuration file '%s' at line %u\n", path, line);
			exit(EXIT_FAILURE);
		}

		/* skip spaces after the command */
		sgetspace(f);

		if (strcmp(tag, "block_size") == 0) {
			ret = sgetu32(f, &state->block_size);
			if (ret < 0) {
				fprintf(stderr, "Invalid 'block_size' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}
			if (state->block_size < 1) {
				fprintf(stderr, "Too small 'block_size' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}
			if (state->block_size > 16*1024) {
				fprintf(stderr, "Too big 'block_size' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}
			/* check if it's a power of 2 */
			if ((state->block_size & (state->block_size - 1)) != 0) {
				fprintf(stderr, "Not power of 2 'block_size' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}
			state->block_size *= 1024;
		} else if (strcmp(tag, "parity") == 0) {
			char device[PATH_MAX];
			char* slash;
			struct stat st;

			if (*state->parity) {
				fprintf(stderr, "Multiple 'parity' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}

			ret = sgetlasttok(f, buffer, sizeof(buffer));
			if (ret < 0) {
				fprintf(stderr, "Invalid 'parity' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}

			pathimport(state->parity, sizeof(state->parity), buffer);

			/* get the device of the directory containing the content file */
			pathimport(device, sizeof(device), buffer);
			slash = strrchr(device, '/');
			if (slash)
				*slash = 0;
			else
				pathcpy(device, sizeof(device), ".");
			if (stat(device, &st) != 0) {
				fprintf(stderr, "Error accessing 'parity' dir '%s' specification in '%s' at line %u\n", device, path, line);
				exit(EXIT_FAILURE);
			}

			state->parity_device = st.st_dev;
		} else if (strcmp(tag, "q-parity") == 0) {
			char device[PATH_MAX];
			char* slash;
			struct stat st;

			if (*state->qarity) {
				fprintf(stderr, "Multiple 'q-parity' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}

			ret = sgetlasttok(f, buffer, sizeof(buffer));
			if (ret < 0) {
				fprintf(stderr, "Invalid 'q-parity' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}

			pathimport(state->qarity, sizeof(state->qarity), buffer);

			/* get the device of the directory containing the content file */
			pathimport(device, sizeof(device), buffer);
			slash = strrchr(device, '/');
			if (slash)
				*slash = 0;
			else
				pathcpy(device, sizeof(device), ".");
			if (stat(device, &st) != 0) {
				fprintf(stderr, "Error accessing 'qarity' dir '%s' specification in '%s' at line %u\n", device, path, line);
				exit(EXIT_FAILURE);
			}

			state->qarity_device = st.st_dev;

			/* we have two level of parity */
			state->level = 2;
		} else if (strcmp(tag, "content") == 0) {
			struct snapraid_content* content;
			char device[PATH_MAX];
			char* slash;
			struct stat st;

			ret = sgetlasttok(f, buffer, sizeof(buffer));
			if (ret < 0) {
				fprintf(stderr, "Invalid 'content' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}

			if (pathcmp(buffer, "/dev/null") == 0 || pathcmp(buffer, "NUL") == 0) {
				fprintf(stderr, "You cannot use the null device as 'content' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}

			/* get the device of the directory containing the content file */
			pathimport(device, sizeof(device), buffer);
			slash = strrchr(device, '/');
			if (slash)
				*slash = 0;
			else
				pathcpy(device, sizeof(device), ".");
			if (stat(device, &st) != 0) {
				fprintf(stderr, "Error accessing 'content' dir '%s' specification in '%s' at line %u\n", device, path, line);
				exit(EXIT_FAILURE);
			}

			content = content_alloc(buffer, st.st_dev);

			tommy_list_insert_tail(&state->contentlist, &content->node, content);
		} else if (strcmp(tag, "disk") == 0) {
			char dir[PATH_MAX];
			char device[PATH_MAX];
			struct stat st;
			struct snapraid_disk* disk;

			ret = sgettok(f, buffer, sizeof(buffer));
			if (ret < 0) {
				fprintf(stderr, "Invalid 'disk' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}

			sgetspace(f);

			ret = sgetlasttok(f, dir, sizeof(dir));
			if (ret < 0) {
				fprintf(stderr, "Invalid 'disk' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}

			/* get the device of the dir */
			pathimport(device, sizeof(device), dir);
			if (stat(device, &st) != 0) {
				fprintf(stderr, "Error accessing 'disk' '%s' specification in '%s' at line %u\n", dir, device, line);
				exit(EXIT_FAILURE);
			}

			disk = disk_alloc(buffer, dir, st.st_dev);

			tommy_list_insert_tail(&state->disklist, &disk->node, disk);
		} else if (strcmp(tag, "nohidden") == 0) {
			state->filter_hidden = 1;
		} else if (strcmp(tag, "exclude") == 0) {
			struct snapraid_filter* filter;

			ret = sgetlasttok(f, buffer, sizeof(buffer));
			if (ret < 0) {
				fprintf(stderr, "Invalid 'exclude' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}

			filter = filter_alloc(-1, buffer);
			if (!filter) {
				fprintf(stderr, "Invalid 'exclude' specification '%s' in '%s' at line %u\n", buffer, path, line);
				exit(EXIT_FAILURE);
			}
			tommy_list_insert_tail(&state->filterlist, &filter->node, filter);
		} else if (strcmp(tag, "include") == 0) {
			struct snapraid_filter* filter;

			ret = sgetlasttok(f, buffer, sizeof(buffer));
			if (ret < 0) {
				fprintf(stderr, "Invalid 'include' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}

			filter = filter_alloc(1, buffer);
			if (!filter) {
				fprintf(stderr, "Invalid 'include' specification '%s' in '%s' at line %u\n", buffer, path, line);
				exit(EXIT_FAILURE);
			}
			tommy_list_insert_tail(&state->filterlist, &filter->node, filter);
		} else if (strcmp(tag, "autosave") == 0) {
			char* e;

			ret = sgetlasttok(f, buffer, sizeof(buffer));
			if (ret < 0) {
				fprintf(stderr, "Invalid 'autosave' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}

			state->autosave = strtoul(buffer, &e, 0);

			if (!e || *e) {
				fprintf(stderr, "Invalid 'autosave' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}

			/* convert to GiB */
			state->autosave *= 1024 * 1024 * 1024;
		} else if (tag[0] == 0) {
			/* allow empty lines */
		} else if (tag[0] == '#') {
			ret = sgetline(f, buffer, sizeof(buffer));
			if (ret < 0) {
				fprintf(stderr, "Invalid comment in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}
		} else {
			fprintf(stderr, "Invalid command '%s' in '%s' at line %u\n", tag, path, line);
			exit(EXIT_FAILURE);
		}

		/* skip final spaces */
		sgetspace(f);

		/* next line */
		c = sgeteol(f);
		if (c == EOF) {
			break;
		}
		if (c != '\n') {
			fprintf(stderr, "Extra data in '%s' at line %u\n", path, line);
			exit(EXIT_FAILURE);
		}
		++line;
	}

	if (serror(f)) {
		fprintf(stderr, "Error reading the configuration file '%s' at line %u\n", path, line);
		exit(EXIT_FAILURE);
	}

	sclose(f);

	state_config_check(state, path, skip_device);

	if (state->gui) {
		tommy_node* i;
		fprintf(stderr, "blocksize:%u\n", state->block_size);
		for(i=state->disklist;i!=0;i=i->next) {
			struct snapraid_disk* disk = i->data;
			fprintf(stderr, "disk:%s:%s\n", disk->name, disk->dir);
		}
		if (state->qarity[0] != 0)
			fprintf(stderr, "mode:raid6\n");
		else
			fprintf(stderr, "mode:raid5\n");
		fprintf(stderr, "parity:%s\n", state->parity);
		if (state->qarity[0] != 0)
			fprintf(stderr, "qarity:%s\n", state->qarity);
		fflush(stderr);
	}
}

/**
 * Updates the disk mapping if required.
 */
static void state_map(struct snapraid_state* state)
{
	unsigned hole;
	tommy_node* i;

	/* removes all the mapping without a disk */
	/* this happens when a disk is removed from the configuration file */
	for(i=state->maplist;i!=0;) {
		struct snapraid_map* map = i->data;
		struct snapraid_disk* disk;
		tommy_node* j;

		for(j=state->disklist;j!=0;j=j->next) {
			disk = j->data;
			if (strcmp(disk->name, map->name) == 0) {
				/* disk found */
				break;
			}
		}

		/* go to the next mapping before removing */
		i = i->next;
		
		if (j==0) {
			/* disk not found, remove the mapping */
			tommy_list_remove_existing(&state->maplist, &map->node);
			map_free(map);
		}
	}

	/* maps each unmapped disk present in the configuration file in the first available hole */
	/* this happens when you add disks for the first time in the configuration file */
	hole = 0; /* first position to try */
	for(i=state->disklist;i!=0;i=i->next) {
		struct snapraid_disk* disk = i->data;
		struct snapraid_map* map;
		tommy_node* j;

		/* check if the disk is already mapped */
		for(j=state->maplist;j!=0;j=j->next) {
			map = j->data;
			if (strcmp(disk->name, map->name) == 0) {
				/* mapping found */
				break;
			}
		}
		if (j!=0)
			continue;

		/* mapping not found, search for an hole */
		while (1) {
			for(j=state->maplist;j!=0;j=j->next) {
				map = j->data;
				if (map->position == hole) {
					/* position already used */
					break;
				}
			}
			if (j==0) {
				/* hole found */
				break;
			}

			/* try with the next one */
			++hole;
		}

		/* insert the new mapping */
		map = map_alloc(disk->name, hole);

		tommy_list_insert_tail(&state->maplist, &map->node, map);
	}
}

/**
 * Checks the content.
 */
static void state_content_check(struct snapraid_state* state, const char* path)
{
	tommy_node* i;
	block_off_t j;

	/* checks that any map has different name and position */
	for(i=state->maplist;i!=0;i=i->next) {
		struct snapraid_map* map = i->data;
		tommy_node* j;
		for(j=i->next;j!=0;j=j->next) {
			struct snapraid_map* other = j->data;
			if (strcmp(map->name, other->name) == 0) {
				fprintf(stderr, "Colliding 'map' disk specification in '%s'\n", path);
				exit(EXIT_FAILURE);
			}
			if (map->position == other->position) {
				fprintf(stderr, "Colliding 'map' index specification in '%s'\n", path);
				exit(EXIT_FAILURE);
			}
		}
	}

	/* check the parity validity */
	for(j=0;1;++j) { /* for all the blocks */
		int at_least_one = 0;
		int parity_is_valid = 0;
		int parity_is_invalid = 0;

		/* for all the disks */
		for(i=state->disklist;i!=0;i=i->next) {
			struct snapraid_disk* disk = i->data;

			/* if the block is present */
			block_off_t size = tommy_array_size(&disk->blockarr);
			if (j < size) {
				struct snapraid_block* block = tommy_array_get(&disk->blockarr, j);

				/* mark if at least one block is addressed */
				at_least_one = 1;

				if (block == BLOCK_EMPTY) {
					/* not relevant */
				} else if (block == BLOCK_DELETED) {
					parity_is_invalid = 1;
				} else {
					unsigned state = block_state_get(block);
					if (state == BLOCK_STATE_BLK) {
						parity_is_valid = 1;
					} else {
						parity_is_invalid = 1;
					}
				}
			}
		}

		/* we cannot have different blocks reporting both valid and invalid parity */
		if (parity_is_valid && parity_is_invalid) {
			fprintf(stderr, "Internal inconsistency in parity validity at block %u\n", j);
			exit(EXIT_FAILURE);
		}

		/* stop if reached the end of all the arrays */
		if (!at_least_one)
			break;
	}
}

void state_read(struct snapraid_state* state)
{
	STREAM* f;
	char path[PATH_MAX];
	struct snapraid_disk* disk;
	struct snapraid_file* file;
	block_off_t blockidx;
	block_off_t blockmax;
	unsigned line;
	unsigned count_file;
	unsigned count_block;
	unsigned count_link;
	tommy_node* node;
	oathash_t hash;

	count_file = 0;
	count_block = 0;
	count_link = 0;
	hash = 0;

	/* iterate over all the available content files and load the first one present */
	f = 0;
	node = tommy_list_head(&state->contentlist);
	while (node) {
		struct snapraid_content* content = node->data;
		pathcpy(path, sizeof(path), content->content);

		if (state->gui) {
			fprintf(stderr, "content:%s\n", path);
			fflush(stderr);
		}
		printf("Loading state from %s...\n", path);

		f = sopen_read(path);
		if (f != 0) {
			/* if openend stop the search */
			break;
		} else {
			/* if it's real error of an existing file, abort */
			if (errno != ENOENT) {
				fprintf(stderr, "Error opening the content file '%s'. %s.\n", path, strerror(errno));
				exit(EXIT_FAILURE);
			}

			/* otherwise continue */
			if (node->next) {
				fprintf(stderr, "Not found, trying with another copy...\n");
			}
		}

		node = node->next;
	}

	/* if not found, assume empty */
	if (!f) {
		/* create the initial mapping */
		state_map(state);

		fprintf(stderr, "No content file found. Assuming empty.\n");
		return;
	}

	/* start with a MD5 default. */
	/* it's for compatibility with version 1.0 where MD5 was implicit. */
	state->hash = HASH_MD5;

	disk = 0;
	file = 0;
	line = 1;
	blockidx = 0;
	blockmax = 0;

	while (1) {
		char buffer[PATH_MAX];
		char tag[PATH_MAX];
		int ret;
		int c;

		/* read the command */
		ret = sgettok(f, tag, sizeof(tag));
		if (ret < 0) {
			fprintf(stderr, "Error reading the configuration file '%s' at line %u\n", path, line);
			exit(EXIT_FAILURE);
		}

		/* skip only one space if present */
		c = sgetc(f);
		if (c != ' ') {
			sungetc(c, f);
		}

		if (strcmp(tag, "blk") == 0 || strcmp(tag, "inv") == 0 || strcmp(tag, "new") == 0 || strcmp(tag, "chg") == 0) {
			/* "blk"/"inv"/"new"/"chg" command */
			block_off_t v_pos;
			struct snapraid_block* block;

			if (!file) {
				fprintf(stderr, "Unexpected 'blk' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}

			ret = sgetu32(f, &v_pos);
			if (ret < 0) {
				fprintf(stderr, "Invalid 'blk' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}

			if (blockidx >= file->blockmax) {
				fprintf(stderr, "Internal inconsistency in 'blk' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}

			/* keep track of the max block number */
			if (blockidx > blockmax)
				blockmax = blockidx;

			block = &file->blockvec[blockidx];

			if (block->parity_pos != POS_INVALID) {
				fprintf(stderr, "Internal inconsistency in 'blk' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}

			/* set the parity only if present */
			switch (tag[0]) {
			case 'b' :
				block_state_set(block, BLOCK_STATE_BLK);
				hash = oathash8(hash, 'b');
				break;
			case 'i' :
				block_state_set(block, BLOCK_STATE_INV);
				hash = oathash8(hash, 'i');
				break;
			case 'n' :
				block_state_set(block, BLOCK_STATE_NEW);
				hash = oathash8(hash, 'n');
				break;
			case 'c' :
				block_state_set(block, BLOCK_STATE_CHG);
				hash = oathash8(hash, 'g');
				break;
			}

			block->parity_pos = v_pos;
			hash = oathash32(hash, v_pos);

			/* read the hash only for some state */
			if (tag[0] == 'b' || tag[0] == 'i') {
				c = sgeteol(f);
				if (c != ' ') {
					fprintf(stderr, "Invalid 'blk' specification in '%s' at line %u\n", path, line);
					exit(EXIT_FAILURE);
				}

				/* set the hash only if present */
				ret = sgethex(f, block->hash, HASH_SIZE);
				if (ret < 0) {
					fprintf(stderr, "Invalid 'blk' specification in '%s' at line %u\n", path, line);
					exit(EXIT_FAILURE);
				}
				hash = oathashm(hash, block->hash, HASH_SIZE);
			}

			/* insert the block in the block array */
			tommy_array_grow(&disk->blockarr, block->parity_pos + 1);
			tommy_array_set(&disk->blockarr, block->parity_pos, block);

			/* check for termination of the block list */
			++blockidx;
			if (blockidx == file->blockmax) {
				file = 0;
				disk = 0;
			}

			/* stat */
			++count_block;
		} else if (strcmp(tag, "off") == 0) {
			/* "off" command */
			block_off_t v_pos;

			hash = oathash8(hash, 'o');

			if (!disk) {
				fprintf(stderr, "Unexpected 'off' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}

			ret = sgetu32(f, &v_pos);
			if (ret < 0) {
				fprintf(stderr, "Invalid 'off' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}

			hash = oathash32(hash, v_pos);

			/* insert the block in the block array */
			tommy_array_grow(&disk->blockarr, v_pos + 1);

			/* deleted block must be empty blocks */
			if (tommy_array_get(&disk->blockarr, v_pos) != 0) {
				fprintf(stderr, "Internal inconsistency for 'off' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}

			tommy_array_set(&disk->blockarr, v_pos, BLOCK_DELETED);
		} else if (strcmp(tag, "file") == 0) {
			/* file */
			char sub[PATH_MAX];
			uint64_t v_size;
			uint64_t v_mtime_sec;
			uint32_t v_mtime_nsec;
			uint64_t v_inode;
			tommy_node* i;

			hash = oathash8(hash, 'f');

			if (file) {
				fprintf(stderr, "Missing 'blk' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}

			ret = sgettok(f, buffer, sizeof(buffer));
			if (ret < 0) {
				fprintf(stderr, "Invalid 'file' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}
			hash = oathashs(hash, buffer);

			c = sgetc(f);
			if (c != ' ') {
				fprintf(stderr, "Invalid 'file' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}

			ret = sgetu64(f, &v_size);
			if (ret < 0) {
				fprintf(stderr, "Invalid 'file' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}
			hash = oathash64(hash, v_size);

			c = sgetc(f);
			if (c != ' ') {
				fprintf(stderr, "Invalid 'file' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}

			ret = sgetu64(f, &v_mtime_sec);
			if (ret < 0) {
				fprintf(stderr, "Invalid 'file' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}
			hash = oathash64(hash, v_mtime_sec);

			c = sgetc(f);
			if (c == '.') { /* the nanosecond field is present only from version 1.12 */
				ret = sgetu32(f, &v_mtime_nsec);
				if (ret < 0) {
					fprintf(stderr, "Invalid 'file' specification in '%s' at line %u\n", path, line);
					exit(EXIT_FAILURE);
				}
				hash = oathash32(hash, v_mtime_nsec);

				c = sgetc(f);
			} else {
				/* use a special value, meaning that we don't have this information */
				v_mtime_nsec = FILE_MTIME_NSEC_INVALID;
			}

			if (c != ' ') {
				fprintf(stderr, "Invalid 'file' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}

			ret = sgetu64(f, &v_inode);
			if (ret < 0) {
				fprintf(stderr, "Invalid 'file' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}
			hash = oathash64(hash, v_inode);

			c = sgetc(f);
			if (c != ' ') {
				fprintf(stderr, "Invalid 'file' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}

			ret = sgetline(f, sub, sizeof(sub));
			if (ret < 0) {
				fprintf(stderr, "Invalid 'file' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}

			if (!*sub) {
				fprintf(stderr, "Invalid 'file' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}
			hash = oathashs(hash, sub);

			/* find the disk */
			for(i=state->disklist;i!=0;i=i->next) {
				disk = i->data;
				if (strcmp(disk->name, buffer) == 0)
					break;
			}
			if (!i) {
				fprintf(stderr, "Disk named '%s' not found in '%s' at line %u\n", buffer, path, line);
				exit(EXIT_FAILURE);
			}

			/* allocate the file */
			file = file_alloc(state->block_size, sub, v_size, v_mtime_sec, v_mtime_nsec, v_inode);

			/* insert the file in the file containers */
			tommy_hashdyn_insert(&disk->inodeset, &file->nodeset, file, file_inode_hash(file->inode));
			tommy_hashdyn_insert(&disk->pathset, &file->pathset, file, file_path_hash(file->sub));
			tommy_list_insert_tail(&disk->filelist, &file->nodelist, file);

			/* start the block allocation of the file */
			blockidx = 0;

			/* check for empty file */
			if (blockidx == file->blockmax) {
				file = 0;
				disk = 0;
			}

			/* stat */
			++count_file;
		} else if (strcmp(tag, "hole") == 0) {
			/* hole */
			tommy_node* i;

			hash = oathash8(hash, 'h');

			if (file) {
				fprintf(stderr, "Missing 'blk' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}

			ret = sgettok(f, buffer, sizeof(buffer));
			if (ret < 0) {
				fprintf(stderr, "Invalid 'hole' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}
			hash = oathashs(hash, buffer);

			/* find the disk */
			for(i=state->disklist;i!=0;i=i->next) {
				disk = i->data;
				if (strcmp(disk->name, buffer) == 0)
					break;
			}
			if (!i) {
				fprintf(stderr, "Disk named '%s' not found in '%s' at line %u\n", buffer, path, line);
				exit(EXIT_FAILURE);
			}
		} else if (strcmp(tag, "symlink") == 0) {
			/* symlink */
			char sub[PATH_MAX];
			char linkto[PATH_MAX];
			char tokento[32];
			tommy_node* i;
			struct snapraid_link* link;

			hash = oathash8(hash, 's');

			ret = sgettok(f, buffer, sizeof(buffer));
			if (ret < 0) {
				fprintf(stderr, "Invalid 'symlink' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}
			hash = oathashs(hash, buffer);

			c = sgetc(f);
			if (c != ' ') {
				fprintf(stderr, "Invalid 'symlink' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}

			ret = sgetline(f, sub, sizeof(sub));
			if (ret < 0) {
				fprintf(stderr, "Invalid 'symlink' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}
			hash = oathashs(hash, sub);

			c = sgeteol(f);
			if (c != '\n') {
				fprintf(stderr, "Invalid 'symlink' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}
			++line;

			ret = sgettok(f, tokento, sizeof(tokento));
			if (ret < 0) {
				fprintf(stderr, "Invalid 'symlink' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}
			if (strcmp(tokento, "to") != 0) {
				fprintf(stderr, "Invalid 'symlink' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}

			c = sgetc(f);
			if (c != ' ') {
				fprintf(stderr, "Invalid 'symlink' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}

			ret = sgetline(f, linkto, sizeof(linkto));
			if (ret < 0) {
				fprintf(stderr, "Invalid 'symlink' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}
			hash = oathashs(hash, linkto);

			if (!*sub || !*linkto) {
				fprintf(stderr, "Invalid 'symlink' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}

			/* find the disk */
			for(i=state->disklist;i!=0;i=i->next) {
				disk = i->data;
				if (strcmp(disk->name, buffer) == 0)
					break;
			}
			if (!i) {
				fprintf(stderr, "Disk named '%s' not found in '%s' at line %u\n", buffer, path, line);
				exit(EXIT_FAILURE);
			}

			/* allocate the link */
			link = link_alloc(sub, linkto);

			/* insert the link in the link containers */
			tommy_hashdyn_insert(&disk->linkset, &link->nodeset, link, link_name_hash(link->sub));
			tommy_list_insert_tail(&disk->linklist, &link->nodelist, link);

			/* stat */
			++count_link;
		} else if (strcmp(tag, "checksum") == 0) {
			hash = oathash8(hash, 'c');

			ret = sgettok(f, buffer, sizeof(buffer));
			if (ret < 0) {
				fprintf(stderr, "Invalid 'checksum' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}

			if (strcmp(buffer, "md5") == 0) {
				state->hash = HASH_MD5;
				hash = oathash8(hash, 'm');
			} else if (strcmp(buffer, "murmur3") == 0) {
				state->hash = HASH_MURMUR3;
				hash = oathash8(hash, 'u');
			} else {
				fprintf(stderr, "Invalid 'checksum' specification '%s' in '%s' at line %u\n", buffer, path, line);
				exit(EXIT_FAILURE);
			}
		} else if (strcmp(tag, "blksize") == 0) {
			block_off_t blksize;

			hash = oathash8(hash, 'z');

			ret = sgetu32(f, &blksize);
			if (ret < 0) {
				fprintf(stderr, "Invalid 'blksize' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}
			hash = oathash32(hash, blksize);

			if (blksize != state->block_size) {
				fprintf(stderr, "Mismatching 'blksize' and 'block_size' specification in '%s' at line %u\n", path, line);
				fprintf(stderr, "Please restore the 'block_size' value in the configuration file to '%u'\n", blksize / 1024);
				exit(EXIT_FAILURE);
			}
		} else if (strcmp(tag, "map") == 0) {
			struct snapraid_map* map;
			uint32_t v_pos;

			hash = oathash8(hash, 'm');

			ret = sgettok(f, buffer, sizeof(buffer));
			if (ret < 0) {
				fprintf(stderr, "Invalid 'map' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}
			hash = oathashs(hash, buffer);

			c = sgetc(f);
			if (c != ' ') {
				fprintf(stderr, "Invalid 'map' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}

			ret = sgetu32(f, &v_pos);
			if (ret < 0) {
				fprintf(stderr, "Invalid 'map' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}
			hash = oathash32(hash, v_pos);

			map = map_alloc(buffer, v_pos);

			tommy_list_insert_tail(&state->maplist, &map->node, map);
		} else if (strcmp(tag, "sign") == 0) {
			oathash_t sign;

			ret = sgetu32(f, &sign);
			if (ret < 0) {
				fprintf(stderr, "Invalid 'sign' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}

			if (sign != hash) {
				fprintf(stderr, "Mismatching 'sign' in '%s' at line %u\n", path, line);
				fprintf(stderr, "Likely this content file is damaged. Use an alternate copy\n");
				exit(EXIT_FAILURE);
			}
		} else if (tag[0] == 0) {
			/* allow empty lines */
			sgetspace(f);
		} else if (tag[0] == '#') {
			ret = sgetline(f, buffer, sizeof(buffer));
			if (ret < 0) {
				fprintf(stderr, "Invalid comment in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}
		} else {
			fprintf(stderr, "Invalid command '%s' in '%s' at line %u\n", tag, path, line);
			exit(EXIT_FAILURE);
		}

		/* next line */
		c = sgeteol(f);
		if (c == EOF) {
			break;
		}
		if (c != '\n') {
			fprintf(stderr, "Extra data in '%s' at line %u\n", path, line);
			exit(EXIT_FAILURE);
		}
		++line;
	}

	if (serror(f)) {
		fprintf(stderr, "Error reading the content file '%s' at line %u\n", path, line);
		exit(EXIT_FAILURE);
	}

	if (file) {
		fprintf(stderr, "Missing 'blk' specification in '%s' at line %u\n", path, line);
		exit(EXIT_FAILURE);
	}

	sclose(f);

	state->loaded_blockmax = blockmax;

	/* update the mapping */
	state_map(state);

	state_content_check(state, path);

	if (state->verbose) {
		printf("\tfile %u\n", count_file);
		printf("\tblock %u\n", count_block);
		printf("\tsymlink %u\n", count_link);
	}
}

void state_write(struct snapraid_state* state)
{
	STREAM* f;
	unsigned count_file;
	unsigned count_block;
	unsigned count_link;
	unsigned count_content;
	tommy_node* i;
	unsigned k;
	oathash_t hash;

	count_file = 0;
	count_block = 0;
	count_link = 0;
	hash = 0;

	/* count the content files */
	count_content = 0;
	i = tommy_list_head(&state->contentlist);
	while (i) {
		struct snapraid_content* content = i->data;
		printf("Saving state to %s...\n", content->content);
		++count_content;
		i = i->next;
	}

	/* open all the content files */
	f = sopen_multi_write(count_content);
	if (!f) {
		fprintf(stderr, "Error opening the content files.\n");
		exit(EXIT_FAILURE);
	}

	k = 0;
	i = tommy_list_head(&state->contentlist);
	while (i) {
		struct snapraid_content* content = i->data;
		char tmp[PATH_MAX];
		pathprint(tmp, sizeof(tmp), "%s.tmp", content->content);
		if (sopen_multi_file(f, k, tmp) != 0) {
			fprintf(stderr, "Error opening the content file '%s'. %s.\n", tmp, strerror(errno));
			exit(EXIT_FAILURE);
		}
		++k;
		i = i->next;
	}

	sputsl("blksize ", f);
	hash = oathash8(hash, 'z');
	sputu32(state->block_size, f);
	hash = oathash32(hash, state->block_size);
	sputeol(f);
	if (serror(f)) {
		fprintf(stderr, "Error writing the content file '%s'. %s.\n", serrorfile(f), strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (state->hash == HASH_MD5) {
		sputsl("checksum md5", f);
		hash = oathash8(hash, 'c');
		hash = oathash8(hash, 'm');
	} else {
		sputsl("checksum murmur3", f);
		hash = oathash8(hash, 'c');
		hash = oathash8(hash, 'u');
	}
	sputeol(f);
	if (serror(f)) {
		fprintf(stderr, "Error writing the content file '%s'. %s.\n", serrorfile(f), strerror(errno));
		exit(EXIT_FAILURE);
	}

	/* for each map */
	for(i=state->maplist;i!=0;i=i->next) {
		struct snapraid_map* map = i->data;
		sputsl("map ", f);
		hash = oathash8(hash, 'm');
		sputs(map->name, f);
		hash = oathashs(hash, map->name);
		sputc(' ', f);
		sputu32(map->position, f);
		hash = oathash32(hash, map->position);
		sputeol(f);
		if (serror(f)) {
			fprintf(stderr, "Error writing the content file '%s'. %s.\n", serrorfile(f), strerror(errno));
			exit(EXIT_FAILURE);
		}
	}

	/* for each disk */
	for(i=state->disklist;i!=0;i=i->next) {
		tommy_node* j;
		struct snapraid_disk* disk = i->data;

		/* for each file */
		for(j=disk->filelist;j!=0;j=j->next) {
			struct snapraid_file* file = j->data;
			block_off_t k;
			uint64_t size;
			uint64_t mtime_sec;
			int32_t mtime_nsec;
			uint64_t inode;

			size = file->size;
			mtime_sec = file->mtime_sec;
			mtime_nsec = file->mtime_nsec;
			inode = file->inode;

			sputsl("file ", f);
			hash = oathash8(hash, 'f');
			sputs(disk->name, f);
			hash = oathashs(hash, disk->name);
			sputc(' ', f);
			sputu64(size, f);
			hash = oathash64(hash, size);
			sputc(' ', f);
			sputu64(mtime_sec, f);
			hash = oathash64(hash, mtime_sec);
			if (mtime_nsec != FILE_MTIME_NSEC_INVALID) {
				sputc('.', f);
				sputu32(mtime_nsec, f);
				hash = oathash32(hash, mtime_nsec);
			}
			sputc(' ', f);
			sputu64(inode, f);
			hash = oathash64(hash, inode);
			sputc(' ', f);
			sputs(file->sub, f);
			hash = oathashs(hash, file->sub);
			sputeol(f);
			if (serror(f)) {
				fprintf(stderr, "Error writing the content file '%s'. %s.\n", serrorfile(f), strerror(errno));
				exit(EXIT_FAILURE);
			}

			/* for each block in the file */
			for(k=0;k<file->blockmax;++k) {
				struct snapraid_block* block = &file->blockvec[k];
				unsigned block_state;

				block_state = block_state_get(block);
				switch (block_state) {
				case BLOCK_STATE_BLK :
					sputsl("blk ", f);
					hash = oathash8(hash, 'b');
					break;
				case BLOCK_STATE_INV :
					sputsl("inv ", f);
					hash = oathash8(hash, 'i');
					break;
				case BLOCK_STATE_NEW :
					sputsl("new ", f);
					hash = oathash8(hash, 'n');
					break;
				case BLOCK_STATE_CHG :
					sputsl("chg ", f);
					hash = oathash8(hash, 'g');
					break;
				}

				sputu32(block->parity_pos, f);
				hash = oathash32(hash, block->parity_pos);

				if (block_state == BLOCK_STATE_BLK || block_state == BLOCK_STATE_INV) {
					sputc(' ', f);
					sputhex(block->hash, HASH_SIZE, f);
					hash = oathashm(hash, block->hash, HASH_SIZE);
				}

				sputeol(f);
				if (serror(f)) {
					fprintf(stderr, "Error writing the content file '%s'. %s.\n", serrorfile(f), strerror(errno));
					exit(EXIT_FAILURE);
				}

				++count_block;
			}

			++count_file;
		}

		/* for each link */
		for(j=disk->linklist;j!=0;j=j->next) {
			struct snapraid_link* link = j->data;

			sputsl("symlink ", f);
			hash = oathash8(hash, 's');
			sputs(disk->name, f);
			hash = oathashs(hash, disk->name);
			sputc(' ', f);
			sputs(link->sub, f);
			hash = oathashs(hash, link->sub);
			sputeol(f);
			sputsl("to ", f);
			sputs(link->linkto, f);
			hash = oathashs(hash, link->linkto);
			sputeol(f);
			if (serror(f)) {
				fprintf(stderr, "Error writing the content file '%s'. %s.\n", serrorfile(f), strerror(errno));
				exit(EXIT_FAILURE);
			}

			++count_link;
		}

		{
			block_off_t k;
			block_off_t blockmax;
			int first_deleted;
			
			first_deleted = 1;
			blockmax = tommy_array_size(&disk->blockarr);

			/* for each deleted block in the disk */
			for(k=0;k<blockmax;++k) {
				struct snapraid_block* block = tommy_array_get(&disk->blockarr, k);
				if (block == BLOCK_DELETED) {
					if (first_deleted) {
						first_deleted = 0;

						sputsl("hole ", f);
						hash = oathash8(hash, 'h');
						sputs(disk->name, f);
						hash = oathashs(hash, disk->name);
						sputeol(f);
						if (serror(f)) {
							fprintf(stderr, "Error writing the content file '%s'. %s.\n", serrorfile(f), strerror(errno));
							exit(EXIT_FAILURE);
						}
					}

					sputsl("off ", f);
					hash = oathash8(hash, 'o');

					sputu32(k, f);
					hash = oathash32(hash, k);

					sputeol(f);
					if (serror(f)) {
						fprintf(stderr, "Error writing the content file '%s'. %s.\n", serrorfile(f), strerror(errno));
						exit(EXIT_FAILURE);
					}
				}
			}
		}
	}

	sputsl("sign ", f);
	sputu32(hash, f);
	sputeol(f);
	if (serror(f)) {
		fprintf(stderr, "Error writing the content file '%s'. %s.\n", serrorfile(f), strerror(errno));
		exit(EXIT_FAILURE);
	}

	/* Use the sequence fflush() -> fsync() -> fclose() -> rename() to ensure */
	/* than even in a system crash event we have one valid copy of the file. */

	if (sflush(f) != 0) {
		fprintf(stderr, "Error writing the content file '%s', in flush(). %s.\n", serrorfile(f), strerror(errno));
		exit(EXIT_FAILURE);
	}

#if HAVE_FSYNC
	if (ssync(f) != 0) {
		fprintf(stderr, "Error writing the content file '%s' in sync(). %s.\n", serrorfile(f), strerror(errno));
		exit(EXIT_FAILURE);
	}
#endif

	if (sclose(f) != 0) {
		fprintf(stderr, "Error writing the content file '%s' in close(). %s.\n", serrorfile(f), strerror(errno));
		exit(EXIT_FAILURE);
	}

	i = tommy_list_head(&state->contentlist);
	while (i) {
		struct snapraid_content* content = i->data;
		char tmp[PATH_MAX];
		pathprint(tmp, sizeof(tmp), "%s.tmp", content->content);
		if (rename(tmp, content->content) != 0) {
			fprintf(stderr, "Error renaming the content file '%s' to '%s' in rename(). %s.\n", tmp, content->content, strerror(errno));
			exit(EXIT_FAILURE);
		}
		i = i->next;
	}

	if (state->verbose) {
		printf("\tfile %u\n", count_file);
		printf("\tblock %u\n", count_block);
		printf("\tsymlink %u\n", count_link);
	}

	state->need_write = 0; /* no write needed anymore */
}

void state_filter(struct snapraid_state* state, tommy_list* filterlist)
{
	tommy_node* i;

	/* if no filter, include all */
	if (tommy_list_empty(filterlist))
		return;

	printf("Filtering...\n");

	if (state->verbose) {
		tommy_node* k;
		for(k=tommy_list_head(filterlist);k!=0;k=k->next) {
			struct snapraid_filter* filter = k->data;
			printf("\t%s", filter->pattern);
			if (filter->is_dir)
				printf("/");
			printf("\n");
		}
	}

	/* for each disk */
	for(i=state->disklist;i!=0;i=i->next) {
		tommy_node* j;
		struct snapraid_disk* disk = i->data;

		/* for each file */
		for(j=tommy_list_head(&disk->filelist);j!=0;j=j->next) {
			struct snapraid_file* file = j->data;

			if (filter_path(filterlist, file->sub) != 0) {
				file_flag_set(file, FILE_IS_EXCLUDED);
			}

			if (state->verbose && !file_flag_has(file, FILE_IS_EXCLUDED)) {
				printf("Processing file '%s'\n", file->sub);
			}
		}

		/* for each link */
		for(j=tommy_list_head(&disk->linklist);j!=0;j=j->next) {
			struct snapraid_link* link = j->data;

			if (filter_path(filterlist, link->sub) != 0) {
				link_flag_set(link, FILE_IS_EXCLUDED);
			}

			if (state->verbose && !link_flag_has(link, FILE_IS_EXCLUDED)) {
				printf("Processing symlink '%s'\n", link->sub);
			}
		}
	}
}

void state_progress_begin(struct snapraid_state* state, block_off_t blockstart, block_off_t blockmax, block_off_t countmax)
{
	if (state->gui) {
		fprintf(stderr,"run:begin:%u:%u:%u\n", blockstart, blockmax, countmax);
		fflush(stderr);
	} else {
		time_t now;

		now = time(0);

		state->progress_start = now;
		state->progress_last = now;
		state->progress_subtract = 0;
	}
}

void state_progress_end(struct snapraid_state* state, block_off_t countpos, block_off_t countmax, data_off_t countsize)
{
	if (state->gui) {
		fprintf(stderr, "run:end\n");
		fflush(stderr);
	} else {
		unsigned countsize_MiB = (countsize + 1024*1024 - 1) / (1024*1024);

		if (countmax)
			printf("%u%% completed, %u MiB processed\n", countpos * 100 / countmax, countsize_MiB);
		else
			printf("Nothing to do\n");
	}
}

#define PROGRESS_CLEAR "          "

void state_progress_stop(struct snapraid_state* state)
{
	time_t now;

	now = time(0);

	printf("\n");
	
	state->progress_interruption = now;
}

void state_progress_restart(struct snapraid_state* state)
{
	time_t now;

	now = time(0);

	if (now >= state->progress_interruption) /* avoid degenerated cases when the clock is manually adjusted */
		state->progress_subtract += now - state->progress_interruption;
}

int state_progress(struct snapraid_state* state, block_off_t blockpos, block_off_t countpos, block_off_t countmax, data_off_t countsize)
{
	if (state->gui) {
		fprintf(stderr, "run:pos:%u:%u:%"PRIu64"\n", blockpos, countpos, countsize);
		fflush(stderr);
	} else {
		time_t now;

		now = time(0);

		if (state->progress_last != now) {
			time_t delta = now - state->progress_start - state->progress_subtract;

			printf("%u%%, %u MiB", countpos * 100 / countmax, (unsigned)(countsize / (1024*1024)));

			if (delta != 0) {
				printf(", %u MiB/s", (unsigned)(countsize / (1024*1024) / delta));
			}

			if (delta > 5 && countpos > 0) {
				unsigned m, h;
				data_off_t todo = countmax - countpos;

				m = todo * delta / (60 * countpos);

				h = m / 60;
				m = m % 60;

				printf(", %u:%02u ETA%s", h, m, PROGRESS_CLEAR);
			}
			printf("\r");
			fflush(stdout);
			state->progress_last = now;
		}
	}

	/* stop if requested */
	if (global_interrupt) {
		if (!state->gui) {
			printf("\n");
			printf("Stopping for interruption at block %u\n", blockpos);
		}
		return 1;
	}

	return 0;
}

