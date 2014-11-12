/*
 * Copyright (C) 2013 Andrea Mazzoleni
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __RAID_COMBO_H
#define __RAID_COMBO_H

#include <assert.h>

/**
 * Get the first permutation with repetition of r of n elements.
 *
 * Typical use is with permutation_next() in the form :
 *
 * int i[R];
 * permutation_first(R, N, i);
 * do {
 *    code using i[0], i[1], ..., i[R-1]
 * } while (permutation_next(R, N, i));
 *
 * It's equivalent at the code :
 *
 * for(i[0]=0;i[0]<N;++i[0])
 *     for(i[1]=0;i[1]<N;++i[1])
 *        ...
 *            for(i[R-2]=0;i[R-2]<N;++i[R-2])
 *                for(i[R-1]=0;i[R-1]<N;++i[R-1])
 *                    code using i[0], i[1], ..., i[R-1]
 */
static __always_inline void permutation_first(int r, int n, int *c)
{
	int i;

	(void)n; /* unused, but kept for clarity */
	assert(0 < r && r <= n);

	for (i = 0; i < r; ++i)
		c[i] = 0;
}

/**
 * Get the next permutation with repetition of r of n elements.
 * Return ==0 when finished.
 */
static __always_inline int permutation_next(int r, int n, int *c)
{
	int i = r - 1; /* present position */

recurse:
	/* next element at position i */
	++c[i];

	/* if the position has reached the max */
	if (c[i] >= n) {

		/* if we are at the first level, we have finished */
		if (i == 0)
			return 0;

		/* increase the previous position */
		--i;
		goto recurse;
	}

	++i;

	/* initialize all the next positions, if any */
	while (i < r) {
		c[i] = 0;
		++i;
	}

	return 1;
}

/**
 * Get the first combination without repetition of r of n elements.
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
static __always_inline void combination_first(int r, int n, int *c)
{
	int i;

	(void)n; /* unused, but kept for clarity */
	assert(0 < r && r <= n);

	for (i = 0; i < r; ++i)
		c[i] = i;
}

/**
 * Get the next combination without repetition of r of n elements.
 * Return ==0 when finished.
 */
static __always_inline int combination_next(int r, int n, int *c)
{
	int i = r - 1; /* present position */
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
		/* each position start at the next value of the previous one */
		c[i] = c[i - 1] + 1;
		++i;
	}

	return 1;
}
#endif

