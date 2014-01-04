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

struct snapraid_content* content_alloc(const char* path, uint64_t dev)
{
	struct snapraid_content* content;

	content = malloc_nofail(sizeof(struct snapraid_content));
	pathimport(content->content, sizeof(content->content), path);
	content->device = dev;

	return content;
}

void content_free(struct snapraid_content* content)
{
	free(content);
}

struct snapraid_filter* filter_alloc_file(int direction, const char* pattern)
{
	struct snapraid_filter* filter;
	char* i;
	char* first;
	char* last;

	filter = malloc_nofail(sizeof(struct snapraid_filter));
	pathimport(filter->pattern, sizeof(filter->pattern), pattern);
	filter->direction = direction;

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

	/* it's a file filter */
	filter->is_disk = 0;

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

struct snapraid_filter* filter_alloc_disk(int direction, const char* pattern)
{
	struct snapraid_filter* filter;

	filter = malloc_nofail(sizeof(struct snapraid_filter));
	pathimport(filter->pattern, sizeof(filter->pattern), pattern);
	filter->direction = direction;

	/* it's a disk filter */
	filter->is_disk = 1;
	filter->is_path = 0;
	filter->is_dir = 0;

	/* no slash allowed in disk names */
	if (strchr(filter->pattern, '/') != 0) {
		free(filter);
		return 0;
	}

	return filter;
}

void filter_free(struct snapraid_filter* filter)
{
	free(filter);
}

static int filter_apply(struct snapraid_filter* filter, const char* path, const char* name, int is_dir)
{
	/* matches dirs with dirs and files with files */
	if (filter->is_dir && !is_dir)
		return 0;
	if (!filter->is_dir && is_dir)
		return 0;

	if (filter->is_path) {
		/* skip initial slash, as always missing from the path */
		if (fnmatch(filter->pattern + 1, path, FNM_PATHNAME | FNM_CASEINSENSITIVE_FOR_WIN) == 0)
			return filter->direction;
	} else {
		if (fnmatch(filter->pattern, name, FNM_CASEINSENSITIVE_FOR_WIN) == 0)
			return filter->direction;
	}

	return 0;
}

static int filter_recurse(struct snapraid_filter* filter, const char* const_path, int is_dir)
{
	char path[PATH_MAX];
	char* name;
	unsigned i;

	pathcpy(path, sizeof(path), const_path);

	/* filter for all the directories */
	name = path;
	for(i=0;path[i] != 0;++i) {
		if (path[i] == '/') {
			/* set a terminator */
			path[i] = 0;

			/* filter the directory */
			if (filter_apply(filter, path, name, 1) != 0)
				return filter->direction;

			/* restore the slash */
			path[i] = '/';

			/* next name */
			name = path + i + 1;
		}
	}

	/* filter the final file */
	if (filter_apply(filter, path, name, is_dir) != 0)
		return filter->direction;

	return 0;
}

static int filter_element(tommy_list* filterlist, const char* disk, const char* sub, int is_dir)
{
	tommy_node* i;

	int direction = 1; /* by default include all */

	/* for each filter */
	for(i=tommy_list_head(filterlist);i!=0;i=i->next) {
		int ret;
		struct snapraid_filter* filter = i->data;

		if (filter->is_disk) {
			if (fnmatch(filter->pattern, disk, FNM_CASEINSENSITIVE_FOR_WIN) == 0)
				ret = filter->direction;
			else
				ret = 0;
		} else {
			ret = filter_recurse(filter, sub, is_dir);
		}

		if (ret > 0) {
			/* include the file */
			return 0;
		} else if (ret < 0) {
			/* exclude the file */
			return -1;
		} else {
			/* default is opposite of the last filter */
			direction = -filter->direction;
			/* continue with the next one */
		}
	}

	/* directories are always included by default, otherwise we cannot apply rules */
	/* to the contained files */
	if (is_dir)
		return 0;

	/* files are excluded/included depending of the last rule processed */
	if (direction < 0)
		return -1;

	return 0;
}

int filter_path(tommy_list* filterlist, const char* disk, const char* sub)
{
	return filter_element(filterlist, disk, sub, 0);
}

int filter_dir(tommy_list* filterlist, const char* disk, const char* sub)
{
	return filter_element(filterlist, disk, sub, 1);
}

int filter_existence(int filter_missing, const char* dir, const char* sub)
{
	char path[PATH_MAX];
	struct stat st;

	if (!filter_missing)
		return 0;

	/* we directly check if in the disk the file is present or not */
	pathprint(path, sizeof(path), "%s%s", dir, sub);
	
	if (lstat(path, &st) != 0) {
		/* if the file doesn't exist, we don't filter it out */
		if (errno == ENOENT)
			return 0;
		fprintf(stderr, "Error in stat file '%s'. %s.\n", path, strerror(errno));
		exit(EXIT_FAILURE);
	}

	/* the file is present, so we filter it out */
	return 1;
}

int filter_correctness(int filter_error, tommy_arrayblkof* infoarr, struct snapraid_file* file)
{
	unsigned i;

	if (!filter_error)
		return 0;

	/* check each block of the file */
	for(i=0;i<file->blockmax;++i) {
		struct snapraid_block* block;
		snapraid_info info;

		block = &file->blockvec[i];
		
		info = info_get(infoarr, block->parity_pos);

		/* if the file has a bad block, don't exclude it */
		if (info_get_bad(info))
			return 0;
	}

	/* the file is correct, so we filter it out */
	return 1;
}

int filter_content(tommy_list* contentlist, const char* path)
{
	tommy_node* i;

	for(i=tommy_list_head(contentlist);i!=0;i=i->next) {
		struct snapraid_content* content = i->data;
		char tmp[PATH_MAX];

		if (pathcmp(content->content, path) == 0)
			return -1;

		/* exclude also the ".tmp" copy used to save it */
		pathprint(tmp, sizeof(tmp), "%s.tmp", content->content);
		if (pathcmp(tmp, path) == 0)
			return -1;
	}

	return 0;
}

block_off_t block_file_pos(struct snapraid_block* block)
{
	struct snapraid_file* file = block_file_get(block);

	if (block < file->blockvec || block >= file->blockvec + file->blockmax) {
		fprintf(stderr, "Internal inconsistency in block %u ownership\n", block->parity_pos);
		exit(EXIT_FAILURE);
	}

	return block - file->blockvec;
}

int block_is_last(struct snapraid_block* block)
{
	struct snapraid_file* file = block_file_get(block);

	return block == file->blockvec + file->blockmax - 1;
}

unsigned block_file_size(struct snapraid_block* block, unsigned block_size)
{
	block_off_t pos = block_file_pos(block);

	/* if it's the last block */
	if (pos + 1 == block_file_get(block)->blockmax) {
		unsigned remainder;
		if (block_file_get(block)->size == 0)
			return 0;
		remainder = block_file_get(block)->size % block_size;
		if (remainder == 0)
			remainder = block_size;
		return remainder;
	}

	return block_size;
}

struct snapraid_deleted* deleted_alloc(void)
{
	struct snapraid_deleted* deleted;

	deleted = malloc_nofail(sizeof(struct snapraid_deleted));

	/* set the state as deleted */
	deleted->block.file_mixed = BLOCK_STATE_DELETED;

	return deleted;
}

struct snapraid_deleted* deleted_dup(struct snapraid_block* block)
{
	struct snapraid_deleted* deleted;

	deleted = deleted_alloc();

	/* copy data from deleted block */
	deleted->block.parity_pos = block->parity_pos;
	memcpy(deleted->block.hash, block->hash, HASH_SIZE);

	return deleted;
}

void deleted_free(struct snapraid_deleted* deleted)
{
	free(deleted);
}

struct snapraid_file* file_alloc(unsigned block_size, const char* sub, uint64_t size, uint64_t mtime_sec, int mtime_nsec, uint64_t inode, uint64_t physical)
{
	struct snapraid_file* file;
	block_off_t i;

	file = malloc_nofail(sizeof(struct snapraid_file));
	file->sub = strdup_nofail(sub);
	file->size = size;
	file->blockmax = (size + block_size - 1) / block_size;
	file->mtime_sec = mtime_sec;
	file->mtime_nsec = mtime_nsec;
	file->inode = inode;
	file->physical = physical;
	file->flag = 0;
	file->blockvec = malloc_nofail(file->blockmax * sizeof(struct snapraid_block));

	/* set the back pointer */
	for(i=0;i<file->blockmax;++i) {
		file->blockvec[i].parity_pos = POS_INVALID;
		file->blockvec[i].file_mixed = 0;
		block_file_set(&file->blockvec[i], file);
	}

	return file;
}

void file_free(struct snapraid_file* file)
{
	free(file->sub);
	free(file->blockvec);
	free(file);
}

void file_rename(struct snapraid_file* file, const char* sub)
{
	free(file->sub);
	file->sub = strdup_nofail(sub);
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

int file_inode_compare_to_arg(const void* void_arg, const void* void_data)
{
	const uint64_t* arg = void_arg;
	const struct snapraid_file* file = void_data;
	if (*arg < file->inode)
		return -1;
	if (*arg > file->inode)
		return 1;
	return 0;
}

int file_inode_compare(const void* void_a, const void* void_b)
{
	const struct snapraid_file* file_a = void_a;
	const struct snapraid_file* file_b = void_b;
	if (file_a->inode < file_b->inode)
		return -1;
	if (file_a->inode > file_b->inode)
		return 1;
	return 0;
}

int file_alpha_compare(const void* void_a, const void* void_b)
{
	const struct snapraid_file* file_a = void_a;
	const struct snapraid_file* file_b = void_b;
	return strcmp(file_a->sub, file_b->sub);
}

int file_physical_compare(const void* void_a, const void* void_b)
{
	const struct snapraid_file* file_a = void_a;
	const struct snapraid_file* file_b = void_b;
	if (file_a->physical < file_b->physical)
		return -1;
	if (file_a->physical > file_b->physical)
		return 1;
	return 0;
}

int file_path_compare(const void* void_arg, const void* void_data)
{
	const char* arg = void_arg;
	const struct snapraid_file* file = void_data;
	return strcmp(arg, file->sub);
}

struct snapraid_link* link_alloc(const char* sub, const char* linkto, unsigned link_flag)
{
	struct snapraid_link* link;

	link = malloc_nofail(sizeof(struct snapraid_link));
	link->sub = strdup_nofail(sub);
	link->linkto = strdup_nofail(linkto);
	link->flag = link_flag;

	return link;
}

void link_free(struct snapraid_link* link)
{
	free(link->sub);
	free(link->linkto);
	free(link);
}

int link_name_compare_to_arg(const void* void_arg, const void* void_data)
{
	const char* arg = void_arg;
	const struct snapraid_link* link = void_data;
	return strcmp(arg, link->sub);
}

int link_alpha_compare(const void* void_a, const void* void_b)
{
	const struct snapraid_link* link_a = void_a;
	const struct snapraid_link* link_b = void_b;
	return strcmp(link_a->sub, link_b->sub);
}

struct snapraid_dir* dir_alloc(const char* sub)
{
	struct snapraid_dir* dir;

	dir = malloc_nofail(sizeof(struct snapraid_dir));
	dir->sub = strdup_nofail(sub);
	dir->flag = 0;

	return dir;
}

void dir_free(struct snapraid_dir* dir)
{
	free(dir->sub);
	free(dir);
}

int dir_name_compare(const void* void_arg, const void* void_data)
{
	const char* arg = void_arg;
	const struct snapraid_dir* dir = void_data;
	return strcmp(arg, dir->sub);
}

struct snapraid_disk* disk_alloc(const char* name, const char* dir, uint64_t dev)
{
	struct snapraid_disk* disk;

	disk = malloc_nofail(sizeof(struct snapraid_disk));
	pathcpy(disk->name, sizeof(disk->name), name);
	pathimport(disk->dir, sizeof(disk->dir), dir);

	/* ensure that the dir terminate with "/" if it isn't empty */
	pathslash(disk->dir, sizeof(disk->dir));

	disk->device = dev;
	disk->first_free_block = 0;
	disk->has_volatile_inodes = 0;
	disk->has_unreliable_physical = 0;
	disk->has_different_uuid = 0;
	disk->had_empty_uuid = 0;
	disk->mapping = -1;
	tommy_list_init(&disk->filelist);
	tommy_list_init(&disk->deletedlist);
	tommy_hashdyn_init(&disk->inodeset);
	tommy_hashdyn_init(&disk->pathset);
	tommy_list_init(&disk->linklist);
	tommy_hashdyn_init(&disk->linkset);
	tommy_list_init(&disk->dirlist);
	tommy_hashdyn_init(&disk->dirset);
	tommy_arrayblk_init(&disk->blockarr);

	return disk;
}

void disk_free(struct snapraid_disk* disk)
{
	tommy_list_foreach(&disk->filelist, (tommy_foreach_func*)file_free);
	tommy_list_foreach(&disk->deletedlist, (tommy_foreach_func*)deleted_free);
	tommy_hashdyn_done(&disk->inodeset);
	tommy_hashdyn_done(&disk->pathset);
	tommy_list_foreach(&disk->linklist, (tommy_foreach_func*)link_free);
	tommy_hashdyn_done(&disk->linkset);
	tommy_list_foreach(&disk->dirlist, (tommy_foreach_func*)dir_free);
	tommy_hashdyn_done(&disk->dirset);
	tommy_arrayblk_done(&disk->blockarr);
	free(disk);
}

int disk_is_empty(struct snapraid_disk* disk, block_off_t blockmax)
{
	block_off_t i;

	/* if there is an element, it's not empty */
	/* even if links and dirs have no block allocation */
	if (!tommy_list_empty(&disk->filelist))
		return 0;
	if (!tommy_list_empty(&disk->linklist))
		return 0;
	if (!tommy_list_empty(&disk->dirlist))
		return 0;

	/* limit the search to the size of the disk */
	if (blockmax > tommy_arrayblk_size(&disk->blockarr))
		blockmax = tommy_arrayblk_size(&disk->blockarr);

	/* checks all the blocks to search for deleted ones */
	/* this search is slow, but it's done only if no file is present */
	for(i=0;i<blockmax;++i) {
		struct snapraid_block* block = tommy_arrayblk_get(&disk->blockarr, i);
		unsigned block_state = block_state_get(block);

		switch (block_state) {
		case BLOCK_STATE_EMPTY :
			/* empty block are expected for an empty disk */
			break;
		case BLOCK_STATE_DELETED :
			/* if there is a deleted block, the disk is not empty */
			return 0;
		default:
			fprintf(stderr, "Internal inconsistency for used block in disk '%s' without files\n", disk->name);
			exit(EXIT_FAILURE);
		}
	}

	/* finally, it's empty */
	return 1;
}

struct snapraid_map* map_alloc(const char* name, unsigned position, const char* uuid)
{
	struct snapraid_map* map;

	map = malloc_nofail(sizeof(struct snapraid_map));
	pathcpy(map->name, sizeof(map->name), name);
	pathcpy(map->uuid, sizeof(map->uuid), uuid);
	map->position = position;

	return map;
}

void map_free(struct snapraid_map* map)
{
	free(map);
}

int info_time_compare(const void* void_a, const void* void_b)
{
	const snapraid_info* info_a = void_a;
	const snapraid_info* info_b = void_b;
	time_t time_a = info_get_time(*info_a);
	time_t time_b = info_get_time(*info_b);
	if (time_a < time_b)
		return -1;
	if (time_a > time_b)
		return 1;
	return 0;
}

