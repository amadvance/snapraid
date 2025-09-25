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
 *
 * More buffers always result in better performance.
 *
 * This is the scrub performance on my machine with different buffers:
 *
 *  1  - 380 MB/s, CPU 26%, speed 100% [SnapRAID 9.2]
 *  2  - 426 MB/s, CPU 46%, speed 112%
 *  4  - 452 MB/s, CPU 54%, speed 118%
 *  8  - 487 MB/s, CPU 60%, speed 128%
 * 16  - 505 MB/s, CPU 63%, speed 132%
 * 32  - 520 MB/s, CPU 64%, speed 136% [SnapRAID <= 12.0]
 * 64  - 524 MB/s, CPU 65%, speed 137% [SnapRAID > 12.0]
 * 128 - 525 MB/s, CPU 66%, speed 138%
 */
#define IO_MIN 3 /* required by writers, readers can work also with 2 */
#define IO_MAX 128

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
 * This represents a worker thread designated to read/write data for a specific disk.
 */
struct snapraid_worker {
#if HAVE_THREAD
	thread_id_t thread; /**< Thread context for the worker. */
#endif

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
};

/**
 * Number of error kind for writers.
 */
#define IO_WRITER_ERROR_BASE TASK_STATE_IOERROR_CONTINUE
#define IO_WRITER_ERROR_MAX (-IO_WRITER_ERROR_BASE)

/**
 * Reader.
 *
 * This represents the pool of worker threads dedicated to read
 * data from the disks.
 */
struct snapraid_io {
	struct snapraid_state* state;

	/**
	 * Number of read-ahead and write-cached buffers to use.
	 *
	 * Between IO_MIN and IO_MAX for thread use.
	 *
	 * If equal to 1, it means to work without any thread.
	 */
	unsigned io_max;

#if HAVE_THREAD
	/**
	 * Mutex used to protect the synchronization
	 * between the io and the workers.
	 */
	thread_mutex_t io_mutex;

	/**
	 * Condition for a new read is completed.
	 *
	 * The workers signal this condition when a new read is completed.
	 * The IO waits on this condition when it's waiting for
	 * a new read to be completed.
	 */
	thread_cond_t read_done;

	/**
	 * Condition for a new read scheduled.
	 *
	 * The workers wait on this condition when they are waiting for a new
	 * read to process.
	 * The IO signals this condition when new reads are scheduled.
	 */
	thread_cond_t read_sched;

	/**
	 * Condition for a new write is completed.
	 *
	 * The workers signal this condition when a new write is completed.
	 * The IO waits on this condition when it's waiting for
	 * a new write to be completed.
	 */
	thread_cond_t write_done;

	/**
	 * Condition for a new write scheduled.
	 *
	 * The workers wait on this condition when they are waiting for a new
	 * write to process.
	 * The IO signals this condition when new writes are scheduled.
	 */
	thread_cond_t write_sched;
#endif

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
	void (*parity_writer)(struct snapraid_worker*, struct snapraid_task*);

	/**
	 * Blocks mapping.
	 *
	 * This info is used to obtain the sequence of block
	 * positions to process.
	 */
	block_off_t block_start;
	block_off_t block_max;
	block_off_t block_next;
	bit_vect_t* block_enabled;

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
	 * A vector of readers, each one representing a different thread.
	 */
	unsigned reader_max; /**< Number of workers. */
	struct snapraid_worker* reader_map; /**< Vector of workers. */
	unsigned writer_max; /**< Number of workers. */
	struct snapraid_worker* writer_map; /**< Vector of workers. */

	/**
	 * List of not yet processed workers.
	 *
	 * The list has ::reader_max + 1 elements. Each element contains
	 * the number of the reader to process.
	 *
	 * At initialization the list is filled with [0..reader_max].
	 * To get the next element to process we use i = list[i + 1].
	 * The end is when i == reader_max.
	 */
	unsigned char* reader_list;
	unsigned char* writer_list;

	/**
	 * Exit condition for all threads.
	 */
	int done;

	/**
	 * The worker task index currently used by the operation in
	 * progress. It's the position in all the reader task_map[].
	 *
	 * Until the operation is in progress, the data buffer cannot be
	 * discarded, because it's in use.
	 *
	 * It's a rolling counter, when reaching ::io_max
	 * it goes again to 0.
	 *
	 * When the caller finish with this index, it calls read_sched(),
	 * and this ::reader_index is increamended, meaning that the data of
	 * the just finished index is not needed anymore and the buffer is
	 * available to be used for the following read-ahead.
	 *
	 * When this happens a read_sched signal is broadcasted, and all the
	 * workers can proceed using this now available index to have one more
	 * read-ahead buffer to load.
	 *
	 * In monothread mode it isn't the task index,
	 * but the worker index.
	 */
	unsigned reader_index;

	/**
	 * The worker task index currently used by the operation in
	 * progress. It's the position in all the writer task_map[].
	 *
	 * Until the operation is in progress, the data buffer cannot be
	 * written, because the data is not yet in the buffer.
	 *
	 * It's a rolling counter, when reaching ::io_max
	 * it goes again to 0.
	 *
	 * When the caller finish writing the data in the buffer for this
	 * index, it calls write_sched() and ::writer_index is incremented,
	 * meaning the the just finished index is scheduled to be written
	 * to disk.
	 *
	 * When this happens a write_sched signal is broadcasted, and all the
	 * workers can proceed writing the data in this index.
	 *
	 * In monothread mode it isn't the task index,
	 * but the worker index.
	 */
	unsigned writer_index;

	/**
	 * Counts the error happening in the writers.
	 */
	int writer_error[IO_WRITER_ERROR_MAX];

	/**
	 * Bandwidth
	 */
	struct snapraid_bw bw;
};

/**
 * Initialize the InputOutput workers.
 *
 * \param io_cache The number of IO buffers for read-ahead and write-behind. 0 for default.
 * \param buffer_max The number of data/parity buffers to allocate.
 */
void io_init(struct snapraid_io* io, struct snapraid_state* state,
	unsigned io_cache, unsigned buffer_max,
	void (*data_reader)(struct snapraid_worker*, struct snapraid_task*),
	struct snapraid_handle* handle_map, unsigned handle_max,
	void (*parity_reader)(struct snapraid_worker*, struct snapraid_task*),
	void (*parity_writer)(struct snapraid_worker*, struct snapraid_task*),
	struct snapraid_parity_handle* parity_handle_map, unsigned parity_handle_max);

/**
 * Deinitialize the InputOutput workers.
 */
void io_done(struct snapraid_io* io);

/**
 * Start all the worker threads.
 */
extern void (*io_start)(struct snapraid_io* io,
	block_off_t blockstart, block_off_t blockmax,
	bit_vect_t* block_enabled);

/**
 * Stop all the worker threads.
 */
extern void (*io_stop)(struct snapraid_io* io);

/**
 * Next read position.
 *
 * This call starts the reading process.
 * It must be called before io_data_read() and io_parity_read().
 *
 * \param io InputOutput context.
 * \param buffer The data buffers to use for this position.
 * \return The parity position.
 */
extern block_off_t (*io_read_next)(struct snapraid_io* io, void*** buffer);

/**
 * Read a data block.
 *
 * It must be called exactly ::handle_max times.
 *
 * \param io InputOutput context.
 * \param diskcur The position of the data block in the ::handle_map vector.
 * \return The completed task.
 */
extern struct snapraid_task* (*io_data_read)(struct snapraid_io* io, unsigned* diskcur, unsigned* waiting_map, unsigned* waiting_mac);

/**
 * Read a parity block.
 *
 * It must be called exactly ::parity_handle_max times.
 *
 * \param io InputOutput context.
 * \param levcur The position of the parity block in the ::parity_handle_map vector.
 * \return The completed task.
 */
extern struct snapraid_task* (*io_parity_read)(struct snapraid_io* io, unsigned* levcur, unsigned* waiting_map, unsigned* waiting_mac);

/**
 * Write of a parity block.
 *
 * It must be called exactly ::parity_handle_max times.
 *
 * \param io InputOutput context.
 * \param levcur The position of the parity block in the ::parity_handle_map vector.
 */
extern void (*io_parity_write)(struct snapraid_io* io, unsigned* levcur, unsigned* waiting_map, unsigned* waiting_mac);

/**
 * Preset the write position.
 *
 * This call starts the write process.
 * It must be called before io_parity_write().
 *
 * \param io InputOutput context.
 * \param blockcur The parity position to write.
 * \param skip Skip the writes, in case parity doesn't need to be updated.
 */
extern void (*io_write_preset)(struct snapraid_io* io, block_off_t blockcur, int skip);

/**
 * Next write position.
 *
 * This call ends the write process.
 * It must be called after io_parity_write().
 *
 * \param io InputOutput context.
 * \param blockcur The parity position to write.
 * \param skip Skip the writes, in case parity doesn't need to be updated.
 * \param writer_error Return the number of errors. Vector of IO_WRITER_ERROR_MAX elements.
 */
extern void (*io_write_next)(struct snapraid_io* io, block_off_t blockcur, int skip, int* writer_error);

/**
 * Refresh the number of cached blocks for all data and parity disks.
 */
extern void (*io_refresh)(struct snapraid_io* io);

/**
 * Flush all the writes.
 */
extern void (*io_flush)(struct snapraid_io* io);

#endif

