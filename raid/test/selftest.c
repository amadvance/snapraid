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

/* Self sanity test for the RAID library */

#include "internal.h"
#include "cpu.h"

#include <stdio.h>
#include <stdlib.h>

int main(void)
{
	printf("Self sanity test for the RAID Cauchy library\n\n");

	raid_init();

	printf("Self test...\n");
	if (raid_selftest() != 0) {
		/* LCOV_EXCL_START */
		printf("FAILED!\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}
	printf("OK\n\n");

	return 0;
}

