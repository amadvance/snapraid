/*
 * Copyright (C) 2016 Andrea Mazzoleni
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

#ifndef __IO_H
#define __IO_H

#include "state.h"
#include "support.h"
#include "handle.h"
#include "parity.h"

/**
 * Number of read-ahead buffers.
 */
#define IO_MAX 4

/**
 * State of the task.
 */
#define TASK_STATE_IOERROR_CONTINUE -4 /**< IO error. Continuation requested. */
#define TASK_STATE_ERROR_CONTINUE -3 /**< Generic error. Continuation requested. */
#define TASK_STATE_IOERROR -2 /**< IO error. Failure requested. */
#define TASK_STATE_ERROR -1 /**< Generic error. Failure requested. */
#define TASK_STATE_EMPTY 0 /**< Nothing to do. */
#define TASK_STATE_READY 1 /**< Ready to start. */
#define TASK_STATE_DONE 2 /**< Task completed. */

/**
 * Task of work.
 *
 * This represents the minimal element of work that worker threads are
 * going to be asked to do.
 *
 * It consists in reading a block of data from a disk.
 *
 * Note that the disk to use is defined implicitly in the worker thread.
 */
struct snapraid_task {
	int state; /**< State of the task. One of the TASK_STATE_*. */
	char path[PATH_MAX]; /**< Path of the file. */
	struct snapraid_disk* disk; /**< Disk of the file. */
	unsigned char* buffer; /**< Where to read the data. */
	block_off_t position; /**< Parity position to read. */

	/**
	 * Result of the task.
	 */
	struct snapraid_block* block;
	struct snapraid_file* file;
	block_off_t file_pos;
	int read_size; /**< Size of the data read. */
	int is_timestamp_different; /**< Report if file has a changed timestamp. */
};

/**
 * Worker for tasks.
 *
 * This represents a worker thread designated to read data
 * from a specific disk.
 */
struct snapraid_worker {
	pthread_t thread;

	struct snapraid_io* io; /**< Parent pointer. */

	void (*func)(struct snapraid_worker*, struct snapraid_task*);

	/**
	 * Handle to data or parity.
	 *
	 * Only one of the two is valid, the other is 0.
	 */
	struct snapraid_handle* handle; /**< Handle at the file on the disk. */
	struct snapraid_parity_handle* parity_handle; /**< Handle at the parity on the disk. */

	/**
	 * Vector of tasks.
	 *
	 * It's a ring of tasks reused cycle after cycle.
	 */
	struct snapraid_task task_map[IO_MAX];

	/**
	 * The task in progress by the worker thread.
	 *
	 * It's an index inside in the ::task_map vector.
	 */
	unsigned index;

	/**
	 * Which buffer base index should be used for destination.
	 */
	unsigned buffer_skew;

	/**
	 * Last chunk we accessed.
	 *
	 * It's used to optimize access of sequential blocks.
	 * We cannot use the one in ::disk because it's used
	 * by the main thread.
	 */
	struct snapraid_chunk* fs_last;
};

/**
 * Reader.
 *
 * This represents the pool of worker threads dedicated to read
 * data from the disks.
 */
struct snapraid_io {
	/**
	 * Mutex used to protect the synchronization
	 * between the io and the workers.
	 */
	pthread_mutex_t mutex;

	/**
	 * Condition for (not) empty queue of tasks.
	 *
	 * The io waits on this condition when the queue is empty,
	 * and it's signaled by workers.
	 */
	pthread_cond_t not_empty;

	/**
	 * Condition for (not) full queue of tasks.
	 *
	 * The workers wait on this condition when the queue is full,
	 * and it's signaled by the io.
	 */
	pthread_cond_t not_full;

	struct snapraid_state* state;

	/**
	 * Base position for workers.
	 *
	 * It's the index in the ::worker_map[].
	 */
	unsigned data_base;
	unsigned data_count;
	unsigned parity_base;
	unsigned parity_count;

	/**
	 * Callbacks for workers.
	 */
	void (*data_reader)(struct snapraid_worker*, struct snapraid_task*);
	void (*parity_reader)(struct snapraid_worker*, struct snapraid_task*);

	/**
	 * Blocks mapping.
	 *
	 * This info is used to obtain the sequence of block
	 * positions to process.
	 */
	block_off_t block_start;
	block_off_t block_max;
	block_off_t block_next;
	int (*block_is_enabled)(void* arg,block_off_t);
	void* block_arg;

	/**
	 * Buffers for data.
	 *
	 * A pool of buffers used to store the data read.
	 */
	unsigned buffer_max; /**< Number of buffers. */
	void* buffer_alloc_map[IO_MAX]; /**< Allocation map for buffers. */
	void** buffer_map[IO_MAX]; /**< Buffers for data. */

	/**
	 * Workers.
	 *
	 * A vector of workers, each one representing a different thread.
	 */
	unsigned worker_max; /**< Number of workers. */
	struct snapraid_worker* worker_map; /**< Vector of workers. */

	/**
	 * List of not yet processed workers.
	 *
	 * The list has ::worker_max + 1 elements. Each element contains
	 * the number of the worker to process.
	 *
	 * At initialization the list is filled with [0..worker_max].
	 * To get the next element to process we use i = list[i + 1].
	 * The end is when i == worker_max.
	 */
	unsigned char* worker_list;

	/**
	 * Exit condition for all threads.
	 */
	int done;

	/**
	 * The task currently used by the caller.
	 *
	 * It's a rolling counter, when reaching IO_MAX
	 * it goes again to 0.
	 */
	unsigned index;
};

/**
 * Initialize the InputOutput workers.
 *
 * \param buffer_max The number of data/parity buffers to allocate.
 */
void io_init(struct snapraid_io* io, struct snapraid_state* state,
	unsigned buffer_max,
	void (*data_reader)(struct snapraid_worker*, struct snapraid_task*),
	struct snapraid_handle* handle_map, unsigned handle_max,
	void (*parity_reader)(struct snapraid_worker*, struct snapraid_task*),
	struct snapraid_parity_handle* parity_handle_map, unsigned parity_handle_max);

/**
 * Deinitialize te InputOutput workers.
 */
void io_done(struct snapraid_io* io);

/**
 * Start all the worker threads.
 */
void io_start(struct snapraid_io* io,
	block_off_t blockstart, block_off_t blockmax,
	int (*block_is_enabled)(void* arg,block_off_t), void* blockarg);

/**
 * Stop all the worker threads.
 */
void io_stop(struct snapraid_io* io);

/**
 * Get the next parity position to process.
 *
 * \param io InputOutput context.
 * \param buffer The data buffers to use for this position.
 * \return The parity position.
 */
block_off_t io_next(struct snapraid_io* io, void*** buffer);

/**
 * Get the next data block to process.
 *
 * It must be called exactly ::handle_max times.
 *
 * \param io InputOutput context.
 * \param pos The position of the data block in the ::handle_map vector.
 * \return The completed task.
 */
struct snapraid_task* io_data_next(struct snapraid_io* io, unsigned* pos);

/**
 * Get the next parity block to process.
 *
 * It must be called exactly ::parity_handle_max times.
 *
 * \param io InputOutput context.
 * \param pos The position of the parity block in the ::parity_handle_map vector.
 * \return The completed task.
 */
struct snapraid_task* io_parity_next(struct snapraid_io* io, unsigned* pos);

#endif
