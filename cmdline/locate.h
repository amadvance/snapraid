// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 Andrea Mazzoleni
 */

#ifndef __LOCATE_H
#define __LOCATE_H

/****************************************************************************/
/* snapraid */

#include "state.h"

void state_locate(struct snapraid_state* state, uint64_t parity_tail);

void state_locate_mark_tail_blocks_for_resync(struct snapraid_state* state, uint64_t parity_tail);

#endif

