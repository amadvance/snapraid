/*
 * Copyright (C) 2013 Andrea Mazzoleni
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

#include "util.h"
#include "elem.h"
#include "import.h"
#include "state.h"
#include "parity.h"
#include "handle.h"
#include "raid/raid.h"

/****************************************************************************/
/* rehash */

void state_rehash(struct snapraid_state* state)
{
	block_off_t blockmax;
	block_off_t i;

	blockmax = parity_size(state);

	/* check if a rehash is already in progress */
	if (state->prevhash != HASH_UNDEFINED) {
		fprintf(stderr, "You already have a rehash in progress.\n");
		exit(EXIT_FAILURE);
	}

	if (state->hash == state->besthash) {
		fprintf(stderr, "You are already using the best hash for your platform.\n");
		exit(EXIT_FAILURE);
	}

	/* copy the present hash as previous one */
	state->prevhash = state->hash;
	memcpy(state->prevhashseed, state->hashseed, HASH_SIZE);

	/* set the new hash and seed */
	state->hash = state->besthash;
	randomize(state->hashseed, HASH_SIZE);

	/* mark all the block for rehashing */
	for(i=0;i<blockmax;++i) {
		snapraid_info info;

		/* if it's unused */
		info = info_get(&state->infoarr, i);
		if (info == 0) {
			/* skip it */
			continue;
		}

		if (info_get_rehash(info)) {
			fprintf(stderr, "Internal inconsistency for a rehash already in progress\n");
			exit(EXIT_FAILURE);
		}

		/* enable the rehash */
		info = info_set_rehash(info);

		/* save it */
		info_set(&state->infoarr, i, info);
	}

	/* save the new content file */
	state->need_write = 1;

	printf(
		"A rehash is now scheduled. It will take place progressively in the next\n"
		"'sync' and 'scrub' commands. You can check the rehash progress using the\n"
		"'status' command.\n");
}

