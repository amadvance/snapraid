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

/**
 * Max length of a line in the configuration and state files.
 */
#define TEXT_LINE_MAX 1024

void state_init(struct snapraid_state* state)
{
	state->verbose = 0;
	state->block_size = 128*1024; /* default 128 kB */
	state->content[0] = 0;
	state->parity[0] = 0;
	tommy_array_init(&state->diskarr);
	tommy_list_init(&state->excludelist);

	if (sizeof(off_t) < sizeof(uint64_t)) {
		fprintf(stderr, "Missing support for large files\n");
		exit(EXIT_FAILURE);
	}
}

void state_done(struct snapraid_state* state)
{
	unsigned i;

	for(i=0;i<tommy_array_size(&state->diskarr);++i)
		disk_free(tommy_array_get(&state->diskarr, i));
	tommy_array_done(&state->diskarr);
	tommy_list_foreach(&state->excludelist, (tommy_foreach_func*)filter_free);
}

void state_config(struct snapraid_state* state, const char* path, int verbose, int force)
{
	FILE* f;
	unsigned line;

	state->verbose = verbose;
	state->force = force;

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
		char buffer[TEXT_LINE_MAX];
		char* tag;
		char* s;
		int ret;

		++line;

		ret = strgets(buffer, TEXT_LINE_MAX, f);
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
			ret = stru32(s, &state->block_size);
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
			pathcpy(state->parity, sizeof(state->parity), s);
		} else if (strcmp(tag, "content") == 0) {
			if (*state->content) {
				fprintf(stderr, "Multiple 'content' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}
			pathcpy(state->content, sizeof(state->content), s);
		} else if (strcmp(tag, "disk") == 0) {
			char* name = s;
			s = strtoken(s);
			tommy_array_insert(&state->diskarr, disk_alloc(name, s));
		} else if (strcmp(tag, "exclude") == 0) {
			struct snapraid_filter* filter = filter_alloc(s);
			if (!filter) {
				fprintf(stderr, "Invalid 'exclude' specification '%s' in '%s' at line %u\n", s, path, line);
				exit(EXIT_FAILURE);
			}
			tommy_list_insert_tail(&state->excludelist, &filter->node, filter);
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

	pathcpy(path, sizeof(path), state->content);
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
		char buffer[TEXT_LINE_MAX];
		char* tag;
		char* s;
		int ret;

		++line;

		ret = strgets(buffer, TEXT_LINE_MAX, f);
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
			data_off_t v_size;
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
			block_off_t v_pos;
			struct snapraid_block* block;
			char* e;

			if (!file) {
				fprintf(stderr, "Unexpected 'blk' specification in '%s' at line %u\n", path, line);
				exit(EXIT_FAILURE);
			}

			pos = s;
			s = strtoken(s);
			hash = s;

			ret = stru32(pos, &v_pos);
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

	pathprint(path, sizeof(path), "%s.tmp", state->content);
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
			block_off_t k;
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

#if HAVE_FSYNC    
	if (fsync(fileno(f)) != 0) {
		fprintf(stderr, "Error writing the state file '%s' in fsync(). %s.\n", path, strerror(errno));
		exit(EXIT_FAILURE);
	}
#endif

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

#define PROGRESS_CLEAR "          "

int state_progress(time_t* start, time_t* last, block_off_t blockpos, block_off_t blockmax, data_off_t count_block, data_off_t count_size)
{
	time_t now;

	now = time(0);

	if (*last != now) {
		time_t delta = now - *start;

		printf("%u%%, %u MB", blockpos * 100 / blockmax, (unsigned)(count_size / (1024*1024)));

		if (delta != 0) {
			printf(", %u MB/s", (unsigned)(count_size / (1024*1024) / delta));
		}

		if (delta > 5 && count_block > 0) {
			unsigned m, h;
			data_off_t todo = blockmax - blockpos;

			m = todo * delta / (60 * count_block);

			h = m / 60;
			m = m % 60;

			printf(", %u:%02u ETA%s", h, m, PROGRESS_CLEAR);
		}
		printf("\r");
		fflush(stdout);
		*last = now;
	}


	/* stop if requested */
	if (global_interrupt) {
		printf("\rStopping for interruption%s\n", PROGRESS_CLEAR);
		return 1;
	}

	return 0;
}

