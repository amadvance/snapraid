// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2016 Andrea Mazzoleni

#include "os/portable.h"

#include "bw.h"

void bw_init(struct snapraid_bw* bw, uint64_t limit)
{
	bw->limit = limit;
	bw->total = 0;
	bw->start = os_tick_ms();
}

void bw_limit(struct snapraid_bw* bw, uint64_t bytes)
{
	if (!bw || bw->limit == 0)
		return;

	uint64_t start = bw->start;
	uint64_t now = os_tick_ms();
	uint64_t elapsed = now - start;
	uint64_t done;
	uint64_t eta;

	done = __atomic_fetch_add(&bw->total, bytes, __ATOMIC_SEQ_CST);

	eta = done * 1000 / bw->limit;

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

	if (eta > elapsed) {
		eta -= elapsed;
		if (eta > 1000)
			eta = 1000;
		usleep((unsigned)eta * 1000);
	}
}

