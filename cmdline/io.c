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

#include "portable.h"

#include "io.h"

void (*io_start)(struct snapraid_io* io,
	block_off_t blockstart, block_off_t blockmax,
	bit_vect_t* block_enabled) = 0;
void (*io_stop)(struct snapraid_io* io) = 0;
block_off_t (*io_read_next)(struct snapraid_io* io, void*** buffer) = 0;
struct snapraid_task* (*io_data_read)(struct snapraid_io* io, unsigned* diskcur, unsigned* waiting_map, unsigned* waiting_mac) = 0;
struct snapraid_task* (*io_parity_read)(struct snapraid_io* io, unsigned* levcur, unsigned* waiting_map, unsigned* waiting_mac) = 0;
void (*io_parity_write)(struct snapraid_io* io, unsigned* levcur, unsigned* waiting_map, unsigned* waiting_mac) = 0;
void (*io_write_preset)(struct snapraid_io* io, block_off_t blockcur, int skip) = 0;
void (*io_write_next)(struct snapraid_io* io, block_off_t blockcur, int skip, int* writer_error) = 0;
void (*io_refresh)(struct snapraid_io* io) = 0;


/**
 * Get the next block position to process.
 */
static block_off_t io_position_next(struct snapraid_io* io)
{
	block_off_t blockcur;

	/* get the next position */
	if (io->block_enabled) {
		while (io->block_next < io->block_max && !bit_vect_test(io->block_enabled, io->block_next))
			++io->block_next;
	}

	blockcur = io->block_next;

	/* next block for the next call */
	++io->block_next;

	return blockcur;
}

/**
 * Setup the next pending task for all readers.
 */
static void io_reader_sched(struct snapraid_io* io, int task_index, block_off_t blockcur)
{
	unsigned i;

	for (i = 0; i < io->reader_max; ++i) {
		struct snapraid_worker* worker = &io->reader_map[i];
		struct snapraid_task* task = &worker->task_map[task_index];

		/* setup the new pending task */
		if (blockcur < io->block_max)
			task->state = TASK_STATE_READY;
		else
			task->state = TASK_STATE_EMPTY;

		task->path[0] = 0;
		if (worker->handle)
			task->disk = worker->handle->disk;
		else
			task->disk = 0;
		task->buffer = io->buffer_map[task_index][worker->buffer_skew + i];
		task->position = blockcur;
		task->block = 0;
		task->file = 0;
		task->file_pos = 0;
		task->read_size = 0;
		task->is_timestamp_different = 0;
	}
}

/**
 * Setup the next pending task for all writers.
 */
static void io_writer_sched(struct snapraid_io* io, int task_index, block_off_t blockcur)
{
	unsigned i;

	for (i = 0; i < io->writer_max; ++i) {
		struct snapraid_worker* worker = &io->writer_map[i];
		struct snapraid_task* task = &worker->task_map[task_index];

		/* setup the new pending task */
		task->state = TASK_STATE_READY;
		task->path[0] = 0;
		task->disk = 0;
		task->buffer = io->buffer_map[task_index][worker->buffer_skew + i];
		task->position = blockcur;
		task->block = 0;
		task->file = 0;
		task->file_pos = 0;
		task->read_size = 0;
		task->is_timestamp_different = 0;
	}
}

/**
 * Setup an empty next pending task for all writers.
 */
static void io_writer_sched_empty(struct snapraid_io* io, int task_index, block_off_t blockcur)
{
	unsigned i;

	for (i = 0; i < io->writer_max; ++i) {
		struct snapraid_worker* worker = &io->writer_map[i];
		struct snapraid_task* task = &worker->task_map[task_index];

		/* setup the new pending task */
		task->state = TASK_STATE_EMPTY;
		task->path[0] = 0;
		task->disk = 0;
		task->buffer = 0;
		task->position = blockcur;
		task->block = 0;
		task->file = 0;
		task->file_pos = 0;
		task->read_size = 0;
		task->is_timestamp_different = 0;
	}
}

/*****************************************************************************/
/* mono thread */

static block_off_t io_read_next_mono(struct snapraid_io* io, void*** buffer)
{
	block_off_t blockcur_schedule;

	/* reset the index */
	io->reader_index = 0;

	blockcur_schedule = io_position_next(io);

	/* schedule the next read */
	io_reader_sched(io, 0, blockcur_schedule);

	/* set the buffer to use */
	*buffer = io->buffer_map[0];

	return blockcur_schedule;
}

static void io_write_preset_mono(struct snapraid_io* io, block_off_t blockcur, int skip)
{
	unsigned i;

	/* reset the index */
	io->writer_index = 0;

	/* clear errors */
	for (i = 0; i < IO_WRITER_ERROR_MAX; ++i)
		io->writer_error[i] = 0;

	if (skip) {
		/* skip the next write */
		io_writer_sched_empty(io, 0, blockcur);
	} else {
		/* schedule the next write */
		io_writer_sched(io, 0, blockcur);
	}
}

static void io_write_next_mono(struct snapraid_io* io, block_off_t blockcur, int skip, int* writer_error)
{
	unsigned i;

	(void)blockcur;
	(void)skip;

	/* report errors */
	for (i = 0; i < IO_WRITER_ERROR_MAX; ++i)
		writer_error[i] = io->writer_error[i];
}

static void io_refresh_mono(struct snapraid_io* io)
{
	(void)io;
}

static struct snapraid_task* io_task_read_mono(struct snapraid_io* io, unsigned base, unsigned count, unsigned* pos, unsigned* waiting_map, unsigned* waiting_mac)
{
	struct snapraid_worker* worker;
	struct snapraid_task* task;
	unsigned i;

	/* get the next task */
	i = io->reader_index++;

	assert(base <= i && i < base + count);

	worker = &io->reader_map[i];
	task = &worker->task_map[0];

	/* do the work */
	if (task->state != TASK_STATE_EMPTY)
		worker->func(worker, task);

	/* return the position */
	*pos = i - base;

	/* store the waiting index */
	waiting_map[0] = i - base;
	*waiting_mac = 1;

	return task;
}

static struct snapraid_task* io_data_read_mono(struct snapraid_io* io, unsigned* pos, unsigned* waiting_map, unsigned* waiting_mac)
{
	return io_task_read_mono(io, io->data_base, io->data_count, pos, waiting_map, waiting_mac);
}

static struct snapraid_task* io_parity_read_mono(struct snapraid_io* io, unsigned* pos, unsigned* waiting_map, unsigned* waiting_mac)
{
	return io_task_read_mono(io, io->parity_base, io->parity_count, pos, waiting_map, waiting_mac);
}

static void io_parity_write_mono(struct snapraid_io* io, unsigned* pos, unsigned* waiting_map, unsigned* waiting_mac)
{
	struct snapraid_worker* worker;
	struct snapraid_task* task;
	unsigned i;

	/* get the next task */
	i = io->writer_index++;

	worker = &io->writer_map[i];
	task = &worker->task_map[0];

	io->writer_error[i] = 0;

	/* do the work */
	if (task->state != TASK_STATE_EMPTY)
		worker->func(worker, task);

	/* return the position */
	*pos = i;

	/* store the waiting index */
	waiting_map[0] = i;
	*waiting_mac = 1;
}

static void io_start_mono(struct snapraid_io* io,
	block_off_t blockstart, block_off_t blockmax,
	bit_vect_t* block_enabled)
{
	io->block_start = blockstart;
	io->block_max = blockmax;
	io->block_enabled = block_enabled;
	io->block_next = blockstart;
}

static void io_stop_mono(struct snapraid_io* io)
{
	(void)io;
}

/*****************************************************************************/
/* multi thread */

/* disable multithread if pthread is not present */
#if HAVE_THREAD

/**
 * Get the next task to work on for a reader.
 *
 * This is the synchronization point for workers with the io.
 */
static struct snapraid_task* io_reader_step(struct snapraid_worker* worker)
{
	struct snapraid_io* io = worker->io;

	/* the synchronization is protected by the io mutex */
	thread_mutex_lock(&io->io_mutex);

	while (1) {
		unsigned next_index;

		/* check if the worker has to exit */
		/* even if there is work to do */
		if (io->done) {
			thread_mutex_unlock(&io->io_mutex);
			return 0;
		}

		/* get the next pending task */
		next_index = (worker->index + 1) % io->io_max;

		/* if the queue of pending tasks is not empty */
		if (next_index != io->reader_index) {
			struct snapraid_task* task;

			/* the index that the IO may be waiting for */
			unsigned waiting_index = io->reader_index;

			/* the index that worker just completed */
			unsigned done_index = worker->index;

			/* get the new working task */
			worker->index = next_index;
			task = &worker->task_map[worker->index];

			/* if the just completed task is at this index */
			if (done_index == waiting_index) {
				/* notify the IO that a new read is complete */
				thread_cond_signal_and_unlock(&io->read_done, &io->io_mutex);
			} else {
				thread_mutex_unlock(&io->io_mutex);
			}

			/* return the new task */
			return task;
		}

		/* otherwise wait for a read_sched event */
		thread_cond_wait(&io->read_sched, &io->io_mutex);
	}
}

/**
 * Get the next task to work on for a writer.
 *
 * This is the synchronization point for workers with the io.
 */
static struct snapraid_task* io_writer_step(struct snapraid_worker* worker, int state)
{
	struct snapraid_io* io = worker->io;
	int error_index;

	/* the synchronization is protected by the io mutex */
	thread_mutex_lock(&io->io_mutex);

	/* counts the number of errors in the global state */
	error_index = state - IO_WRITER_ERROR_BASE;
	if (error_index >= 0 && error_index < IO_WRITER_ERROR_MAX)
		++io->writer_error[error_index];

	while (1) {
		unsigned next_index;

		/* get the next pending task */
		next_index = (worker->index + 1) % io->io_max;

		/* if the queue of pending tasks is not empty */
		if (next_index != io->writer_index) {
			struct snapraid_task* task;

			/* the index that the IO may be waiting for */
			unsigned waiting_index = (io->writer_index + 1) % io->io_max;

			/* the index that worker just completed */
			unsigned done_index = worker->index;

			/* get the new working task */
			worker->index = next_index;
			task = &worker->task_map[worker->index];

			/* if the just completed task is at this index */
			if (done_index == waiting_index) {
				/* notify the IO that a new write is complete */
				thread_cond_signal_and_unlock(&io->write_done, &io->io_mutex);
			} else {
				thread_mutex_unlock(&io->io_mutex);
			}

			/* return the new task */
			return task;
		}

		/* check if the worker has to exit */
		/* but only if there is no work to do */
		if (io->done) {
			thread_mutex_unlock(&io->io_mutex);
			return 0;
		}

		/* otherwise wait for a write_sched event */
		thread_cond_wait(&io->write_sched, &io->io_mutex);
	}
}

/**
 * Get the next block position to operate on.
 *
 * This is the synchronization point for workers with the io.
 */
static block_off_t io_read_next_thread(struct snapraid_io* io, void*** buffer)
{
	block_off_t blockcur_schedule;
	block_off_t blockcur_caller;
	unsigned i;

	/* get the next parity position to process */
	blockcur_schedule = io_position_next(io);

	/* ensure that all data/parity was read */
	assert(io->reader_list[0] == io->reader_max);

	/* setup the list of workers to process */
	for (i = 0; i <= io->reader_max; ++i)
		io->reader_list[i] = i;

	/* the synchronization is protected by the io mutex */
	thread_mutex_lock(&io->io_mutex);

	/* schedule the next read */
	io_reader_sched(io, io->reader_index, blockcur_schedule);

	/* set the index for the tasks to return to the caller */
	io->reader_index = (io->reader_index + 1) % io->io_max;

	/* get the position to operate at high level from one task */
	blockcur_caller = io->reader_map[0].task_map[io->reader_index].position;

	/* set the buffer to use */
	*buffer = io->buffer_map[io->reader_index];

	/* signal all the workers that there is a new pending task */
	thread_cond_broadcast_and_unlock(&io->read_sched, &io->io_mutex);

	return blockcur_caller;
}

static void io_write_preset_thread(struct snapraid_io* io, block_off_t blockcur, int skip)
{
	(void)io;
	(void)blockcur;
	(void)skip;
}

static void io_write_next_thread(struct snapraid_io* io, block_off_t blockcur, int skip, int* writer_error)
{
	unsigned i;

	/* ensure that all parity was written */
	assert(io->writer_list[0] == io->writer_max);

	/* setup the list of workers to process */
	for (i = 0; i <= io->writer_max; ++i)
		io->writer_list[i] = i;

	/* the synchronization is protected by the io mutex */
	thread_mutex_lock(&io->io_mutex);

	/* report errors */
	for (i = 0; i < IO_WRITER_ERROR_MAX; ++i) {
		writer_error[i] = io->writer_error[i];
		io->writer_error[i] = 0;
	}

	if (skip) {
		/* skip the next write */
		io_writer_sched_empty(io, io->writer_index, blockcur);
	} else {
		/* schedule the next write */
		io_writer_sched(io, io->writer_index, blockcur);
	}

	/* at this point the writers must be in sync with the readers */
	assert(io->writer_index == io->reader_index);

	/* set the index to be used for the next write */
	io->writer_index = (io->writer_index + 1) % io->io_max;

	/* signal all the workers that there is a new pending task */
	thread_cond_broadcast_and_unlock(&io->write_sched, &io->io_mutex);
}

static void io_refresh_thread(struct snapraid_io* io)
{
	unsigned i;

	/* the synchronization is protected by the io mutex */
	thread_mutex_lock(&io->io_mutex);

	/* for all readers, count the number of read blocks */
	for (i = 0; i < io->reader_max; ++i) {
		unsigned begin, end, cached;
		struct snapraid_worker* worker = &io->reader_map[i];

		/* the first block read */
		begin = io->reader_index + 1;
		/* the block in reading */
		end = worker->index;
		if (begin > end)
			end += io->io_max;
		cached = end - begin;

		if (worker->parity_handle)
			io->state->parity[worker->parity_handle->level].cached_blocks = cached;
		else
			worker->handle->disk->cached_blocks = cached;
	}

	/* for all writers, count the number of written blocks */
	/* note that this is a kind of "opposite" of cached blocks */
	for (i = 0; i < io->writer_max; ++i) {
		unsigned begin, end, cached;
		struct snapraid_worker* worker = &io->writer_map[i];

		/* the first block written */
		begin = io->writer_index + 1;
		/* the block in writing */
		end = worker->index;
		if (begin > end)
			end += io->io_max;
		cached = end - begin;

		io->state->parity[worker->parity_handle->level].cached_blocks = cached;
	}

	thread_mutex_unlock(&io->io_mutex);
}

static struct snapraid_task* io_task_read_thread(struct snapraid_io* io, unsigned base, unsigned count, unsigned* pos, unsigned* waiting_map, unsigned* waiting_mac)
{
	unsigned waiting_cycle;

	/* count the waiting cycle */
	waiting_cycle = 0;

	/* clear the waiting indexes */
	*waiting_mac = 0;

	/* the synchronization is protected by the io mutex */
	thread_mutex_lock(&io->io_mutex);

	while (1) {
		unsigned char* let;
		unsigned busy_index;

		/* get the index the IO is using */
		/* we must ensure that this index has not a read in progress */
		/* to avoid a concurrent access */
		busy_index = io->reader_index;

		/* search for a worker that has already finished */
		let = &io->reader_list[0];
		while (1) {
			unsigned i = *let;

			/* if we are at the end */
			if (i == io->reader_max)
				break;

			/* if it's in range */
			if (base <= i && i < base + count) {
				struct snapraid_worker* worker;

				/* if it's the first cycle */
				if (waiting_cycle == 0) {
					/* store the waiting indexes */
					waiting_map[(*waiting_mac)++] = i - base;
				}

				worker = &io->reader_map[i];

				/* if the worker has finished this index */
				if (busy_index != worker->index) {
					struct snapraid_task* task;

					task = &worker->task_map[io->reader_index];

					thread_mutex_unlock(&io->io_mutex);

					/* mark the worker as processed */
					/* setting the previous one to point at the next one */
					*let = io->reader_list[i + 1];

					/* return the position */
					*pos = i - base;

					/* on the first cycle, no one is waiting */
					if (waiting_cycle == 0)
						*waiting_mac = 0;

					return task;
				}
			}

			/* next position to check */
			let = &io->reader_list[i + 1];
		}

		/* if no worker is ready, wait for an event */
		thread_cond_wait(&io->read_done, &io->io_mutex);

		/* count the cycles */
		++waiting_cycle;
	}
}

static struct snapraid_task* io_data_read_thread(struct snapraid_io* io, unsigned* pos, unsigned* waiting_map, unsigned* waiting_mac)
{
	return io_task_read_thread(io, io->data_base, io->data_count, pos, waiting_map, waiting_mac);
}

static struct snapraid_task* io_parity_read_thread(struct snapraid_io* io, unsigned* pos, unsigned* waiting_map, unsigned* waiting_mac)
{
	return io_task_read_thread(io, io->parity_base, io->parity_count, pos, waiting_map, waiting_mac);
}

static void io_parity_write_thread(struct snapraid_io* io, unsigned* pos, unsigned* waiting_map, unsigned* waiting_mac)
{
	unsigned waiting_cycle;

	/* count the waiting cycle */
	waiting_cycle = 0;

	/* clear the waiting indexes */
	*waiting_mac = 0;

	/* the synchronization is protected by the io mutex */
	thread_mutex_lock(&io->io_mutex);

	while (1) {
		unsigned char* let;
		unsigned busy_index;

		/* get the next index the IO is going to use */
		/* we must ensure that this index has not a write in progress */
		/* to avoid a concurrent access */
		/* note that we are already sure that a write is not in progress */
		/* at the index the IO is using at now */
		busy_index = (io->writer_index + 1) % io->io_max;

		/* search for a worker that has already finished */
		let = &io->writer_list[0];
		while (1) {
			unsigned i = *let;
			struct snapraid_worker* worker;

			/* if we are at the end */
			if (i == io->writer_max)
				break;

			/* if it's the first cycle */
			if (waiting_cycle == 0) {
				/* store the waiting indexes */
				waiting_map[(*waiting_mac)++] = i;
			}

			worker = &io->writer_map[i];

			/* the two indexes cannot be equal */
			assert(io->writer_index != worker->index);

			/* if the worker has finished this index */
			if (busy_index != worker->index) {
				thread_mutex_unlock(&io->io_mutex);

				/* mark the worker as processed */
				/* setting the previous one to point at the next one */
				*let = io->writer_list[i + 1];

				/* return the position */
				*pos = i;

				/* on the first cycle, no one is waiting */
				if (waiting_cycle == 0)
					*waiting_mac = 0;

				return;
			}

			/* next position to check */
			let = &io->writer_list[i + 1];
		}

		/* if no worker is ready, wait for an event */
		thread_cond_wait(&io->write_done, &io->io_mutex);

		/* count the cycles */
		++waiting_cycle;
	}
}

static void io_reader_worker(struct snapraid_worker* worker, struct snapraid_task* task)
{
	/* if we reached the end */
	if (task->position >= worker->io->block_max) {
		/* complete a dummy task */
		task->state = TASK_STATE_EMPTY;
	} else {
		worker->func(worker, task);
	}
}

static void* io_reader_thread(void* arg)
{
	struct snapraid_worker* worker = arg;

	/* force completion of the first task */
	io_reader_worker(worker, &worker->task_map[0]);

	while (1) {
		struct snapraid_task* task;

		/* get the new task */
		task = io_reader_step(worker);

		/* if no task, it means to exit */
		if (!task)
			break;

		/* nothing more to do */
		if (task->state == TASK_STATE_EMPTY)
			continue;

		assert(task->state == TASK_STATE_READY);

		/* work on the assigned task */
		io_reader_worker(worker, task);
	}

	return 0;
}

static void* io_writer_thread(void* arg)
{
	struct snapraid_worker* worker = arg;
	int latest_state = TASK_STATE_DONE;

	while (1) {
		struct snapraid_task* task;

		/* get the new task */
		task = io_writer_step(worker, latest_state);

		/* if no task, it means to exit */
		if (!task)
			break;

		/* nothing more to do */
		if (task->state == TASK_STATE_EMPTY) {
			latest_state = TASK_STATE_DONE;
			continue;
		}

		assert(task->state == TASK_STATE_READY);

		/* work on the assigned task */
		worker->func(worker, task);

		/* save the resulting state */
		latest_state = task->state;
	}

	return 0;
}

static void io_start_thread(struct snapraid_io* io,
	block_off_t blockstart, block_off_t blockmax,
	bit_vect_t* block_enabled)
{
	unsigned i;
	tommy_node* j;

	/* enable the filesystem mutex in all disks */
	for (j = io->state->disklist; j != 0; j = j->next) {
		struct snapraid_disk* disk = j->data;
		disk_start_thread(disk);
	}

	io->block_start = blockstart;
	io->block_max = blockmax;
	io->block_enabled = block_enabled;
	io->block_next = blockstart;

	io->done = 0;
	io->reader_index = io->io_max - 1;
	io->writer_index = 0;

	/* clear writer errors */
	for (i = 0; i < IO_WRITER_ERROR_MAX; ++i)
		io->writer_error[i] = 0;

	/* setup the initial read pending tasks, except the latest one, */
	/* the latest will be initialized at the fist io_read_next() call */
	for (i = 0; i < io->io_max - 1; ++i) {
		block_off_t blockcur = io_position_next(io);

		io_reader_sched(io, i, blockcur);
	}

	/* setup the lists of workers to process */
	io->reader_list[0] = io->reader_max;
	for (i = 0; i <= io->writer_max; ++i)
		io->writer_list[i] = i;

	/* start the reader threads */
	for (i = 0; i < io->reader_max; ++i) {
		struct snapraid_worker* worker = &io->reader_map[i];

		worker->index = 0;

		thread_create(&worker->thread, io_reader_thread, worker);
	}

	/* start the writer threads */
	for (i = 0; i < io->writer_max; ++i) {
		struct snapraid_worker* worker = &io->writer_map[i];

		worker->index = io->io_max - 1;

		thread_create(&worker->thread, io_writer_thread, worker);
	}
}

static void io_stop_thread(struct snapraid_io* io)
{
	unsigned i;

	thread_mutex_lock(&io->io_mutex);

	/* mark that we are stopping */
	io->done = 1;

	/* signal all the threads to recognize the new state */
	thread_cond_broadcast(&io->read_sched);
	thread_cond_broadcast(&io->write_sched);

	thread_mutex_unlock(&io->io_mutex);

	/* wait for all readers to terminate */
	for (i = 0; i < io->reader_max; ++i) {
		struct snapraid_worker* worker = &io->reader_map[i];
		void* retval;

		/* wait for thread termination */
		thread_join(worker->thread, &retval);
	}

	/* wait for all writers to terminate */
	for (i = 0; i < io->writer_max; ++i) {
		struct snapraid_worker* worker = &io->writer_map[i];
		void* retval;

		/* wait for thread termination */
		thread_join(worker->thread, &retval);
	}
}

#endif

/*****************************************************************************/
/* global */

void io_init(struct snapraid_io* io, struct snapraid_state* state,
	unsigned io_cache, unsigned buffer_max,
	void (*data_reader)(struct snapraid_worker*, struct snapraid_task*),
	struct snapraid_handle* handle_map, unsigned handle_max,
	void (*parity_reader)(struct snapraid_worker*, struct snapraid_task*),
	void (*parity_writer)(struct snapraid_worker*, struct snapraid_task*),
	struct snapraid_parity_handle* parity_handle_map, unsigned parity_handle_max)
{
	unsigned i;
	size_t allocated_size;
	size_t block_size = state->block_size;

	io->state = state;

#if HAVE_THREAD
	if (io_cache == 0) {
		/* default is 16 MiB of cache */
		/* this seems to be a good tradeoff between speed and memory usage */
		io->io_max = 16 * 1024 * 1024 / state->block_size;
		if (io->io_max < IO_MIN)
			io->io_max = IO_MIN;
		if (io->io_max > IO_MAX)
			io->io_max = IO_MAX;
	} else {
		io->io_max = io_cache;
	}
#else
	(void)io_cache;

	/* without pthread force the mono thread mode */
	io->io_max = 1;
#endif

	assert(io->io_max == 1 || (io->io_max >= IO_MIN && io->io_max <= IO_MAX));

	io->buffer_max = buffer_max;
	allocated_size = 0;
	for (i = 0; i < io->io_max; ++i) {
		if (state->file_mode != ADVISE_DIRECT)
			io->buffer_map[i] = malloc_nofail_vector_align(handle_max, buffer_max, block_size, &io->buffer_alloc_map[i]);
		else
			io->buffer_map[i] = malloc_nofail_vector_direct(handle_max, buffer_max, block_size, &io->buffer_alloc_map[i]);
		if (!state->opt.skip_self)
			mtest_vector(io->buffer_max, state->block_size, io->buffer_map[i]);
		allocated_size += block_size * buffer_max;
	}

	msg_progress("Using %u MiB of memory for %u cached blocks.\n", (unsigned)(allocated_size / MEBI), io->io_max);

	if (parity_writer) {
		io->reader_max = handle_max;
		io->writer_max = parity_handle_max;
	} else {
		io->reader_max = handle_max + parity_handle_max;
		io->writer_max = 0;
	}

	io->reader_map = malloc_nofail(sizeof(struct snapraid_worker) * io->reader_max);
	io->reader_list = malloc_nofail(io->reader_max + 1);
	io->writer_map = malloc_nofail(sizeof(struct snapraid_worker) * io->writer_max);
	io->writer_list = malloc_nofail(io->writer_max + 1);

	io->data_base = 0;
	io->data_count = handle_max;
	io->parity_base = handle_max;
	io->parity_count = parity_handle_max;

	for (i = 0; i < io->reader_max; ++i) {
		struct snapraid_worker* worker = &io->reader_map[i];

		worker->io = io;

		if (i < handle_max) {
			/* it's a data read */
			worker->handle = &handle_map[i];
			worker->parity_handle = 0;
			worker->func = data_reader;

			/* data read is put in lower buffer index */
			worker->buffer_skew = 0;
		} else {
			/* it's a parity read */
			worker->handle = 0;
			worker->parity_handle = &parity_handle_map[i - handle_max];
			worker->func = parity_reader;

			/* parity read is put after data and computed parity */
			worker->buffer_skew = parity_handle_max;
		}
	}

	for (i = 0; i < io->writer_max; ++i) {
		struct snapraid_worker* worker = &io->writer_map[i];

		worker->io = io;

		/* it's a parity write */
		worker->handle = 0;
		worker->parity_handle = &parity_handle_map[i];
		worker->func = parity_writer;

		/* parity to write is put after data */
		worker->buffer_skew = handle_max;
	}

#if HAVE_THREAD
	if (io->io_max > 1) {
		io_read_next = io_read_next_thread;
		io_write_preset = io_write_preset_thread;
		io_write_next = io_write_next_thread;
		io_refresh = io_refresh_thread;
		io_data_read = io_data_read_thread;
		io_parity_read = io_parity_read_thread;
		io_parity_write = io_parity_write_thread;
		io_start = io_start_thread;
		io_stop = io_stop_thread;

		thread_mutex_init(&io->io_mutex);
		thread_cond_init(&io->read_done);
		thread_cond_init(&io->read_sched);
		thread_cond_init(&io->write_done);
		thread_cond_init(&io->write_sched);
	} else
#endif
	{
		io_read_next = io_read_next_mono;
		io_write_preset = io_write_preset_mono;
		io_write_next = io_write_next_mono;
		io_refresh = io_refresh_mono;
		io_data_read = io_data_read_mono;
		io_parity_read = io_parity_read_mono;
		io_parity_write = io_parity_write_mono;
		io_start = io_start_mono;
		io_stop = io_stop_mono;
	}
}

void io_done(struct snapraid_io* io)
{
	unsigned i;

	for (i = 0; i < io->io_max; ++i) {
		free(io->buffer_map[i]);
		free(io->buffer_alloc_map[i]);
	}

	free(io->reader_map);
	free(io->reader_list);
	free(io->writer_map);
	free(io->writer_list);

#if HAVE_THREAD
	if (io->io_max > 1) {
		thread_mutex_destroy(&io->io_mutex);
		thread_cond_destroy(&io->read_done);
		thread_cond_destroy(&io->read_sched);
		thread_cond_destroy(&io->write_done);
		thread_cond_destroy(&io->write_sched);
	}
#endif
}

