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
		if (fnmatch(filter->pattern + 1, path, FNM_PATHNAME) == 0)
			return filter->direction;
	} else {
		if (fnmatch(filter->pattern, name, 0) == 0)
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
			/* using fnmatch allows to ignore case in windows */
			if (fnmatch(filter->pattern, disk, 0) == 0)
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

int filter_content(tommy_list* contentlist, const char* path)
{
	tommy_node* i;

	for(i=tommy_list_head(contentlist);i!=0;i=i->next) {
		struct snapraid_content* content = i->data;
		char tmp[PATH_MAX];

		/* FNM_PATHNAME because we are comparing full path */
		/* FNM_NOESCAPE because the pattern is also a path, surely without escape chars */
		/* using fnmatch allows to ignore case in windows */
		if (fnmatch(content->content, path, FNM_PATHNAME | FNM_NOESCAPE) == 0)
			return -1;

		/* exclude also the ".tmp" copy used to save it */
		pathprint(tmp, sizeof(tmp), "%s.tmp", content->content);
		if (fnmatch(tmp, path, FNM_PATHNAME | FNM_NOESCAPE) == 0)
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

struct snapraid_file* file_alloc(unsigned block_size, const char* sub, uint64_t size, uint64_t mtime_sec, int mtime_nsec, uint64_t inode)
{
	struct snapraid_file* file;
	block_off_t i;

	file = malloc_nofail(sizeof(struct snapraid_file));
	pathcpy(file->sub, sizeof(file->sub), sub);
	file->size = size;
	file->blockmax = (size + block_size - 1) / block_size;
	file->mtime_sec = mtime_sec;
	file->mtime_nsec = mtime_nsec;
	file->inode = inode;
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

int file_path_compare(const void* void_arg, const void* void_data)
{
	const char* arg = void_arg;
	const struct snapraid_file* file = void_data;
	return strcmp(arg, file->sub);
}

struct snapraid_link* link_alloc(const char* sub, const char* linkto, int is_hardlink)
{
	struct snapraid_link* link;

	link = malloc_nofail(sizeof(struct snapraid_link));
	pathcpy(link->sub, sizeof(link->sub), sub);
	pathcpy(link->linkto, sizeof(link->linkto), linkto);
	link->flag = is_hardlink != 0 ? FILE_IS_HARDLINK : 0;

	return link;
}

void link_free(struct snapraid_link* link)
{
	free(link);
}

int link_name_compare(const void* void_arg, const void* void_data)
{
	const char* arg = void_arg;
	const struct snapraid_link* link = void_data;
	return strcmp(arg, link->sub);
}

struct snapraid_dir* dir_alloc(const char* sub)
{
	struct snapraid_dir* dir;

	dir = malloc_nofail(sizeof(struct snapraid_dir));
	pathcpy(dir->sub, sizeof(dir->sub), sub);
	dir->flag = 0;

	return dir;
}

void dir_free(struct snapraid_dir* dir)
{
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
	tommy_list_init(&disk->filelist);
	tommy_hashdyn_init(&disk->inodeset);
	tommy_hashdyn_init(&disk->pathset);
	tommy_list_init(&disk->linklist);
	tommy_hashdyn_init(&disk->linkset);
	tommy_list_init(&disk->dirlist);
	tommy_hashdyn_init(&disk->dirset);
	tommy_array_init(&disk->blockarr);

	return disk;
}

void disk_free(struct snapraid_disk* disk)
{
	tommy_list_foreach(&disk->filelist, (tommy_foreach_func*)file_free);
	tommy_hashdyn_done(&disk->inodeset);
	tommy_hashdyn_done(&disk->pathset);
	tommy_list_foreach(&disk->linklist, (tommy_foreach_func*)link_free);
	tommy_hashdyn_done(&disk->linkset);
	tommy_list_foreach(&disk->dirlist, (tommy_foreach_func*)dir_free);
	tommy_hashdyn_done(&disk->dirset);
	tommy_array_done(&disk->blockarr);
	free(disk);
}

struct snapraid_map* map_alloc(const char* name, unsigned position)
{
	struct snapraid_map* map;

	map = malloc_nofail(sizeof(struct snapraid_map));
	pathcpy(map->name, sizeof(map->name), name);
	map->position = position;

	return map;
}

void map_free(struct snapraid_map* map)
{
	free(map);
}

