// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2013 Andrea Mazzoleni

/* Self-test for the RAID library */

#include "internal.h"
#include "cpu.h"

#include <stdio.h>
#include <stdlib.h>

int main(void)
{
	printf("Self-test for the RAID Cauchy library\n\n");

	raid_init();

	printf("Self-test...\n");
	if (raid_selftest() != 0) {
		/* LCOV_EXCL_START */
		printf("FAILED!\n");
		exit(EXIT_FAILURE);
		/* LCOV_EXCL_STOP */
	}
	printf("OK\n\n");

	return 0;
}

