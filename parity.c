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

int parity_create(struct snapraid_parity* parity, const char* path, data_off_t size)
{
	int ret;

	pathcpy(parity->path, sizeof(parity->path), path);

	/* opening in sequential mode in Windows */
	parity->f = open(parity->path, O_RDWR | O_CREAT | O_BINARY | O_SEQUENTIAL, 0600);
	if (parity->f == -1) {
		fprintf(stderr, "Error opening parity file '%s'. %s.\n", parity->path, strerror(errno));
		return -1;
	}

	/* get the stat info */
	ret = fstat(parity->f, &parity->st);
	if (ret != 0) {
		fprintf(stderr, "Error accessing parity file '%s'. %s.\n", parity->path, strerror(errno));
		goto bail;
	}

	/* get the size of the exising data */
	parity->valid_size = parity->st.st_size;

	if (parity->st.st_size < size) {
#if HAVE_FALLOCATE
		/* allocate real space using the specific Linux fallocate() operation. */
		/* If the underline filesystem doesn't support it, this operation fails, */
		/* instead posix_fallocate() fallbacks to write the whole file. */
		ret = fallocate(parity->f, 0, 0, size);

		/* fallocate() returns the error number as positive integer, */
		/* and in this case it doesn't set errno, just like posix_fallocate() */
		/* Checking the glibc code (2.11.1 and 2.14.1) it seems that ENOSYS */
		/* may be returned in errno, so we support both the return way */
		if (ret > 0) { /* if a positive error is returned, convert it to errno */
			errno = ret;
			ret = -1;
		}

		/* we get EOPNOTSUPP if the operation is not supported, like in ext3/ext2 */
		/* or ENOSYS with kernel before 2.6.23 */
		if (errno == EOPNOTSUPP || errno == ENOSYS) {
			/* fallback using ftruncate() */
			ret = ftruncate(parity->f, size);
		}
#else
		/* allocate using a sparse file */
		ret = ftruncate(parity->f, size);
#endif
		if (ret != 0) {
			if (errno == ENOSPC) {
				fprintf(stderr, "Failed to grow parity file '%s' to size %"PRIu64" due lack of space.\n", parity->path, size);
			} else {
				fprintf(stderr, "Error growing parity file '%s' to size %"PRIu64". Do you have enough space? %s.\n", parity->path, size, strerror(errno));
			}
			goto bail;
		}
	} else if (parity->st.st_size > size) {
		ret = ftruncate(parity->f, size);
		if (ret != 0) {
			fprintf(stderr, "Error truncating parity file '%s' to size %"PRIu64". %s.\n", parity->path, size, strerror(errno));
			goto bail;
		}

		/* adjust the size to the truncated size */
		parity->valid_size = size;
	}

#if HAVE_POSIX_FADVISE
	/* advise sequential access */
	ret = posix_fadvise(parity->f, 0, 0, POSIX_FADV_SEQUENTIAL);
	if (ret != 0) {
		fprintf(stderr, "Error advising parity file '%s'. %s.\n", parity->path, strerror(ret));
		goto bail;
	}
#endif

	return 0;

bail:
	close(parity->f);
	parity->f = -1;
	parity->valid_size = 0;
	return -1;
}

int parity_open(struct snapraid_parity* parity, const char* path)
{
	int ret;

	pathcpy(parity->path, sizeof(parity->path), path);

	/* opening in sequential mode in Windows */
	parity->f = open(parity->path, O_RDONLY | O_BINARY | O_SEQUENTIAL);
	if (parity->f == -1) {
		fprintf(stderr, "Error opening parity file '%s'. %s.\n", parity->path, strerror(errno));
		return -1;
	}

	/* get the stat info */
	ret = fstat(parity->f, &parity->st);
	if (ret != 0) {
		fprintf(stderr, "Error accessing parity file '%s'. %s.\n", parity->path, strerror(errno));
		goto bail;
	}

	/* get the size of the exising data */
	parity->valid_size = parity->st.st_size;

#if HAVE_POSIX_FADVISE
	/* advise sequential access */
	ret = posix_fadvise(parity->f, 0, 0, POSIX_FADV_SEQUENTIAL);
	if (ret != 0) {
		fprintf(stderr, "Error advising parity file '%s'. %s.\n", parity->path, strerror(ret));
		goto bail;
	}
#endif

	return 0;

bail:
	close(parity->f);
	parity->f = -1;
	parity->valid_size = 0;
	return -1;
}

int parity_sync(struct snapraid_parity* parity)
{
#if HAVE_FSYNC
	int ret;

	/* Ensure that data changes are written to disk. */
	/* This is required to ensure that parity is more updated than content */
	/* in case of a system crash. */
	ret = fsync(parity->f);
	if (ret != 0) {
		fprintf(stderr, "Error synching parity file '%s'. %s.\n", parity->path, strerror(errno));
		return -1;
	}
#endif

	return 0;
}

int parity_close(struct snapraid_parity* parity)
{
	int ret;

	ret = close(parity->f);
	if (ret != 0) {
		/* invalidate for error */
		parity->f = -1;
		parity->valid_size = 0;

		/* This is a serious error, as it may be the result of a failed write */
		/* identified at later time. */
		/* In a normal filesystem (not NFS) it should never happen */
		fprintf(stderr, "Error closing parity file '%s'. %s.\n", parity->path, strerror(errno));
		return -1;
	}

	/* reset the descriptor */
	parity->f = -1;
	parity->valid_size = 0;

	return 0;
}

int parity_write(struct snapraid_parity* parity, block_off_t pos, unsigned char* block_buffer, unsigned block_size)
{
	ssize_t write_ret;
	data_off_t offset;

	offset = pos * (data_off_t)block_size;

#if HAVE_PWRITE
	write_ret = pwrite(parity->f, block_buffer, block_size, offset);
#else
	if (lseek(parity->f, offset, SEEK_SET) != offset) {
		if (errno == ENOSPC) {
			fprintf(stderr, "Failed to grow parity file '%s' due lack of space.\n", parity->path);
		} else {
			fprintf(stderr, "Error seeking file '%s'. %s.\n", parity->path, strerror(errno));
		}
		return -1;
	}

	write_ret = write(parity->f, block_buffer, block_size);
#endif
	if (write_ret != (ssize_t)block_size) { /* conversion is safe because block_size is always small */
		if (errno == ENOSPC) {
			fprintf(stderr, "Failed to grow parity file '%s' due lack of space.\n", parity->path);
		} else {
			fprintf(stderr, "Error writing file '%s'. %s.\n", parity->path, strerror(errno));
		}
		return -1;
	}

	/* adjust the size of the valid data */
	if (parity->valid_size < offset + block_size) {
		parity->valid_size = offset + block_size;
	}

	/* Here doesn't make sense to call posix_fadvise(..., POSIX_FADV_DONTNEED) because */
	/* at this time the data is still in not yet written and it cannot be discharged. */

	return 0;
}

int parity_read(struct snapraid_parity* parity, block_off_t pos, unsigned char* block_buffer, unsigned block_size)
{
	ssize_t read_ret;
	data_off_t offset;

	offset = pos * (data_off_t)block_size;

	/* check if we are going to read only not initialized data */
	if (offset >= parity->valid_size) {
		fprintf(stderr, "Reading missing data from file '%s' at offset %"PRIu64".\n", parity->path, offset);
		return -1;
	}

#if HAVE_PREAD
	read_ret = pread(parity->f, block_buffer, block_size, offset);
#else
	if (lseek(parity->f, offset, SEEK_SET) != offset) {
		fprintf(stderr, "Error seeking file '%s' at offset %"PRIu64". %s.\n", parity->path, offset, strerror(errno));
		return -1;
	}

	read_ret = read(parity->f, block_buffer, block_size);
#endif
	if (read_ret < 0) {
		fprintf(stderr, "Error reading file '%s' at offset %"PRIu64". %s.\n", parity->path, offset, strerror(errno));
		return -1;
	}
	if (read_ret != (ssize_t)block_size) { /* signed conversion is safe because block_size is always small */
		fprintf(stderr, "File '%s' is smaller than expected. Read %d bytes instead of %d at offset %"PRIu64".\n", parity->path, (int)read_ret, block_size, offset);
		return -1;
	}

	/* Here isn't needed to call posix_fadvise(..., POSIX_FADV_DONTNEED) */
	/* because we already advised sequential access with POSIX_FADV_SEQUENTIAL. */
	/* In Linux 2.6.33 it's enough to ensure that data is not kept in the cache. */
	/* Better to do nothing and save a syscall for each block. */

	return block_size;
}

