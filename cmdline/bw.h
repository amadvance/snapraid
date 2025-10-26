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

#ifndef __BW_H
#define __BW_H

#include "state.h"
#include "support.h"

/**
 * Bandwidth limiting
 */
struct snapraid_bw {
	uint64_t limit; /**< Bandwidth limit in bytes per second */
	uint64_t total; /**< Remaining bytes allowed in current second */
	uint64_t start; /**< Time when to reset the bandwidth counter */
};

/**
 * Initialize the bandwidth limit
 */
void bw_init(struct snapraid_bw* bw, uint64_t limit);

/**
 * Limit IO bandwidth to stay within the configured limit.
 * If no limit is set, returns immediately.
 * Otherwise sleeps as needed to maintain the rate limit.
 */
void bw_limit(struct snapraid_bw* bw, uint64_t bytes);

#endif

