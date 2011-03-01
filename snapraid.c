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
#include "md5.h"
#include "tommylist.h"
#include "tommyhash.h"
#include "tommyhashdyn.h"
#include "tommyarray.h"

/****************************************************************************/
/* generic */

/**
 * Max length of a line.
 */
#define LINE_MAX 1024

/**
 * If Ctrl+C was pressed.
 */
static volatile int global_interrupt = 0;

/**
 * Safe malloc.
 */
void* malloc_nofail(size_t size)
{
	void* ptr = malloc(size);

	if (!ptr) {
		fprintf(stderr, "Low memory\n");
		exit(EXIT_FAILURE);
	}

	return ptr;
}

/****************************************************************************/
/* snapraid */

/**
 * Size of the hash used as a checksum.
 */
#define HASH_MAX MD5_SIZE

/**
 * Invalid position.
 */
#define POS_INVALID -1

/**
 * Basic position type.
 * With a 32 bit and 128k block you can address 256 TB.
 */
typedef unsigned pos_t;

struct snapraid_file;

/**
 * Block of a file.
 */
struct snapraid_block {
	pos_t parity_pos; /**< Position of the block in the parity. */
	int is_hashed; /**< If the hash of the block is valid. */
	struct snapraid_file* file; /**< Back pointer to the file owning this block. */
	unsigned char hash[HASH_MAX]; /**< Hash of the block. */
};

/**
 * File.
 */
struct snapraid_file {
	char sub[PATH_MAX]; /**< Sub path of the file. Without the disk dir. The disk is implicit. */
	uint64_t size; /**< Size of the file. */
	struct snapraid_block* blockvec; /**< All the blocks of the file. */
	unsigned blockmax; /**< Number of blocks. */
	time_t mtime; /**< Modification time. */
	int is_present; /**< If it's seen as present. */

	/* nodes for data structures */
	tommy_node nodelist;
	tommy_hashdyn_node nodeset;
};

/**
 * Disk.
 */
struct snapraid_disk {
	char name[PATH_MAX]; /**< Name of the disk. */
	char dir[PATH_MAX]; /**< Mount point of the disk. */
	pos_t first_free_block; /**< First free searching block. */
	tommy_list filelist; /**< List of all the files. */
	tommy_hashdyn fileset; /**< Hashtable by sub of all the files. */
	tommy_array blockarr; /**< Block array of the disk. */
};

/****************************************************************************/
/* snapraid */

/**
 * Get the relative position of a block inside the file.
 */
pos_t block_file_pos(struct snapraid_block* block)
{
	struct snapraid_file* file = block->file;

	if (block < file->blockvec || block >= file->blockvec + file->blockmax) {
		fprintf(stderr, "Internal inconsistency in block %u ownership\n", block->parity_pos);
		exit(EXIT_FAILURE);
	}

	return block - file->blockvec;
}

/**
 * Get the size in bytes of the block.
 * If it's the last block of a file it could less than block_size.
 */
unsigned block_file_size(struct snapraid_block* block, unsigned block_size)
{
	pos_t pos = block_file_pos(block);

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

struct snapraid_file* file_alloc(unsigned block_size, const char* sub, uint64_t size, time_t mtime)
{
	struct snapraid_file* file;
	pos_t i;

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

tommy_uint32_t file_hash(const char* sub)
{
	return tommy_hash_u32(0, sub, strlen(sub));
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

static inline struct snapraid_block* disk_block_get(struct snapraid_disk* disk, pos_t pos)
{
	if (pos < tommy_array_size(&disk->blockarr))
		return tommy_array_get(&disk->blockarr, pos);
	else
		return 0;
}

/****************************************************************************/
/* state */

struct snapraid_state {
	int verbose; /**< Verbose output. */
	unsigned block_size; /**< Block size in bytes. */
	char content[PATH_MAX]; /**< Path of the content file. */
	char parity[PATH_MAX]; /**< Path of the parity file. */
	tommy_array diskarr; /**< Disk array. */
};

void state_init(struct snapraid_state* state)
{
	state->verbose = 0;
	state->block_size = 128*1024; /* default 128 kB */
	state->content[0] = 0;
	state->parity[0] = 0;
	tommy_array_init(&state->diskarr);

	if (sizeof(off_t) < sizeof(uint64_t)) {
		fprintf(stderr, "Internal inconsistency in off_t type size\n");
		exit(EXIT_FAILURE);
	}
}

void state_done(struct snapraid_state* state)
{
	unsigned i;
	for(i=0;i<tommy_array_size(&state->diskarr);++i)
		disk_free(tommy_array_get(&state->diskarr, i));
	tommy_array_done(&state->diskarr);
}

/****************************************************************************/
/* config */

void state_config(struct snapraid_state* state, const char* path, int verbose)
{
	FILE* f;
	unsigned line;

	state->verbose = verbose;

	f = fopen(path, "rt");
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

	line = 0;
	while (1) {
		char buffer[LINE_MAX];
		char* tag;
		char* s;
		int ret;

		++line;

		ret = strgets(buffer, LINE_MAX, f);
		if (ret < 0) {
			fprintf(stderr, "Error reading the configuration file '%s' at line %u\n", path, line);
			exit(EXIT_FAILURE);
		}
		if (ret == 0)
			break;

		/* start */
		s = buffer;
		s = strskip(s);

		/* ignore commens and empty lines */
		if (*s == '#' || *s == 0)
			continue;

		tag = s;
		s = strtoken(s);

		if (strcmp(tag, "block_size") == 0) {
			ret = stru(s, &state->block_size);
			if (ret != 0) {
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
			if (*state->parity) {
				fprintf(stderr, "Multiple 'parity' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}
			snprintf(state->parity, sizeof(state->parity), "%s", s);
		} else if (strcmp(tag, "content") == 0) {
			if (*state->content) {
				fprintf(stderr, "Multiple 'content' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}
			snprintf(state->content, sizeof(state->content), "%s", s);
		} else if (strcmp(tag, "disk") == 0) {
			char* name = s;
			s = strtoken(s);
			tommy_array_insert(&state->diskarr, disk_alloc(name, s));
		} else {
			fprintf(stderr, "Unknown tag '%s' in '%s'\n", tag, path);
			exit(EXIT_FAILURE);
		}
	}

	fclose(f);

	if (!state->parity) {
		fprintf(stderr, "No 'parity' specification in '%s' at line %u\n", path, line);
		exit(EXIT_FAILURE);
	}
	if (!state->content) {
		fprintf(stderr, "No 'content' specification in '%s' at line %u\n", path, line);
		exit(EXIT_FAILURE);
	}
}

/****************************************************************************/
/* read */

/**
 * Read the state.
 */
void state_read(struct snapraid_state* state)
{
	FILE* f;
	char path[PATH_MAX];
	struct snapraid_disk* disk;
	struct snapraid_file* file;
	unsigned block_index;
	unsigned line;
	unsigned count_file;
	unsigned count_block;

	count_file = 0;
	count_block = 0;

	snprintf(path, sizeof(path), "%s", state->content);
	f = fopen(state->content, "r");
	if (!f) {
		/* if not found, assume empty */
		if (errno == ENOENT)
			return;
			
		fprintf(stderr, "Error opening the state file '%s'\n", path);
		exit(EXIT_FAILURE);
	}

	printf("Loading state...\n");

	disk = 0;
	file = 0;
	line = 0;
	block_index = 0;
	while (1) {
		char buffer[LINE_MAX];
		char* tag;
		char* s;
		int ret;

		++line;

		ret = strgets(buffer, LINE_MAX, f);
		if (ret < 0) {
			fprintf(stderr, "Error reading the state file '%s' at line %u\n", path, line);
			exit(EXIT_FAILURE);
		}
		if (ret == 0)
			break;

		/* start */
		s = buffer;
		s = strskip(s);

		/* ignore commens and empty lines */
		if (*s == '#' || *s == 0)
			continue;

		tag = s;
		s = strtoken(s);

		if (strcmp(tag, "file") == 0) {
			char* name;
			char* size;
			char* mtime;
			char* sub;
			uint64_t v_size;
			uint64_t v_mtime;
			unsigned i;

			if (file) {
				fprintf(stderr, "Missing 'blk' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}

			name = s;
			s = strtoken(s);
			size = s;
			s = strtoken(s);
			mtime = s;
			s = strtoken(s);
			sub = s;

			ret = stru64(size, &v_size);
			if (ret != 0) {
				fprintf(stderr, "Invalid 'file' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}

			ret = stru64(mtime, &v_mtime);
			if (ret != 0) {
				fprintf(stderr, "Invalid 'file' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}

			if (!*sub) {
				fprintf(stderr, "Invalid 'file' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}

			/* find the disk */
			for(i=0;i<tommy_array_size(&state->diskarr);++i) {
				disk = tommy_array_get(&state->diskarr, i);
				if (strcmp(disk->name, name) == 0)
					break;
			}
			if (i == tommy_array_size(&state->diskarr)) {
				fprintf(stderr, "Disk named '%s' not found in '%s' at line %u\n", name, path, line);
				exit(EXIT_FAILURE);
			}

			/* allocate the file */
			file = file_alloc(state->block_size, sub, v_size, v_mtime);

			/* insert the file in the file containers */
			tommy_hashdyn_insert(&disk->fileset, &file->nodeset, file, file_hash(file->sub));
			tommy_list_insert_tail(&disk->filelist, &file->nodelist, file);

			/* start the block allocation of the file */
			block_index = 0;

			/* check for empty file */
			if (block_index == file->blockmax) {
				file = 0;
				disk = 0;
			}

			/* stat */
			++count_file;
		} else if (strcmp(tag, "blk") == 0) {
			char* pos;
			char* hash;
			pos_t v_pos;
			struct snapraid_block* block;
			char* e;

			if (!file) {
				fprintf(stderr, "Unexpected 'blk' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}

			pos = s;
			s = strtoken(s);
			hash = s;

			ret = stru(pos, &v_pos);
			if (ret != 0) {
				fprintf(stderr, "Invalid 'blk' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}

			if (block_index >= file->blockmax) {
				fprintf(stderr, "Internal inconsistency in 'blk' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}

			block = &file->blockvec[block_index];

			if (block->parity_pos != POS_INVALID) {
				fprintf(stderr, "Internal inconsistency in 'blk' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}

			block->parity_pos = v_pos;

			/* set the hash only if present */
			if (*hash != 0) {
				e = strdechex(block->hash, HASH_MAX, hash);
				if (e) {
					fprintf(stderr, "Invalid 'blk' specification in '%s' at line %u\n", path, line);
					exit(EXIT_FAILURE);
				}
				block->is_hashed = 1;
			}

			/* insert the block in the block array */
			tommy_array_grow(&disk->blockarr, block->parity_pos + 1);
			tommy_array_set(&disk->blockarr, block->parity_pos, block);

			/* check for termination of the block list */
			++block_index;
			if (block_index == file->blockmax) {
				file = 0;
				disk = 0;
			}

			/* stat */
			++count_block;
		} else {
			fprintf(stderr, "Unknown tag '%s' in '%s' at line%u\n", tag, path, line);
			exit(EXIT_FAILURE);
		}
	}

	if (file) {
		fprintf(stderr, "Missing 'blk' specification in '%s' at line %u\n", path, line);
		exit(EXIT_FAILURE);
	}

	fclose(f);

	if (state->verbose) {
		printf("\tfile %u\n", count_file);
		printf("\tblock %u\n", count_block);
	}
}

/****************************************************************************/
/* write */

/**
 * Write the new state.
 */
void state_write(struct snapraid_state* state)
{
	FILE* f;
	char path[PATH_MAX];
	unsigned i;
	unsigned count_file;
	unsigned count_block;

	count_file = 0;
	count_block = 0;

	printf("Saving state...\n");

	snprintf(path, sizeof(path), "%s.tmp", state->content);
	f = fopen(path, "w");
	if (!f) {
		fprintf(stderr, "Error opening for writing the state file '%s'\n", path);
		exit(EXIT_FAILURE);
	}

	/* for each disk */
	for(i=0;i<tommy_array_size(&state->diskarr);++i) {
		tommy_node* j;
		struct snapraid_disk* disk = tommy_array_get(&state->diskarr, i);

		/* for each file */
		for(j=disk->filelist;j!=0;j=j->next) {
			pos_t k;
			struct snapraid_file* file = j->data;
			int ret;

			ret = fprintf(f,"file %s %lld %ld %s\n", disk->name, file->size, file->mtime, file->sub);
			if (ret < 0) {
				fprintf(stderr, "Error writing the state file '%s' in fprintf(). %s.\n", path, strerror(errno));
				exit(EXIT_FAILURE);
			}

			/* for each block */
			for(k=0;k<file->blockmax;++k) {
				struct snapraid_block* block = &file->blockvec[k];

				if (block->is_hashed) {
					char s_hash[HASH_MAX*2+1];
					strenchex(s_hash, block->hash, HASH_MAX);
					s_hash[HASH_MAX*2] = 0;
					ret = fprintf(f, "blk %u %s\n", block->parity_pos, s_hash);
				} else {
					ret = fprintf(f, "blk %u\n", block->parity_pos);
				}
				if (ret < 0) {
					fprintf(stderr, "Error writing the state file '%s' in fprintf(). %s.\n", path, strerror(errno));
					exit(EXIT_FAILURE);
				}

				++count_block;
			}

			++count_file;
		}
	}

	/* Use the sequence fflush() -> fsync() -> fclose() -> rename() to ensure */
	/* than even in a system crash event we have one valid copy of the file. */

	if (fflush(f) != 0) {
		fprintf(stderr, "Error writing the state file '%s', in fflush(). %s.\n", path, strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (fsync(fileno(f)) != 0) {
		fprintf(stderr, "Error writing the state file '%s' in fsync(). %s.\n", path, strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (fclose(f) != 0) {
		fprintf(stderr, "Error writing the state file '%s' in close(). %s.\n", path, strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (rename(path, state->content) != 0) {
		fprintf(stderr, "Error renaming the state file '%s' to '%s' in rename(). %s.\n", path, state->content, strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (state->verbose) {
		printf("\tfile %u\n", count_file);
		printf("\tblock %u\n", count_block);
	}
}

/****************************************************************************/
/* scan */

struct snapraid_scan {
	unsigned count_equal;
	unsigned count_change;
	unsigned count_remove;
	unsigned count_insert;
};

/**
 * Remove the specified file from the data set.
 */
static void state_scan_file_remove(struct snapraid_state* state, struct snapraid_disk* disk, struct snapraid_file* file)
{
	pos_t i;

	/* free all the blocks of the file */
	for(i=0;i<file->blockmax;++i) {
		pos_t block_pos = file->blockvec[i].parity_pos;

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
static void state_scan_file_insert(struct snapraid_state* state, struct snapraid_disk* disk, struct snapraid_file* file)
{
	pos_t i;
	pos_t block_max;
	pos_t block_pos;

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

static void state_scan_file(struct snapraid_scan* scan, struct snapraid_state* state, struct snapraid_disk* disk, const char* path, const char* sub)
{
	struct stat st;
	struct snapraid_file* file;

	if (stat(path, &st) != 0) {
		fprintf(stderr, "Error in stat file '%s'\n", path);
		exit(EXIT_FAILURE);
	}

	/* check if the file already exists */
	file = tommy_hashdyn_search(&disk->fileset, file_compare, sub, file_hash(sub));
	if (file) {
		/* check if the file is the same */
		if (file->size == st.st_size && file->mtime == st.st_mtime) {
			/* mark as present */
			++scan->count_equal;
			file->is_present = 1;
			return;
		} else {
			/* remove and reinsert it */
			++scan->count_change;
			--scan->count_insert;
			state_scan_file_remove(state, disk, file);
			/* continue to insert it */
		}
	}

	/* create the new file */
	++scan->count_insert;
	file = file_alloc(state->block_size, sub, st.st_size, st.st_mtime);
	file->is_present = 1;

	/* insert it */
	state_scan_file_insert(state, disk, file);
}

static void state_scan_dir(struct snapraid_scan* scan, struct snapraid_state* state, struct snapraid_disk* disk, const char* dir, const char* sub)
{
	DIR* d;
	struct dirent* dd;

	d = opendir(dir);
	if (!d) {
		fprintf(stderr, "Error accessing directory '%s'. %s.\n", dir, strerror(errno));
		exit(EXIT_FAILURE);
	}

	while ((dd = readdir(d)) != 0) {
		if (dd->d_type == DT_REG) {
			char path_next[PATH_MAX];
			char sub_next[PATH_MAX];
			snprintf(path_next, sizeof(path_next), "%s%s", dir, dd->d_name);
			snprintf(sub_next, sizeof(sub_next), "%s%s", sub, dd->d_name);
			state_scan_file(scan, state, disk, path_next, sub_next);
		} else if (dd->d_type == DT_DIR) {
			if (strcmp(dd->d_name, ".") != 0 && strcmp(dd->d_name, "..") != 0 && strcmp(dd->d_name, "lost+found") != 0
			) {
				char dir_next[PATH_MAX];
				char sub_next[PATH_MAX];
				snprintf(dir_next, sizeof(dir_next), "%s%s/", dir, dd->d_name);
				snprintf(sub_next, sizeof(sub_next), "%s%s/", sub, dd->d_name);
				state_scan_dir(scan, state, disk, dir_next, sub_next);
			}
		} else {
			if (state->verbose) {
				printf("warning: File '%s/%s' not processed\n", dir, dd->d_name);
			}
		}
	}

	closedir(d);
}

/**
 * Update the internal state to represent the new state.
 */
void state_scan(struct snapraid_state* state)
{
	unsigned disk_count = tommy_array_size(&state->diskarr);
	unsigned i;
	struct snapraid_scan scan;

	scan.count_equal = 0;
	scan.count_change = 0;
	scan.count_remove = 0;
	scan.count_insert = 0;

	for(i=0;i<disk_count;++i) {
		struct snapraid_disk* disk = tommy_array_get(&state->diskarr, i);
		tommy_node* node;

		printf("Scanning disk %s...\n", disk->name);

		state_scan_dir(&scan, state, disk, disk->dir, "");

		/* check for removed files */
		node = disk->filelist;
		while (node) {
			struct snapraid_file* file = node->data;

			/* next node */
			node = node->next;

			/* remove if not present */
			if (!file->is_present) {
				state_scan_file_remove(state, disk, file);
				++scan.count_remove;
			}
		}
	}

	if (state->verbose) {
		printf("\tequal %d\n", scan.count_equal);
		printf("\tchanged %d\n", scan.count_change);
		printf("\tremoved %d\n", scan.count_remove);
		printf("\tadded %d\n", scan.count_insert);
	}
}

/****************************************************************************/
/* common */

/**
 * Computes the size of the parity data in number of blocks.
 */
static pos_t state_sync_resize(struct snapraid_state* state)
{
	unsigned disk_count = tommy_array_size(&state->diskarr);
	pos_t parity_block;
	unsigned i;

	/* compute the size of the parity file */
	parity_block = 0;
	for(i=0;i<disk_count;++i) {
		struct snapraid_disk* disk = tommy_array_get(&state->diskarr, i);

		/* start from the declared size */
		pos_t block = tommy_array_size(&disk->blockarr);

		/* decrease the block until an allocated block */
		while (block > 0 && tommy_array_get(&disk->blockarr, block - 1) == 0)
			--block;

		if (i == 0 || parity_block < block)
			parity_block = block;
	}

	return parity_block;
}

static void state_xor_block(unsigned char* xor, const unsigned char* block, unsigned size)
{
	while (size >= 16) {
		uint32_t* xor32 = (uint32_t*)xor;
		const uint32_t* block32 = (const uint32_t*)block;
		xor32[0] ^= block32[0];
		xor32[1] ^= block32[1];
		xor32[2] ^= block32[2];
		xor32[3] ^= block32[3];

		xor += 16;
		block += 16;
		size -= 16;
	}

	while (size >= 4) {
		uint32_t* xor32 = (uint32_t*)xor;
		const uint32_t* block32 = (const uint32_t*)block;
		xor32[0] ^= block32[0];

		xor += 4;
		block += 4;
		size -= 4;
	}

	while (size != 0) {
		xor[0] ^= block[0];

		xor += 1;
		block += 1;
		size -= 1;
	}
}

struct snapraid_sync_block {
	char path[PATH_MAX]; /**< Path of the file. */
	struct snapraid_disk* disk; /**< Disk of the file. */
	struct snapraid_file* file; /**< File opened. */
	int f; /**< Handle of the file. */
	off_t offset; /**< Position in the file. */
};

static int state_sync_read_block(int ret_on_error, struct snapraid_sync_block* sync, struct snapraid_block* block, unsigned char* block_buffer, unsigned block_size)
{
	ssize_t read_ret;
	off_t offset;
	unsigned read_size;

	if (sync->file != block->file) {
		if (sync->f != -1) {
			int ret;
			ret = close(sync->f);
			if (ret != 0) {
				/* here we always abort, as close is supposed to never fail */
				fprintf(stderr, "Error closing file '%s'. %s.\n", sync->file->sub, strerror(errno));
				exit(EXIT_FAILURE);
			}
		}

		/* invalidate for error */
		sync->file = 0;
		sync->f = -1;

		snprintf(sync->path, sizeof(sync->path), "%s%s", sync->disk->dir, block->file->sub);
		sync->f = open(sync->path, O_RDONLY | O_BINARY);
		if (sync->f == -1) {
			if (ret_on_error)
				return ret_on_error;
			fprintf(stderr, "Error opening file '%s'. %s.\n", sync->path, strerror(errno));
			exit(EXIT_FAILURE);
		}

		sync->file = block->file;
		sync->offset = 0;
	}

	offset = block_file_pos(block) * (off_t)block_size;

	if (sync->offset != offset) {
		off_t ret;

		/* invalidate for error */
		sync->offset = -1;

		ret = lseek(sync->f, offset, SEEK_SET);
		if (ret != offset) {
			if (ret_on_error)
				return ret_on_error;
			fprintf(stderr, "Error seeking file '%s'. %s.\n", sync->path, strerror(errno));
			exit(EXIT_FAILURE);
		}
		sync->offset = offset;
	}

	read_size = block_file_size(block, block_size);

	read_ret = read(sync->f, block_buffer, read_size);
	if (read_ret != read_size) {
		if (ret_on_error)
			return ret_on_error;
		fprintf(stderr, "Error reading file '%s'. %s.\n", sync->path, strerror(errno));
		exit(EXIT_FAILURE);
	}

	sync->offset += read_size;

	return read_size;
}

static void state_sync_write_parity(const char* parity_path, int parity_f, off_t* parity_offset, pos_t pos, unsigned char* block_buffer, unsigned block_size)
{
	ssize_t write_ret;
	off_t offset;

	offset = pos * (off_t)block_size;

	if (*parity_offset != offset) {
		off_t ret;
		ret = lseek(parity_f, offset, SEEK_SET);
		if (ret != offset) {
			fprintf(stderr, "Error seeking file '%s'. %s.\n", parity_path, strerror(errno));
			exit(EXIT_FAILURE);
		}
		*parity_offset = offset;
	}

	write_ret = write(parity_f, block_buffer, block_size);
	if (write_ret != block_size) {
		if (errno == ENOSPC) {
			fprintf(stderr, "Failed to grow parity file '%s' due lack of space.\n", parity_path);
		} else {
			fprintf(stderr, "Error writing file '%s'. %s.\n", parity_path, strerror(errno));
		}
		exit(EXIT_FAILURE);
	}

	*parity_offset += block_size;
}

static int state_sync_read_parity(int ret_on_error, const char* parity_path, int parity_f, off_t* parity_offset, pos_t pos, unsigned char* block_buffer, unsigned block_size)
{
	ssize_t read_ret;
	off_t offset;

	offset = pos * (off_t)block_size;

	if (*parity_offset != offset) {
		off_t ret;

		/* invalidate for error */
		*parity_offset = -1;

		ret = lseek(parity_f, offset, SEEK_SET);
		if (ret != offset) {
			if (ret_on_error)
				return ret_on_error;
			fprintf(stderr, "Error seeking file '%s'. %s.\n", parity_path, strerror(errno));
			exit(EXIT_FAILURE);
		}
		*parity_offset = offset;
	}

	read_ret = read(parity_f, block_buffer, block_size);
	if (read_ret != block_size) {
		if (ret_on_error)
			return ret_on_error;
		fprintf(stderr, "Error reading file '%s'. %s.\n", parity_path, strerror(errno));
		exit(EXIT_FAILURE);
	}

	*parity_offset += block_size;

	return block_size;
}

/****************************************************************************/
/* sync */

static void state_sync_process(struct snapraid_state* state, int parity_f, pos_t blockstart, pos_t blockmax)
{
	struct snapraid_sync_block* sync;
	off_t parity_offset;
	unsigned diskmax = tommy_array_size(&state->diskarr);
	pos_t i;
	unsigned char* block_buffer;
	unsigned char* xor_buffer;
	int ret;
	uint64_t count_size;
	time_t start;
	time_t last;

	block_buffer = malloc_nofail(state->block_size);
	xor_buffer = malloc_nofail(state->block_size);

	sync = malloc_nofail(diskmax * sizeof(struct snapraid_sync_block));
	for(i=0;i<diskmax;++i) {
		sync[i].disk = tommy_array_get(&state->diskarr, i);
		sync[i].file = 0;
		sync[i].f = -1;
	}
	parity_offset = -1;

	start = time(0);
	last = start;
	count_size = 0;

	for(i=blockstart;i<blockmax;++i) {
		unsigned j;
		int unhashed;
		time_t now;
		
		/* for each disk, search for an unhashed block */
		unhashed = 0;
		for(j=0;j<diskmax;++j) {
			struct snapraid_block* block = disk_block_get(sync[j].disk, i);
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

			block = disk_block_get(sync[j].disk, i);
			if (!block)
				continue;

			read_size = state_sync_read_block(0, &sync[j], block, block_buffer, state->block_size);

			/* now compute the hash */
			md5_init(&md5);
			md5_update(&md5, block_buffer, read_size);
			md5_final(&md5, hash);

			if (block->is_hashed) {
				/* compare the hash */
				if (memcmp(hash, block->hash, HASH_MAX) != 0) {
					fprintf(stderr, "Data error for file %s at position %u\n", block->file->sub, block_file_pos(block));
					fprintf(stderr, "Stopping to allow recovery. Try with 'snapraid -s %u fix'\n", block_file_pos(block));
					exit(EXIT_FAILURE);
				}
			} else {
				/* copy the hash */
				memcpy(block->hash, hash, HASH_MAX);

				/* mark the block as hashed */
				block->is_hashed = 1;
			}

			/* compute the parity */
			state_xor_block(xor_buffer, block_buffer, read_size);

			count_size += read_size;
		}

		/* write the parity */
		state_sync_write_parity(state->parity, parity_f, &parity_offset, i, xor_buffer, state->block_size);

		/* progress */
		now = time(0);
		if (last != now) {
			printf("%u%%, %u MB", i * 100 / blockmax, (unsigned)(count_size / (1024*1024)));
			if (now != start) {
				printf(", %u MB/s", (unsigned)(count_size / (1024*1024) / (now - start)));
			}
			printf("\r");
			fflush(stdout);
			last = now;
		}

		/* stop if requested */
		if (global_interrupt) {
			printf("\rStopping at block %u\n", i);
			break;
		}
	}

	printf("%u%% completed, %u MB processed\n", i * 100 / blockmax, (unsigned)(count_size / (1024*1024)));

	printf("Done\n");

	for(i=0;i<diskmax;++i) {
		if (sync[i].file) {
			ret = close(sync[i].f);
			if (ret != 0) {
				fprintf(stderr, "Error closing file '%s'. %s.\n", sync[i].file->sub, strerror(errno));
				exit(EXIT_FAILURE);
			}
		}
	}

	free(sync);
	free(block_buffer);
	free(xor_buffer);
}

/**
 * Update the parity file.
 */
void state_sync(struct snapraid_state* state, pos_t blockstart)
{
	char path[PATH_MAX];
	pos_t blockmax;
	off_t size;
	int ret;
	int f;
	struct stat st;

	printf("Syncing...\n");

	blockmax = state_sync_resize(state);
	size = blockmax * (off_t)state->block_size;

	if (blockstart >= blockmax) {
		fprintf(stderr, "The specified starting block %u is bigger than the parity size %u.\n", blockstart, blockmax);
		exit(EXIT_FAILURE);
	}

	snprintf(path, sizeof(path), "%s", state->parity);
	f = open(path, O_RDWR | O_CREAT, 0600);
	if (f == -1) {
		fprintf(stderr, "Error opening parity file '%s'. %s.\n", path, strerror(errno));
		exit(EXIT_FAILURE);
	}

	ret = fstat(f, &st);
	if (ret != 0) {
		fprintf(stderr, "Error accessing parity file '%s'. %s.\n", path, strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (st.st_size < size) {
#if HAVE_FALLOCATE
		/* allocate real space. This is Linux specific. */
		ret = fallocate(f, 0, 0, size);
#else
		/* allocate using a sparse file */
		ret = ftruncate(f, size);
#endif
		if (ret != 0) {
			if (errno == ENOSPC) {
				fprintf(stderr, "Failed to grow parity file '%s' due lack of space.\n", path);
			} else {
				fprintf(stderr, "Error growing parity file '%s'. %s.\n", path, strerror(errno));
			}
			exit(EXIT_FAILURE);
		}
	} else {
		ret = ftruncate(f, size);
		if (ret != 0) {
			fprintf(stderr, "Error truncating parity file '%s'. %s.\n", path, strerror(errno));
			exit(EXIT_FAILURE);
		}
	}

	/* ensure that the file size change is written to disk */
	/* this is not really required, but just to avoid to result in a wrong sized file */
	/* in case of a system crash */
	ret = fsync(f);
	if (ret != 0) {
		fprintf(stderr, "Error synching parity file '%s'. %s.\n", path, strerror(errno));
		exit(EXIT_FAILURE);
	}

	state_sync_process(state, f, blockstart, blockmax);

	/* ensure that data changes are written to disk */
	/* this is required to ensure that parity is more updated than content */
	/* in case of a system crash */
	ret = fsync(f);
	if (ret != 0) {
		fprintf(stderr, "Error synching parity file '%s'. %s.\n", path, strerror(errno));
		exit(EXIT_FAILURE);
	}

	ret = close(f);
	if (ret != 0) {
		fprintf(stderr, "Error closing parity file '%s'. %s.\n", path, strerror(errno));
		exit(EXIT_FAILURE);
	}
}

/****************************************************************************/
/* check */


static int state_check_process(int ret_on_error, struct snapraid_state* state, int parity_f, pos_t blockstart, pos_t blockmax)
{
	struct snapraid_sync_block* sync;
	off_t parity_offset;
	unsigned diskmax = tommy_array_size(&state->diskarr);
	pos_t i;
	unsigned char* block_buffer;
	unsigned char* xor_buffer;
	int ret;
	uint64_t count_size;
	time_t start;
	time_t last;
	unsigned error;
	unsigned unrecoverable_error;

	block_buffer = malloc_nofail(state->block_size);
	xor_buffer = malloc_nofail(state->block_size);

	sync = malloc_nofail(diskmax * sizeof(struct snapraid_sync_block));
	for(i=0;i<diskmax;++i) {
		sync[i].disk = tommy_array_get(&state->diskarr, i);
		sync[i].file = 0;
		sync[i].f = -1;
	}
	parity_offset = -1;
	error = 0;
	unrecoverable_error = 0;

	start = time(0);
	last = start;
	count_size = 0;

	for(i=blockstart;i<blockmax;++i) {
		unsigned j;
		int hashed;
		int unhashed;
		int failed;
		unsigned char failed_hash[HASH_MAX];
		unsigned failed_size;
		time_t now;

		/* for each disk, search for a hashed block */
		hashed = 0;
		for(j=0;j<diskmax;++j) {
			struct snapraid_block* block = disk_block_get(sync[j].disk, i);
			if (block && block->is_hashed) {
				hashed = 1;
				break;
			}
		}

		/* if there is at least an hashed block we have to check */
		if (!hashed)
			continue;

		/* start with 0 */
		memset(xor_buffer, 0, state->block_size);

		/* for each disk, process the block */
		unhashed = 0;
		failed = 0;
		for(j=0;j<diskmax;++j) {
			int read_size;
			struct md5_t md5;
			unsigned char hash[HASH_MAX];
			struct snapraid_block* block;

			block = disk_block_get(sync[j].disk, i);
			if (!block)
				continue;

			/* we can check only if the block is hashed */
			if (!block->is_hashed) {
				unhashed = 1;
				continue;
			}

			read_size = state_sync_read_block(-1, &sync[j], block, block_buffer, state->block_size);

			if (read_size == -1) {
				++failed;

				/* save the hash for the parity check */
				memcpy(failed_hash, block->hash, HASH_MAX);
				failed_size = block_file_size(block, state->block_size);

				fprintf(stderr, "Read error for file %s at position %u\n", block->file->sub, block_file_pos(block));
				++error;
			} else {
				/* now compute the hash */
				md5_init(&md5);
				md5_update(&md5, block_buffer, read_size);
				md5_final(&md5, hash);

				/* compare the hash */
				if (memcmp(hash, block->hash, HASH_MAX) != 0) {
					++failed;
					fprintf(stderr, "Data error for file %s at position %u\n", block->file->sub, block_file_pos(block));
					++error;
				}

				/* compute the parity */
				state_xor_block(xor_buffer, block_buffer, read_size);

				count_size += read_size;
			}
		}

		/* only if no unhashed block, we can check for parity */
		if (unhashed)
			continue;

		if (failed == 0) {
			int ret;

			/* read the parity */
			ret = state_sync_read_parity(-1, state->parity, parity_f, &parity_offset, i, block_buffer, state->block_size);
			if (ret == -1) {
				fprintf(stderr, "Parity read error for position %u\n", i);
				++error;
			} else {
				/* compare it */
				if (memcmp(xor_buffer, block_buffer, state->block_size) != 0) {
					fprintf(stderr, "Parity data error for position %u\n", i);
					++error;
				}
			}
		} else if (failed == 1) {
			struct md5_t md5;
			unsigned char hash[HASH_MAX];
			int ret;

			/* read the parity */
			ret = state_sync_read_parity(-1, state->parity, parity_f, &parity_offset, i, block_buffer, state->block_size);
			if (ret == -1) {
				fprintf(stderr, "UNRECOVERABLE parity read error for position  %u\n", i);
				++unrecoverable_error;
			}

			/* compute the failed block */
			state_xor_block(block_buffer, xor_buffer, state->block_size);

			/* now compute the hash */
			md5_init(&md5);
			md5_update(&md5, block_buffer, failed_size);
			md5_final(&md5, hash);

			/* compare the hash */
			if (memcmp(hash, failed_hash, HASH_MAX) != 0) {
				fprintf(stderr, "UNRECOVERABLE parity data error for position  %u\n", i);
				++unrecoverable_error;
			}
		} else {
			fprintf(stderr, "UNRECOVERABLE parity missing for position %u\n", i);
			++unrecoverable_error;
		}

		/* progress */
		now = time(0);
		if (last != now) {
			printf("%u%%, %u MB", i * 100 / blockmax, (unsigned)(count_size / (1024*1024)));
			if (now != start) {
				printf(", %u MB/s", (unsigned)(count_size / (1024*1024) / (now - start)));
			}
			printf("\r");
			fflush(stdout);
			last = now;
		}

		/* stop if requested */
		if (global_interrupt) {
			printf("\rStopping at block %u\n", i);
			break;
		}
	}

	printf("%u%% completed, %u MB processed\n", i * 100 / blockmax, (unsigned)(count_size / (1024*1024)));

	if (error || unrecoverable_error) {
		if (error)
			printf("%u recoverable errors\n", error);
		else
			printf("No recoverable errors\n");
		if (unrecoverable_error)
			printf("%u UNRECOVERABLE errors\n", unrecoverable_error);
		else
			printf("No unrecoverable errors\n");
	} else {
		printf("No error\n");
	}

	for(i=0;i<diskmax;++i) {
		if (sync[i].file) {
			ret = close(sync[i].f);
			if (ret != 0) {
				fprintf(stderr, "Error closing file '%s'. %s.\n", sync[i].file->sub, strerror(errno));
				exit(EXIT_FAILURE);
			}
		}
	}

	free(sync);
	free(block_buffer);
	free(xor_buffer);

	return unrecoverable_error != 0 ? ret_on_error : 0;
}


/**
 * Check.
 */
void state_check(struct snapraid_state* state, pos_t blockstart)
{
	char path[PATH_MAX];
	pos_t blockmax;
	off_t size;
	int ret;
	int f;

	printf("Checking...\n");

	blockmax = state_sync_resize(state);
	size = blockmax * (off_t)state->block_size;

	if (blockstart >= blockmax) {
		fprintf(stderr, "The specified starting block %u is bigger than the parity size %u.\n", blockstart, blockmax);
		exit(EXIT_FAILURE);
	}

	snprintf(path, sizeof(path), "%s", state->parity);
	f = open(path, O_RDONLY);
	if (f == -1) {
		fprintf(stderr, "Error opening parity file '%s'. %s.\n", path, strerror(errno));
		exit(EXIT_FAILURE);
	}

	ret = state_check_process(-1, state, f, blockstart, blockmax);
	if (ret == -1) {
		/* no extra message */
		exit(EXIT_FAILURE);
	}

	ret = close(f);
	if (ret != 0) {
		fprintf(stderr, "Error closing parity file '%s'. %s.\n", path, strerror(errno));
		exit(EXIT_FAILURE);
	}
}

/****************************************************************************/
/* main */

void version(void) {
	printf(PACKAGE " v" VERSION " by Andrea Mazzoleni\n");
}

void usage(void) {
	version();

	printf("Usage: snapraid [options]\n");
	printf("\n");
	printf("Options:\n");
	printf("  " SWITCH_GETOPT_LONG("-c, --conf FILE    ", "-c") "  Configuration file (default /etc/snapraid.conf)\n");
	printf("  " SWITCH_GETOPT_LONG("-v, --verbose      ", "-v") "  Verbose\n");
	printf("  " SWITCH_GETOPT_LONG("-h, --help         ", "-h") "  Help\n");
	printf("  " SWITCH_GETOPT_LONG("-V, --version      ", "-V") "  Version\n");
}

#if HAVE_GETOPT_LONG
struct option long_options[] = {
	{ "conf", 1, 0, 'c' },
	{ "start", 1, 0, 's' },
	{ "verbose", 0, 0, 'v' },
	{ "help", 0, 0, 'h' },
	{ "version", 0, 0, 'V' },
	{ 0, 0, 0, 0 }
};
#endif

#define OPTIONS "c:s:vhV"

void signal_handler(int signal)
{
	switch (signal) {
	case SIGINT :
		global_interrupt = 1;
		break;
	}
}

#define OPERATION_SYNC 0
#define OPERATION_CHECK 1
#define OPERATION_FIX 2

int main(int argc, char* argv[])
{
	int c;
	int verbose = 0;
	const char* conf;
	struct snapraid_state state;
	int operation;
	pos_t blockstart;

	/* defaults */
	conf = "/etc/snapraid.conf";
	verbose = 0;
	blockstart = 0;

	opterr = 0;
	while ((c =
#if HAVE_GETOPT_LONG
		getopt_long(argc, argv, OPTIONS, long_options, 0))
#else
		getopt(argc, argv, OPTIONS))
#endif
	!= EOF) {
		switch (c) {
		case 'c' :
			conf = optarg;
			break;
		case 's' :
			if (stru(optarg, &blockstart) != 0) {
				fprintf(stderr, "Invalid start position '%s'\n", optarg);
				exit(EXIT_FAILURE);
			}
			break;
		case 'v' :
			verbose = 1;
			break;
		case 'h' :
			usage();
			exit(EXIT_SUCCESS);
		case 'V' :
			version();
			exit(EXIT_SUCCESS);
		default:
			fprintf(stderr, "Unknown option '%c'\n", (char)c);
			exit(EXIT_FAILURE);
		}
	}

	if (optind + 1 != argc) {
		usage();
		exit(EXIT_FAILURE);
	}

	if (strcmp(argv[optind], "sync") == 0) {
		operation = OPERATION_SYNC;
	} else if (strcmp(argv[optind], "check") == 0) {
		operation = OPERATION_CHECK;
	} else  if (strcmp(argv[optind], "fix") == 0) {
		operation = OPERATION_FIX;
	} else {
		fprintf(stderr, "Unknown command '%s'\n", argv[optind]);
		exit(EXIT_FAILURE);
	}

	struct sigaction sig;
	sig.sa_handler = signal_handler;
	sigemptyset(&sig.sa_mask);
	sig.sa_flags = 0;
	sigaction(SIGINT, &sig, 0);

	state_init(&state);

	state_config(&state, conf, verbose);

	if (operation == OPERATION_SYNC) {
		state_read(&state);

		state_scan(&state);

		state_sync(&state, blockstart);

		state_write(&state);
	} else if (operation == OPERATION_CHECK) {
		state_read(&state);

		state_check(&state, blockstart);
	}

	state_done(&state);

	return EXIT_SUCCESS;
}

