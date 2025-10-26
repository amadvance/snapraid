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

#include "bw.h"

void bw_init(struct snapraid_bw* bw, uint64_t limit)
{
	bw->limit = limit;
	bw->total = 0;
	bw->start = tick_ms();
}

void bw_limit(struct snapraid_bw* bw, uint64_t bytes)
{
	if (!bw || bw->limit == 0)
		return;

	uint64_t elapsed = tick_ms() - bw->start;
	uint64_t done;
	uint64_t eta;

	done = __atomic_fetch_add(&bw->total, bytes, __ATOMIC_SEQ_CST);

	eta = done * 1000 / bw->limit;

	if (eta > elapsed) {
		eta -= elapsed;
		usleep(eta * 1000);
	}
}

