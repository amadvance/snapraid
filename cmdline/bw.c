// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2016 Andrea Mazzoleni

#include "os/portable.h"

#include "bw.h"

void bw_init(struct snapraid_bw* bw, uint64_t limit)
{
	bw->limit = limit;
	bw->total = 0;
	bw->start = os_tick_ms();
#if HAVE_THREAD
	thread_mutex_init(&bw->lock);
#endif
}

void bw_done(struct snapraid_bw* bw)
{
#if HAVE_THREAD
	thread_mutex_destroy(&bw->lock);
#else
	(void)bw;
#endif
}

void bw_limit(struct snapraid_bw* bw, uint64_t bytes)
{
	if (!bw || bw->limit == 0)
		return;

#if HAVE_THREAD
	thread_mutex_lock(&bw->lock);
#endif

	uint64_t start = bw->start;
	uint64_t now = os_tick_ms();
	uint64_t elapsed = now - start;
	uint64_t done;
	uint64_t eta;

	bw->total += bytes;
	done = bw->total;

	eta = (done / bw->limit) * 1000 + (done % bw->limit) * 1000 / bw->limit;

	/*
	 * To prevent accumulating unlimited credit during long pauses
	 * (such as CPU-bound hashing or metadata scanning), we cap the
	 * maximum accumulated credit to 10 seconds.
	 * If the elapsed time is more than 10 seconds ahead of the target ETA,
	 * we advance the start time to align the credit to exactly 10 seconds.
	 */
	if (elapsed > eta + 10000) {
		bw->start = now - eta - 10000;
		elapsed = eta + 10000;
	}

	/*
	 * Sleep inside the lock to serialize delays across all worker threads.
	 *
	 * When multiple threads perform IO in parallel, holding the lock
	 * during sleep ensures each thread only sleeps for its own incremental
	 * delay, while waiting threads wait on the mutex and accurately measure
	 * the elapsed time once they acquire the lock.
	 */
	if (eta > elapsed) {
		os_usleep((eta - elapsed) * 1000);
	}

#if HAVE_THREAD
	thread_mutex_unlock(&bw->lock);
#endif
}

