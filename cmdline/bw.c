// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2016 Andrea Mazzoleni

#include "portable.h"

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

	uint64_t elapsed = os_tick_ms() - bw->start;
	uint64_t done;
	uint64_t eta;

	done = __atomic_fetch_add(&bw->total, bytes, __ATOMIC_SEQ_CST);

	eta = done * 1000 / bw->limit;

	if (eta > elapsed) {
		eta -= elapsed;
		usleep(eta * 1000);
	}
}

