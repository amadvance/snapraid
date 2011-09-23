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
#include "parity.h"

/****************************************************************************/
/* parity */

block_off_t parity_resize(struct snapraid_state* state)
{
	block_off_t parity_block;
	tommy_node* i;

	/* compute the size of the parity file */
	parity_block = 0;
	for(i=state->disklist;i!=0;i=i->next) {
		struct snapraid_disk* disk = i->data;

		/* start from the declared size */
		block_off_t block = tommy_array_size(&disk->blockarr);

		/* decrease the block until an allocated block */
		while (block > parity_block && tommy_array_get(&disk->blockarr, block - 1) == 0)
			--block;

		/* get the highest value */
		if (block > parity_block)
			parity_block = block;
	}

	return parity_block;
}

int parity_create(const char* path, data_off_t size)
{
	struct stat st;
	int ret;
	int f;

	/* opening in sequential mode in Windows */
	f = open(path, O_RDWR | O_CREAT | O_BINARY | O_SEQUENTIAL, 0600);
	if (f == -1) {
		fprintf(stderr, "Error opening parity file '%s'. %s.\n", path, strerror(errno));
		return -1;
	}

	ret = fstat(f, &st);
	if (ret != 0) {
		fprintf(stderr, "Error accessing parity file '%s'. %s.\n", path, strerror(errno));
		goto bail;
	}

	if (st.st_size < size) {
#if HAVE_POSIX_FALLOCATE
		/* allocate real space */
		ret = posix_fallocate(f, 0, size);
#else
		/* allocate using a sparse file */
		ret = ftruncate(f, size);
#endif
		if (ret != 0) {
			if (errno == ENOSPC) {
				fprintf(stderr, "Failed to grow parity file '%s' to size %"PRIu64" due lack of space.\n", path, size);
			} else {
				fprintf(stderr, "Error growing parity file '%s' to size %"PRIu64". Do you have enough space? %s.\n", path, size, strerror(errno));
			}
			goto bail;
		}
	} else if (st.st_size > size) {
		ret = ftruncate(f, size);
		if (ret != 0) {
			fprintf(stderr, "Error truncating parity file '%s' to size %"PRIu64". %s.\n", path, size, strerror(errno));
			goto bail;
		}
	}

#if HAVE_POSIX_FADVISE
	/* advise sequential access */
	ret = posix_fadvise(f, 0, 0, POSIX_FADV_SEQUENTIAL);
	if (ret != 0) {
		fprintf(stderr, "Error advising parity file '%s'. %s.\n", path, strerror(errno));
		goto bail;
	}
#endif

	return f;

bail:
	close(f);
	return -1;
}

int parity_open(const char* path)
{
#if HAVE_POSIX_FADVISE
	int ret;
#endif    
	int f;

	/* opening in sequential mode in Windows */
	f = open(path, O_RDONLY | O_BINARY | O_SEQUENTIAL);
	if (f == -1) {
		fprintf(stderr, "Error opening parity file '%s'. %s.\n", path, strerror(errno));
		return -1;
	}

#if HAVE_POSIX_FADVISE
	/* advise sequential access */
	ret = posix_fadvise(f, 0, 0, POSIX_FADV_SEQUENTIAL);
	if (ret != 0) {
		fprintf(stderr, "Error advising parity file '%s'. %s.\n", path, strerror(errno));
		goto bail;
	}
#endif

	return f;

#if HAVE_POSIX_FADVISE
bail:
	close(f);
	return -1;
#endif
}

int parity_sync(const char* path, int f)
{
#if HAVE_FSYNC
	int ret;

	/* Ensure that data changes are written to disk. */
	/* This is required to ensure that parity is more updated than content */
	/* in case of a system crash. */
	ret = fsync(f);
	if (ret != 0) {
		fprintf(stderr, "Error synching parity file '%s'. %s.\n", path, strerror(errno));
		return -1;
	}
#endif

	return 0;
}

int parity_close(const char* path, int f)
{
	int ret;

	ret = close(f);
	if (ret != 0) {
		/* This is a serious error, as it may be the result of a failed write */
		/* identified at later time. */
		/* In a normal filesystem (not NFS) it should never happen */
		fprintf(stderr, "Error closing parity file '%s'. %s.\n", path, strerror(errno));
		return -1;
	}

	return 0;
}

int parity_write(const char* path, int f, block_off_t pos, unsigned char* block_buffer, unsigned block_size)
{
	ssize_t write_ret;
	data_off_t offset;

	offset = pos * (data_off_t)block_size;

#if HAVE_PWRITE
	write_ret = pwrite(f, block_buffer, block_size, offset);
#else
	if (lseek(f, offset, SEEK_SET) != offset) {
		if (errno == ENOSPC) {
			fprintf(stderr, "Failed to grow parity file '%s' due lack of space.\n", path);
		} else {
			fprintf(stderr, "Error seeking file '%s'. %s.\n", path, strerror(errno));
		}
		return -1;
	}

	write_ret = write(f, block_buffer, block_size);
#endif
	if (write_ret != (ssize_t)block_size) { /* conversion is safe because block_size is always small */
		if (errno == ENOSPC) {
			fprintf(stderr, "Failed to grow parity file '%s' due lack of space.\n", path);
		} else {
			fprintf(stderr, "Error writing file '%s'. %s.\n", path, strerror(errno));
		}
		return -1;
	}

	/* Here doesn't make sense to call posix_fadvise(..., POSIX_FADV_DONTNEED) because */
	/* at this time the data is still in not yet written and it cannot be discharged. */

	return 0;
}

int parity_read(const char* path, int f, block_off_t pos, unsigned char* block_buffer, unsigned block_size)
{
	ssize_t read_ret;
	data_off_t offset;

	offset = pos * (data_off_t)block_size;

#if HAVE_PREAD
	read_ret = pread(f, block_buffer, block_size, offset);
#else
	if (lseek(f, offset, SEEK_SET) != offset) {
		fprintf(stderr, "Error seeking file '%s'. %s.\n", path, strerror(errno));
		return -1;
	}

	read_ret = read(f, block_buffer, block_size);
#endif
	if (read_ret < 0) {
		fprintf(stderr, "Error reading file '%s'. %s.\n", path, strerror(errno));
		return -1;
	}
	if (read_ret != (ssize_t)block_size) { /* signed conversion is safe because block_size is always small */
		fprintf(stderr, "File '%s' is smaller than expected.\n", path);
		return -1;
	}

	/* Here isn't needed to call posix_fadvise(..., POSIX_FADV_DONTNEED) */
	/* because we already advised sequential access with POSIX_FADV_SEQUENTIAL. */
	/* In Linux 2.6.33 it's enough to ensure that data is not kept in the cache. */
	/* Better to do nothing and save a syscall for each block. */

	return block_size;
}

