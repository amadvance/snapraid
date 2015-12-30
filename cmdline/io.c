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

void io_init(struct snapraid_io* io, struct snapraid_state* state, unsigned buffer_max,
	void (*data_reader)(struct snapraid_worker*, struct snapraid_task*),
	struct snapraid_handle* handle_map, unsigned handle_max,
	void (*parity_reader)(struct snapraid_worker*, struct snapraid_task*),
	struct snapraid_parity_handle* parity_handle_map, unsigned parity_handle_max)
{
	unsigned i;

	pthread_mutex_init(&io->mutex, 0);
	pthread_cond_init(&io->not_empty, 0);
	pthread_cond_init(&io->not_full, 0);

	io->state = state;

	io->buffer_max = buffer_max;
	for (i = 0; i < IO_MAX; ++i) {
		io->buffer_map[i] = malloc_nofail_vector_align(handle_max, buffer_max, state->block_size, &io->buffer_alloc_map[i]);
		if (!state->opt.skip_self)
			mtest_vector(io->buffer_max, state->block_size, io->buffer_map[i]);
	}

	io->worker_max = handle_max + parity_handle_max;
	io->worker_map = malloc_nofail(sizeof(struct snapraid_worker) * io->worker_max);
	io->worker_list = malloc_nofail(io->worker_max + 1);

	io->data_base = 0;
	io->data_count = handle_max;
	io->parity_base = handle_max;
	io->parity_count = parity_handle_max;

	for (i = 0; i < io->worker_max; ++i) {
		struct snapraid_worker* worker = &io->worker_map[i];

		worker->io = io;
		worker->fs_last = 0;

		if (i < handle_max) {
			worker->handle = &handle_map[i];
			worker->parity_handle = 0;
			worker->func = data_reader;

			/* data read is put in lower buffer index */
			worker->buffer_skew = 0;
		} else {
			worker->handle = 0;
			worker->parity_handle = &parity_handle_map[i - handle_max];
			worker->func = parity_reader;

			/* parity read is put after data and computed parity */
			worker->buffer_skew = parity_handle_max;
		}
	}
}

void io_done(struct snapraid_io* io)
{
	unsigned i;

	for (i = 0; i < IO_MAX; ++i) {
		free(io->buffer_map[i]);
		free(io->buffer_alloc_map[i]);
	}

	free(io->worker_map);
	free(io->worker_list);

	pthread_mutex_destroy(&io->mutex);
	pthread_cond_destroy(&io->not_empty);
	pthread_cond_destroy(&io->not_full);
}

/**
 * Get the next block position to process.
 */
static block_off_t io_position_next(struct snapraid_io* io)
{
	block_off_t blockcur;

	/* get the next position */
	while (io->block_next < io->block_max && !io->block_is_enabled(io->block_arg, io->block_next))
		++io->block_next;

	blockcur = io->block_next;

	/* next block for the next call */
	++io->block_next;

	return blockcur;
}

/**
 * Setup the next pending task for all workers.
 */
static void io_pending_next(struct snapraid_io* io, int index, block_off_t blockcur)
{
	unsigned i;

	for (i = 0; i < io->worker_max; ++i) {
		struct snapraid_worker* worker = &io->worker_map[i];
		struct snapraid_task* task = &worker->task_map[index];

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
		task->buffer = io->buffer_map[index][worker->buffer_skew + i];
		task->position = blockcur;
		task->block = 0;
		task->file = 0;
		task->file_pos = 0;
		task->read_size = 0;
		task->is_timestamp_different = 0;
	}
}

/**
 * Get the next task to work on for a worker.
 *
 * This is the synchronization point for workers with the io.
 */
static struct snapraid_task* io_worker_next(struct snapraid_worker* worker)
{
	struct snapraid_io* io = worker->io;
	struct snapraid_task* task = 0;

	/* the synchronization is protected by the io mutex */
	pthread_mutex_lock(&io->mutex);

	while (1) {
		unsigned index;

		/* check if the worker has to exit */
		if (io->done)
			break;

		/* get the next pending task */
		index = (worker->index + 1) % IO_MAX;

		/* if the queue of pending tasks is not empty */
		if (index != io->index) {
			/* get the new working task */
			worker->index = index;
			task = &worker->task_map[worker->index];

			/* notify the io that we started working */
			pthread_cond_signal(&io->not_empty);

			/* return the new task */
			break;
		}

		/* otherwise wait for a not_full event */
		pthread_cond_wait(&io->not_full, &io->mutex);
	}

	pthread_mutex_unlock(&io->mutex);

	return task;
}

/**
 * Get the next block position to operate on.
 *
 * This is the synchronization point for workers with the io.
 */
block_off_t io_next(struct snapraid_io* io, void*** buffer)
{
	block_off_t blockcur_schedule;
	block_off_t blockcur_caller;
	unsigned i;

	blockcur_schedule = io_position_next(io);

	/* setup the list of workers to process */
	for (i = 0; i <= io->worker_max; ++i)
		io->worker_list[i] = i;

	/* the synchronization is protected by the io mutex */
	pthread_mutex_lock(&io->mutex);

	/* reset the task just used to be the next pending task */
	io_pending_next(io, io->index, blockcur_schedule);

	/* get the next task to return to the caller */
	io->index = (io->index + 1) % IO_MAX;

	/* get the position to operate at high level from one task */
	blockcur_caller = io->worker_map[0].task_map[io->index].position;

	/* set the buffer to use */
	*buffer = io->buffer_map[io->index];

	/* signal all the workers that there is a new pending task */
	pthread_cond_broadcast(&io->not_full);

	pthread_mutex_unlock(&io->mutex);

	return blockcur_caller;
}

static struct snapraid_task* io_task_next(struct snapraid_io* io, unsigned base, unsigned count, unsigned* pos)
{
	/* the synchronization is protected by the io mutex */
	pthread_mutex_lock(&io->mutex);

	while (1) {
		unsigned char* let;

		/* search for a worker that has already finished */
		let = &io->worker_list[0];
		while (1) {
			unsigned i = *let;

			/* if we are at the end */
			if (i == io->worker_max)
				break;

			/* if it's in range */
			if (base <= i && i < base + count) {
				struct snapraid_worker* worker;

				worker = &io->worker_map[i];

				/* if the worker has finished this index */
				if (io->index != worker->index) {
					struct snapraid_task* task;

					task = &worker->task_map[io->index];

					pthread_mutex_unlock(&io->mutex);

					/* mark the worker as processed */
					/* setting the previous one to point at the next one */
					*let = io->worker_list[i + 1];

					/* return the position */
					*pos = i - base;

					return task;
				}
			}

			/* next position to check */
			let = &io->worker_list[i + 1];
		}

		/* if not worker is ready, wait for an event */
		pthread_cond_wait(&io->not_empty, &io->mutex);
	}
}

struct snapraid_task* io_data_next(struct snapraid_io* io, unsigned* pos)
{
	return io_task_next(io, io->data_base, io->data_count, pos);
}

struct snapraid_task* io_parity_next(struct snapraid_io* io, unsigned* pos)
{
	return io_task_next(io, io->parity_base, io->parity_count, pos);
}

static void io_worker(struct snapraid_worker* worker, struct snapraid_task* task)
{
	/* if we reached the end */
	if (task->position >= worker->io->block_max) {
		task->state = TASK_STATE_EMPTY;
	} else {
		worker->func(worker, task);
	}
}

static void* io_thread(void* arg)
{
	struct snapraid_worker* worker = arg;

	/* force completion of the first task */
	io_worker(worker, &worker->task_map[0]);

	while (1) {
		struct snapraid_task* task;

		/* get the new task */
		task = io_worker_next(worker);

		/* if no task, it means to exit */
		if (!task)
			break;

		/* nothing more to do */
		if (task->state == TASK_STATE_EMPTY)
			continue;

		assert(task->state == TASK_STATE_READY);

		/* work on the assigned task */
		io_worker(worker, task);
	}

	return 0;
}

void io_start(struct snapraid_io* io,
	block_off_t blockstart, block_off_t blockmax,
	int (*block_is_enabled)(void* arg,block_off_t), void* blockarg)
{
	unsigned i;

	io->block_start = blockstart;
	io->block_max = blockmax;
	io->block_is_enabled = block_is_enabled;
	io->block_arg = blockarg;
	io->block_next = blockstart;

	io->done = 0;
	io->index = IO_MAX - 1;

	/* setup the initial pending tasks, except the latest one, */
	/* the latest will be initialized at the fist io_next() call */
	for (i = 0; i < IO_MAX - 1; ++i) {
		block_off_t blockcur = io_position_next(io);

		io_pending_next(io, i, blockcur);
	}

	/* start the worker threads */
	for (i = 0; i < io->worker_max; ++i) {
		struct snapraid_worker* worker = &io->worker_map[i];

		worker->index = 0;

		if (pthread_create(&worker->thread, 0, io_thread, worker) != 0) {
			/* LCOV_EXCL_START */
			log_fatal("Failed to create thread.\n");
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}
}

void io_stop(struct snapraid_io* io)
{
	unsigned i;

	pthread_mutex_lock(&io->mutex);

	/* mark that we are stopping */
	io->done = 1;

	/* signal all the threads to recognize the new state */
	pthread_cond_broadcast(&io->not_full);

	pthread_mutex_unlock(&io->mutex);

	/* wait for all threads to terminate */
	for (i = 0; i < io->worker_max; ++i) {
		struct snapraid_worker* worker = &io->worker_map[i];
		void* retval;

		/* wait for thread termination */
		if (pthread_join(worker->thread, &retval) != 0) {
			/* LCOV_EXCL_START */
			log_fatal("Failed to join thread.\n");
			exit(EXIT_FAILURE);
			/* LCOV_EXCL_STOP */
		}
	}
}

