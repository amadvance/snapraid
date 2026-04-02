// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2016 Andrea Mazzoleni
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

