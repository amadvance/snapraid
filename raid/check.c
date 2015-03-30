/*
 * Copyright (C) 2015 Andrea Mazzoleni
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

#include "internal.h"
#include "combo.h"
#include "gf.h"

/**
 * Validate the provided failed blocks.
 *
 * This function checks if the specified failed blocks satisfy the redundancy
 * information using the data from the known valid parity blocks.
 *
 * It's similar at raid_check(), just with a different format for arguments.
 *
 * The number of failed blocks @nr must be strictly less than the number of
 * parities @nv, because you need one more parity to validate the recovering.
 *
 * No data or parity blocks are modified.
 *
 * @nr Number of failed data blocks.
 * @id[] Vector of @nr indexes of the failed data blocks.
 *   The indexes start from 0. They must be in order.
 * @nv Number of valid parity blocks.
 * @ip[] Vector of @nv indexes of the valid parity blocks.
 *   The indexes start from 0. They must be in order.
 * @nd Number of data blocks.
 * @size Size of the blocks pointed by @v. It must be a multipler of 64.
 * @v Vector of pointers to the blocks of data and parity.
 *   It has (@nd + @ip[@nv - 1] + 1) elements. The starting elements are the
 *   blocks for data, following with the parity blocks.
 *   Each block has @size bytes. 
 * @return 0 if the check is satisfied. -1 otherwise.
 */
static int raid_validate(int nr, int *id, int nv, int *ip, int nd, size_t size, void **vv)
{
	uint8_t **v = (uint8_t **)vv;
	const uint8_t *T[RAID_PARITY_MAX][RAID_PARITY_MAX];
	uint8_t G[RAID_PARITY_MAX * RAID_PARITY_MAX];
	uint8_t V[RAID_PARITY_MAX * RAID_PARITY_MAX];
	size_t i;
	int j, k, l;

	BUG_ON(nr >= nv);

	/* setup the coefficients matrix */
	for (j = 0; j < nr; ++j)
		for (k = 0; k < nr; ++k)
			G[j * nr + k] = A(ip[j], id[k]);

	/* invert it to solve the system of linear equations */
	raid_invert(G, V, nr);

	/* get multiplication tables */
	for (j = 0; j < nr; ++j)
		for (k = 0; k < nr; ++k)
			T[j][k] = table(V[j * nr + k]);

	/* check all positions */
	for (i = 0; i < size; ++i) {
		uint8_t p[RAID_PARITY_MAX];

		/* get parity */
		for (j = 0; j < nv; ++j)
			p[j] = v[nd + ip[j]][i];

		/* compute delta parity, skipping broken disks */
		for (j = 0, k = 0; j < nd; ++j) {
			uint8_t b;

			/* skip broken disks */
			if (k < nr && id[k] == j) {
				++k;
				continue;
			}

			b = v[j][i];
			for (l = 0; l < nv; ++l)
				p[l] ^= gfmul[b][gfgen[ip[l]][j]];
		}

		/* reconstruct data */
		for (j = 0; j < nr; ++j) {
			uint8_t b = 0;
			int idj = id[j];

			/* recompute the data */
			for (k = 0; k < nr; ++k)
				b ^= T[j][k][p[k]];

			/* add the parity contribution of the reconstructed data */
			for (l = nr; l < nv; ++l)
				p[l] ^= gfmul[b][gfgen[ip[l]][idj]];
		}

		/* check that the final parity is 0 */
		for (l = nr; l < nv; ++l)
			if (p[l] != 0)
				return -1;
	}

	return 0;
}

int raid_check(int nr, int *ir, int nd, int np, size_t size, void **v)
{
	/* valid parity index */
	int ip[RAID_PARITY_MAX];
	int vp;
	int rd;
	int i, j;

	/* enforce limit on size */
	BUG_ON(size % 64 != 0);

	/* enforce limit on number of failures */
	BUG_ON(nr >= np); /* >= because we check with extra parity */
	BUG_ON(np > RAID_PARITY_MAX);

	/* enforce order in index vector */
	BUG_ON(nr >= 2 && ir[0] >= ir[1]);
	BUG_ON(nr >= 3 && ir[1] >= ir[2]);
	BUG_ON(nr >= 4 && ir[2] >= ir[3]);
	BUG_ON(nr >= 5 && ir[3] >= ir[4]);
	BUG_ON(nr >= 6 && ir[4] >= ir[5]);

	/* enforce limit on index vector */
	BUG_ON(nr > 0 && ir[nr-1] >= nd + np);

	/* count failed data disk */
	rd = 0;
	while (rd < nr && ir[rd] < nd)
		++rd;

	/* put valid parities into ip[] */
	vp = 0;
	for (i = rd, j = 0; j < np; ++j) {
		/* if parity is failed */
		if (i < nr && ir[i] == nd + j) {
			/* skip broken parity */
			++i;
		} else {
			/* store valid parity */
			ip[vp] = j;
			++vp;
		}
	}

	return raid_validate(rd, ir, vp, ip, nd, size, v);
}

int raid_scan(int *ir, int nd, int np, size_t size, void **v)
{
	int r;

	/* check the special case of no failure */
	if (np != 0 && raid_check(0, 0, nd, np, size, v) == 0)
		return 0;

	/* for each number of possible failures */
	for (r = 1; r < np; ++r) {
		/* try all combinations of r failures on n disks */
		combination_first(r, nd + np, ir);
		do {
			/* verify if the combination is a valid one */
			if (raid_check(r, ir, nd, np, size, v) == 0)
				return r;
		} while (combination_next(r, nd + np, ir));
	}

	/* no solution found */
	return -1;
}

