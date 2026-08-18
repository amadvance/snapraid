// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2013 Andrea Mazzoleni

/* Self-test for the RAID library */

#include "internal.h"
#include "test.h"
#include "cpu.h"

#include <stdio.h>
#include <stdlib.h>

int main(void)
{
	printf("Self-test for the RAID Cauchy library\n\n");

	raid_init();

	printf("Test Cauchy RAID\n");
	if (raid_test_poly(RAID_MODE_CAUCHY_RAID) != 0) {
		/* LCOV_EXCL_START */
		goto bail;
		/* LCOV_EXCL_STOP */
	}
	if (raid_selftest() != 0) {
		/* LCOV_EXCL_START */
		goto bail;
		/* LCOV_EXCL_STOP */
	}

	printf("Test Cauchy AES\n");
	if (raid_test_poly(RAID_MODE_CAUCHY_AES) != 0) {
		/* LCOV_EXCL_START */
		goto bail;
		/* LCOV_EXCL_STOP */
	}
	if (raid_selftest() != 0) {
		/* LCOV_EXCL_START */
		goto bail;
		/* LCOV_EXCL_STOP */
	}

	printf("OK\n");
	return 0;

bail:
	/* LCOV_EXCL_START */
	printf("FAILED!\n");
	exit(EXIT_FAILURE);
	/* LCOV_EXCL_STOP */
}
