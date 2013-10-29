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

#ifndef __COMBO_H
#define __COMBO_H

#include <assert.h>

/** 
 * Get the first combination of r of n elements.
 * 
 * Typical use is with combination_next() in the form :
 * 
 * int i[R];
 * combination_first(R, N, i);
 * do {
 *    code using i[0], i[1], ..., i[R-1] 
 * } while (combination_next(R, N, i));
 * 
 * It's equivalent at the code :
 * 
 * for(i[0]=0;i[0]<N-(R-1);++i[0])
 *     for(i[1]=i[0]+1;i[1]<N-(R-2);++i[1])
 *        ...
 *            for(i[R-2]=i[R-3]+1;i[R-2]<N-1;++i[R-2])
 *                for(i[R-1]=i[R-2]+1;i[R-1]<N;++i[R-1])
 *                    code using i[0], i[1], ..., i[R-1]
 */
static inline void combination_first(unsigned r, unsigned n, int* c)
{
	unsigned i;

	(void)n; /* unused, but kept for clarity */
	assert(0 < r && r <= n);

	for(i=0;i<r;++i)
		c[i] = i;
}

/** 
 * Get the next combination of r of n elements.
 * Return ==0 when finished.
 */ 
static inline int combination_next(unsigned r, unsigned n, int* c)
{
	unsigned i = r - 1; /* present position */
	int h = n; /* high limit for this position */

recurse:
	/* next element at position i */
	++c[i];

	/* if the position has reached the max */
	if (c[i] >= h) { 

		/* if we are at the first level, we have finished */
		if (i == 0)
			return 0;

		/* increase the previous position */
		--i;
		--h;
		goto recurse;
	}

	++i;

	/* initialize all the next positions, if any */
	while (i < r) {
		c[i] = c[i-1] + 1; /* each position start at the next value of the previous one */
		++i;
	}

	return 1;
}
#endif
