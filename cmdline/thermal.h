// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 Andrea Mazzoleni
 */

#ifndef __THERMAL_H
#define __THERMAL_H

#include "elem.h"

/****************************************************************************/
/* thermal */

/**
 * Period of temperature measures
 */
#define THERMAL_PERIOD_SECONDS 30

/**
 * Max number of temperature measures
 */
#define THERMAL_MAX 100

/**
 * Minimum value of the R^2 to consider valid a measure
 */
#define THERMAL_R_SQUARED_LIMIT 0.9

struct snapraid_thermal_point {
	double temperature; /**< Temperatures in celsius */
	double time; /**< Time in seconds */
};

struct snapraid_thermal_params {
	double k_heat; /**< Heating coefficient */
	double t_steady; /**< Steady state temperature during heating */
	double t_ambient; /**< Ambient temperature */
	double rmse; /**< Root mean square error of the fit */
	double r_squared; /**< Coefficient of determination */
	double max_error; /**< Maximum absolute error in fit */
};

struct snapraid_thermal {
	uint64_t device; /**< Device ID. */
	char name[PATH_MAX]; /**< Name of the disk. Note that it's not unique as more physical disks may map to the same logical disk */
	int latest_temperature; /**< Latest temperature */
	struct snapraid_thermal_point data[THERMAL_MAX]; /**< Measures. Stopped after the first sleep. */
	unsigned count; /**< Number of measures */
	struct snapraid_thermal_params params; /**< Estimated thermal parameters */
	tommy_node node; /**< Next node in the list. */
};

/**
 * Allocate a thermal state.
 */
struct snapraid_thermal* thermal_alloc(uint64_t dev, const char* name);

/**
 * Deallocate a thermal state.
 */
void thermal_free(struct snapraid_thermal* thermal);

#endif

