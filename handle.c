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
#include "handle.h"

/****************************************************************************/
/* handle */

int handle_ancestor(const char* file)
{
	char dir[PATH_MAX];
	char* c;
	
	pathcpy(dir, sizeof(dir), file);
	
	c = strrchr(dir, '/');
	if (!c) {
		/* no ancestor */
		return 0;
	}

	/* clear the file */
	*c = 0;

	if (*dir == 0) {
		/* nothing more to do */
		return 0;
	}

	if (access(dir, F_OK) == 0) {
		/* the directory/file exists */
		return 0;
	}

	/* recursively create them all */
	if (handle_ancestor(dir) != 0) {
		return -1;
	}

	/* create it */
	if (mkdir(dir, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) != 0) {
		fprintf(stderr, "Error creating directory '%s'. %s.\n", dir, strerror(errno));	
		return -1;
	}
	
	return 0;
}

int handle_create(struct snapraid_handle* handle, struct snapraid_file* file)
{
	int ret;

	/* if it's the same file, and already opened, nothing to do */
	if (handle->file == file && handle->f != -1) {
		return 0;
	}

	pathprint(handle->path, sizeof(handle->path), "%s%s", handle->disk->dir, file->sub);

	ret = handle_ancestor(handle->path);
	if (ret != 0) {
		return -1;
	}

	/* opening in sequential mode in Windows */
	/* do not follow links to ensure to open the real file */
	handle->f = open(handle->path, O_RDWR | O_CREAT | O_BINARY | O_SEQUENTIAL | O_NOFOLLOW, 0600);

	/* if failed for missing permission */
	if (handle->f == -1 && errno == EACCES) {
		/* retry without requesting write permission */
		handle->f = open(handle->path, O_RDONLY | O_BINARY | O_SEQUENTIAL | O_NOFOLLOW);
	}

	if (handle->f == -1) {
		/* invalidate for error */
		handle->file = 0;
		handle->f = -1;
		handle->valid_size = 0;

		fprintf(stderr, "Error opening file '%s'. %s.\n", handle->path, strerror(errno));
		return -1;
	}

	/* just opened */
	handle->file = file;

	/* get the stat info */
	ret = fstat(handle->f, &handle->st);
	if (ret != 0) {
		fprintf(stderr, "Error accessing file '%s'. %s.\n", handle->path, strerror(errno));
		return -1;
	}

	/* get the size of the exising data */
	handle->valid_size = handle->st.st_size;

	/* Here we only truncate the file and we don't grow it */
	/* as allocating real space for it would imply a too big performance drop, */
	/* and allocating sparse space wouldn't improve the safety of subsequent writes. */
	if (handle->st.st_size > file->size) {
		ret = ftruncate(handle->f, file->size);
		if (ret != 0) {
			if (errno == EACCES) {
				fprintf(stderr, "Failed to truncate file '%s' for missing write permission.\n", handle->path);
			} else {
				fprintf(stderr, "Error truncating file '%s'. %s.\n", handle->path, strerror(errno));
			}
			return -1;
		}

		/* adjust the size to the truncated size */
		handle->valid_size = file->size;
	}

#if HAVE_POSIX_FADVISE
	/* advise sequential access */
	ret = posix_fadvise(handle->f, 0, 0, POSIX_FADV_SEQUENTIAL);
	if (ret != 0) {
		fprintf(stderr, "Error advising file '%s'. %s.\n", handle->path, strerror(ret));
		return -1;
	}
#endif

	/* return if the file size was truncated */
	if (handle->st.st_size > file->size) {
		return 1;
	}

	return 0;
}

int handle_open(struct snapraid_handle* handle, struct snapraid_file* file)
{
	int ret;

	/* if already opened, nothing to do */
	if (handle->file == file && handle->f != -1) {
		return 0;
	}

	pathprint(handle->path, sizeof(handle->path), "%s%s", handle->disk->dir, file->sub);
	
	/* opening in sequential mode in Windows */
	/* do not follow links to ensure to open the real file */
	handle->f = open(handle->path, O_RDONLY | O_BINARY | O_SEQUENTIAL | O_NOFOLLOW);

	if (handle->f == -1) {
		/* invalidate for error */
		handle->file = 0;
		handle->f = -1;
		handle->valid_size = 0;

		fprintf(stderr, "Error opening file '%s'. %s.\n", handle->path, strerror(errno));
		return -1;
	}

	/* just opened */
	handle->file = file;

	/* get the stat info */
	ret = fstat(handle->f, &handle->st);
	if (ret != 0) {
		fprintf(stderr, "Error accessing file '%s'. %s.\n", handle->path, strerror(errno));
		return -1;
	}

	/* get the size of the exising data */
	handle->valid_size = handle->st.st_size;

#if HAVE_POSIX_FADVISE
	/* advise sequential access */
	ret = posix_fadvise(handle->f, 0, 0, POSIX_FADV_SEQUENTIAL);
	if (ret != 0) {
		fprintf(stderr, "Error advising file '%s'. %s.\n", handle->path, strerror(ret));
		return -1;
	}
#endif

	return 0;
}

int handle_close(struct snapraid_handle* handle)
{
	int ret;

	/* close if open */
	if (handle->f != -1) {
		ret = close(handle->f);
		if (ret != 0) {
			/* invalidate for error */
			handle->file = 0;
			handle->f = -1;
			handle->valid_size = 0;

			fprintf(stderr, "Error closing file '%s'. %s.\n", handle->file->sub, strerror(errno));
			return -1;
		}
	}

	/* reset the descriptor */
	handle->file = 0;
	handle->f = -1;
	handle->valid_size = 0;

	return 0;
}

int handle_read(struct snapraid_handle* handle, struct snapraid_block* block, unsigned char* block_buffer, unsigned block_size)
{
	ssize_t read_ret;
	data_off_t offset;
	unsigned read_size;
	unsigned count;

	offset = block_file_pos(block) * (data_off_t)block_size;

	/* check if we are going to read only not initialized data */
	if (offset >= handle->valid_size) {
		fprintf(stderr, "Reading missing data from file '%s' at offset %"PRIu64".\n", handle->path, offset);
		return -1;
	}

	read_size = block_file_size(block, block_size);

#if !HAVE_PREAD
	if (lseek(handle->f, offset, SEEK_SET) != offset) {
		fprintf(stderr, "Error seeking file '%s' at offset %"PRIu64". %s.\n", handle->path, offset, strerror(errno));
		return -1;
	}
#endif

	count = 0;
	do {

#if HAVE_PREAD
		read_ret = pread(handle->f, block_buffer + count, read_size - count, offset + count);
#else
		read_ret = read(handle->f, block_buffer + count, read_size - count);
#endif

		if (read_ret < 0) {
			fprintf(stderr, "Error reading file '%s' at offset %"PRIu64". %s.\n", handle->path, offset, strerror(errno));
			return -1;
		}
		if (read_ret == 0) {
			fprintf(stderr, "Unexpected end of file '%s' at offset %"PRIu64". %s.\n", handle->path, offset, strerror(errno));
			return -1;
		}

		count += read_ret;
	} while (count < read_size);
		

	/* pad with 0 */
	if (read_size < block_size) {
		memset(block_buffer + read_size, 0, block_size - read_size);
	}

	/* Here isn't needed to call posix_fadvise(..., POSIX_FADV_DONTNEED) */
	/* because we already advised sequential access with POSIX_FADV_SEQUENTIAL. */
	/* In Linux 2.6.33 it's enough to ensure that data is not kept in the cache. */
	/* Better to do nothing and save a syscall for each block. */

	/* Here we cannot call posix_fadvise(..., POSIX_FADV_WILLNEED) for the next block */
	/* because it may be blocking */
	/* See Ted Ts'o comment in "posix_fadvise(POSIX_FADV_WILLNEED) waits before returning?" */
	/* at: https://lkml.org/lkml/2010/12/6/122 */
	/* We relay only on the automatic readahead triggered by POSIX_FADV_SEQUENTIAL. */

	return read_size;
}

int handle_write(struct snapraid_handle* handle, struct snapraid_block* block, unsigned char* block_buffer, unsigned block_size)
{
	ssize_t write_ret;
	data_off_t offset;
	unsigned write_size;

	offset = block_file_pos(block) * (data_off_t)block_size;

	write_size = block_file_size(block, block_size);

#if HAVE_PWRITE
	write_ret = pwrite(handle->f, block_buffer, write_size, offset);
#else
	if (lseek(handle->f, offset, SEEK_SET) != offset) {
		fprintf(stderr, "Error seeking file '%s'. %s.\n", handle->path, strerror(errno));
		return -1;
	}

	write_ret = write(handle->f, block_buffer, write_size);
#endif
	if (write_ret != (ssize_t)write_size) { /* conversion is safe because block_size is always small */
		fprintf(stderr, "Error writing file '%s'. %s.\n", handle->path, strerror(errno));
		return -1;
	}

	/* adjust the size of the valid data */
	if (handle->valid_size < offset + write_size) {
		handle->valid_size = offset + write_size;
	}

	/* Here doesn't make sense to call posix_fadvise(..., POSIX_FADV_DONTNEED) because */
	/* at this time the data is still in not yet written and it cannot be discharged. */

	return 0;
}

int handle_utime(struct snapraid_handle* handle)
{
	struct timeval tv[2];
	int ret;

	/* do nothing if not opened */
	if (handle->f == -1)
		return 0;

	tv[0].tv_sec = handle->file->mtime;
	tv[0].tv_usec = 0;

	tv[1].tv_sec = tv[0].tv_sec;
	tv[1].tv_usec = tv[0].tv_usec;

	ret = futimes(handle->f, tv);
	if (ret != 0) {
		fprintf(stderr, "Error timing file '%s'. %s.\n", handle->file->sub, strerror(errno));
		return -1;
	}

	return 0;
}

struct snapraid_handle* handle_map(struct snapraid_state* state, unsigned* handlemax)
{
	tommy_node* i;
	unsigned j;
	unsigned size = 0;
	struct snapraid_handle* handle;

	/* get the size of the mapping */
	size = 0;
	for(i=state->maplist;i!=0;i=i->next) {
		struct snapraid_map* map = i->data;
		if (map->position > size)
			size = map->position;
	}
	++size; /* size is one more than the max */

	handle = malloc_nofail(size * sizeof(struct snapraid_handle));

	for(j=0;j<size;++j) {
		/* default for empty position */
		handle[j].disk = 0;
		handle[j].file = 0;
		handle[j].f = -1;
		handle[j].valid_size = 0;
	}

	/* set the vector */
	for(i=state->disklist;i!=0;i=i->next) {
		struct snapraid_map* map;
		struct snapraid_disk* disk = i->data;
		tommy_node* k;

		/* search the mapping for this disk */
		for(k=state->maplist;k!=0;k=k->next) {
			map = k->data;
			if (strcmp(disk->name, map->name) == 0)
				break;
		}
		if (k==0) {
			fprintf(stderr, "Inconsistent disk mapping.\n");
			exit(EXIT_FAILURE);
			continue;
		}

		handle[map->position].disk = disk;
	}

	*handlemax = size;
	return handle;
}

