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

int handle_close_if_different(struct snapraid_handle* handle, struct snapraid_file* file)
{
	int ret;
	
	/* if it's the same file, nothing to do */
	if (handle->file == file) {
		return 0;
	}

	/* reset the file */
	handle->file = 0;

	/* close if already open */
	if (handle->f != -1) {
		ret = close(handle->f);
		if (ret != 0) {
			/* invalidate for error */
			handle->f = -1;

			fprintf(stderr, "Error closing file '%s'. %s.\n", handle->file->sub, strerror(errno));
			return -1;
		}
	}

	/* reset the descriptor */
	handle->f = -1;

	return 0;
}

static int handle_ancestor(const char* file)
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
	handle->f = open(handle->path, O_RDWR | O_CREAT | O_BINARY | O_SEQUENTIAL, 0600);
	if (handle->f == -1) {
		/* invalidate for error */
		handle->file = 0;
		handle->f = -1;

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

	if (handle->st.st_size < file->size) {
#if HAVE_POSIX_FALLOCATE
		/* allocate real space */
		ret = posix_fallocate(handle->f, 0, file->size);
#else
		/* allocate using a sparse file */
		ret = ftruncate(handle->f, file->size);
#endif
		if (ret != 0) {
			if (errno == ENOSPC) {
				fprintf(stderr, "Failed to grow file '%s' due lack of space.\n", handle->path);
			} else {
				fprintf(stderr, "Error growing file '%s'. %s.\n", handle->path, strerror(errno));
			}
			return -1;
		}
	} else if (handle->st.st_size > file->size) {
		ret = ftruncate(handle->f, file->size);
		if (ret != 0) {
			fprintf(stderr, "Error truncating file '%s'. %s.\n", handle->path, strerror(errno));
			return -1;
		}
	}

#if HAVE_POSIX_FADVISE
	/* advise sequential access */
	ret = posix_fadvise(handle->f, 0, 0, POSIX_FADV_SEQUENTIAL);
	if (ret != 0) {
		fprintf(stderr, "Error advising file '%s'. %s.\n", handle->path, strerror(errno));
		return -1;
	}
#endif

	/* return the warning condition of a larger file */
	if (handle->st.st_size > file->size) {
		fprintf(stderr, "File '%s' truncated to size %"PRIu64".\n", handle->path, file->size);
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

	/* opening in sequential mode in Windows */
	pathprint(handle->path, sizeof(handle->path), "%s%s", handle->disk->dir, file->sub);
	handle->f = open(handle->path, O_RDONLY | O_BINARY | O_SEQUENTIAL);

	if (handle->f == -1) {
		/* invalidate for error */
		handle->file = 0;
		handle->f = -1;

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

#if HAVE_POSIX_FADVISE
	/* advise sequential access */
	ret = posix_fadvise(handle->f, 0, 0, POSIX_FADV_SEQUENTIAL);
	if (ret != 0) {
		fprintf(stderr, "Error advising file '%s'. %s.\n", handle->path, strerror(errno));
		return -1;
	}
#endif

	return 0;
}

int handle_close(struct snapraid_handle* handle)
{
	int ret;

	if (handle->f != -1) {
		ret = close(handle->f);
		if (ret != 0) {
			/* invalidate for error */
			handle->file = 0;
			handle->f = -1;

			fprintf(stderr, "Error closing file '%s'. %s.\n", handle->file->sub, strerror(errno));
			return -1;
		}
	}

	handle->file = 0;
	handle->f = -1;

	return 0;
}

int handle_read(struct snapraid_handle* handle, struct snapraid_block* block, unsigned char* block_buffer, unsigned block_size)
{
	ssize_t read_ret;
	data_off_t offset;
	unsigned read_size;

	offset = block_file_pos(block) * (data_off_t)block_size;

	read_size = block_file_size(block, block_size);

#if HAVE_PREAD
	read_ret = pread(handle->f, block_buffer, read_size, offset);
#else
	if (lseek(handle->f, offset, SEEK_SET) != offset) {
		fprintf(stderr, "Error seeking file '%s'. %s.\n", handle->path, strerror(errno));
		return -1;
	}

	read_ret = read(handle->f, block_buffer, read_size);
#endif
	if (read_ret != (ssize_t)read_size) { /* conversion is safe because block_size is always small */
		fprintf(stderr, "Error reading file '%s'. %s.\n", handle->path, strerror(errno));
		return -1;
	}

	/* pad with 0 */
	if (read_size < block_size) {
		memset(block_buffer + read_size, 0, block_size - read_size);
	}

	/* Here isn't needed to call posix_fadvise(..., POSIX_FADV_DONTNEED) because */
	/* we already advised sequential access with POSIX_FADV_SEQUENTIAL. */
	/* In Linux 2.6.33 it's enough to ensure that data is not kept in the cache. */
	/* Better to do nothing and save a syscall for each block. */

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

	/* Here doesn't make sense to call posix_fadvise(..., POSIX_FADV_DONTNEED) because */
	/* at this time the data is still in not yet written and it cannot be discharged. */

	return 0;
}


