/*
 * Copyright (C) 2025 Andrea Mazzoleni
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

#include "thermal.h"
#include "state.h"
#include "io.h"

#include <math.h>

struct snapraid_thermal* thermal_alloc(uint64_t dev, const char* name)
{
	struct snapraid_thermal* thermal = malloc_nofail(sizeof(struct snapraid_thermal));

	thermal->device = dev;
	thermal->latest_temperature = 0;
	thermal->count = 0;
	pathcpy(thermal->name, sizeof(thermal->name), name);

	return thermal;
}

void thermal_free(struct snapraid_thermal* thermal)
{
	free(thermal);
}

/*
 * Fit exponential heating model to data using least squares
 */
struct snapraid_thermal_params fit_thermal_model(const struct snapraid_thermal_point* points, int n_points, double t_ambient)
{
	struct snapraid_thermal_params model;
	double t_steady_try;
	double k_try;

	memset(&model, 0, sizeof(model));

	model.t_ambient = t_ambient;

	/* at least four points to have a result */
	if (n_points < 4)
		return model;

	double last_temp = points[n_points - 1].temperature;

	/* iterative refinement to find best k_heat and t_steady */
	double best_error = 1e10;
	double best_k = 0;
	double best_t_steady = 0;

	/* grid search for parameters */
	for (t_steady_try = last_temp + 2.0; t_steady_try <= last_temp + 25.0; t_steady_try += 0.5) {
		for (k_try = 0.00001; k_try <= 0.001; k_try *= 1.2) {
			double error = 0.0;
			int i;

			/* calculate error for this parameter set */
			for (i = 0; i < n_points; i++) {
				double t = points[i].time;
				double t_predicted = t_steady_try - (t_steady_try - points[0].temperature) * exp(-k_try * t);
				double diff = points[i].temperature - t_predicted;
				error += diff * diff;
			}

			if (error < best_error) {
				best_error = error;
				best_k = k_try;
				best_t_steady = t_steady_try;
			}
		}
	}

	model.k_heat = best_k;
	model.t_steady = best_t_steady;

	/* calculate quality metrics */
	double sum_squared_residuals = 0.0;
	double sum_total = 0.0;
	double mean_temp = 0.0;
	model.max_error = 0.0;
	int i;

	/* calculate mean temperature */
	for (i = 0; i < n_points; i++)
		mean_temp += points[i].temperature;
	mean_temp /= n_points;

	/* calculate R-squared and errors */
	for (i = 0; i < n_points; i++) {
		double t = points[i].time;
		double t_predicted = model.t_steady - (model.t_steady - points[0].temperature) * exp(-model.k_heat * t);
		double residual = points[i].temperature - t_predicted;

		sum_squared_residuals += residual * residual;
		sum_total += (points[i].temperature - mean_temp) * (points[i].temperature - mean_temp);

		double abs_error = fabs(residual);
		if (abs_error > model.max_error) {
			model.max_error = abs_error;
		}
	}

	model.rmse = sqrt(sum_squared_residuals / n_points);
	model.r_squared = 1.0 - (sum_squared_residuals / sum_total);

	return model;
}

static int smart_temp(devinfo_t* devinfo)
{
	uint64_t t = devinfo->smart[SMART_TEMPERATURE_CELSIUS];

	/* validate temperature */
	if (t == SMART_UNASSIGNED)
		return -1;
	if (t == 0)
		return -1;
	if (t > 100)
		return -1;

	return t;
}

void state_thermal(struct snapraid_state* state, time_t now)
{
	tommy_node* i;
	unsigned j;
	tommy_list high;
	tommy_list low;
	int ret;

	if (state->thermal_temperature_limit == 0)
		return;

	tommy_list_init(&high);
	tommy_list_init(&low);

	/* for all disks */
	for (i = state->disklist; i != 0; i = i->next) {
		struct snapraid_disk* disk = i->data;
		devinfo_t* entry;

		entry = calloc_nofail(1, sizeof(devinfo_t));

		entry->device = disk->device;
		device_name_set(entry, disk->name, 0);
		pathcpy(entry->mount, sizeof(entry->mount), disk->dir);
		pathcpy(entry->smartctl, sizeof(entry->smartctl), disk->smartctl);
		memcpy(entry->smartignore, disk->smartignore, sizeof(entry->smartignore));

		tommy_list_insert_tail(&high, &entry->node, entry);
	}

	/* for all parities */
	for (j = 0; j < state->level; ++j) {
		devinfo_t* entry;
		unsigned s;

		for (s = 0; s < state->parity[j].split_mac; ++s) {
			entry = calloc_nofail(1, sizeof(devinfo_t));

			entry->device = state->parity[j].split_map[s].device;
			device_name_set(entry, lev_config_name(j), s);
			pathcpy(entry->mount, sizeof(entry->mount), state->parity[j].split_map[s].path);
			pathcpy(entry->smartctl, sizeof(entry->smartctl), state->parity[j].smartctl);
			memcpy(entry->smartignore, state->parity[j].smartignore, sizeof(entry->smartignore));
			pathcut(entry->mount); /* remove the parity file */

			tommy_list_insert_tail(&high, &entry->node, entry);
		}
	}

	if (state->opt.fake_device) {
		ret = devtest(&high, &low, DEVICE_SMART);
	} else {
		ret = devquery(&high, &low, DEVICE_SMART, 0 /* only disks in the array */);
	}

	/* on error, just disable thermal gathering */
	if (ret != 0)
		return;

	/* if the list is empty, it's not supported in this platform */
	if (tommy_list_empty(&low))
		return;

	/* if ambient temperature is not set, set it now with the lowest HD temperature */
	if (state->thermal_ambient_temperature == 0) {
		state->thermal_ambient_temperature = ambient_temperature();

		for (i = tommy_list_head(&low); i != 0; i = i->next) {
			devinfo_t* devinfo = i->data;

			int temp = smart_temp(devinfo);
			if (temp < 0)
				continue;

			log_tag("thermal:system:candidate:%d\n", temp);

			if (state->thermal_ambient_temperature == 0 || state->thermal_ambient_temperature > temp)
				state->thermal_ambient_temperature = temp;
		}

		log_tag("thermal:system:final:%d\n", state->thermal_ambient_temperature);
	}

	int highest_temperature = 0;
	for (i = tommy_list_head(&low); i != 0; i = i->next) {
		tommy_node* t;
		struct snapraid_thermal* found;
		devinfo_t* devinfo = i->data;
		unsigned k;

		int temperature = smart_temp(devinfo);
		if (temperature < 0)
			continue;

		/* search of the entry */
		found = 0;
		for (t = tommy_list_head(&state->thermallist); t != 0; t = t->next) {
			struct snapraid_thermal* thermal = t->data;
			if (thermal->device == devinfo->device) {
				found = thermal;
				break;
			}
		}

		/* if not found, create it */
		if (found == 0) {
			found = thermal_alloc(devinfo->device, devinfo->name);
			tommy_list_insert_tail(&state->thermallist, &found->node, found);
		}

		found->latest_temperature = temperature;

		if (highest_temperature < temperature)
			highest_temperature = temperature;

		log_tag("thermal:current:%s:%" PRIu64 ":%d\n", devinfo->name, devinfo->device, temperature);

		if (state->thermal_stop_gathering)
			continue;

		if (found->count + 1 >= THERMAL_MAX) /* keep one extra space at the end */
			continue;

		/* only monotone increasing temperature */
		if (found->count > 0 && found->data[found->count - 1].temperature >= temperature)
			continue;

		/* insert the new data point */
		found->data[found->count].temperature = temperature;
		found->data[found->count].time = now - state->thermal_first;
		++found->count;

		if (state->opt.fake_device) {
			/* fill with fake data */
			found->data[0].time = 0;
			found->data[0].temperature = 27;
			found->data[1].time = 100;
			found->data[1].temperature = 28;
			found->data[2].time = 300;
			found->data[2].temperature = 29;
			found->data[3].time = 700;
			found->data[3].temperature = 30;
			found->data[4].time = 1500;
			found->data[4].temperature = 31;
			found->count = 5;
		}

		/* log the new data */
		log_tag("thermal:heat:%s:%" PRIu64 ":%u:", devinfo->name, devinfo->device, found->count);
		for (k = 0; k < found->count; ++k)
			log_tag("%s%d/%d", k > 0 ? "," : "", (int)found->data[k].temperature, (int)found->data[k].time);
		log_tag("\n");

		/* estimate parameters */
		found->params = fit_thermal_model(found->data, found->count, state->thermal_ambient_temperature);

		log_tag("thermal:params:%s:%" PRIu64 ":%g:%g:%g:%g:%g:%g\n", devinfo->name, devinfo->device,
			found->params.k_heat, found->params.t_ambient, found->params.t_steady,
			found->params.rmse, found->params.r_squared, found->params.max_error);
	}

	/* always update the highest temperature */
	state->thermal_highest_temperature = highest_temperature;

	log_tag("thermal:highest:%d\n", highest_temperature);
	log_flush();

	tommy_list_foreach(&high, free);
	tommy_list_foreach(&low, free);
}

int state_thermal_alarm(struct snapraid_state* state)
{
	/* if no limit, there is no thermal support */
	if (state->thermal_temperature_limit == 0)
		return 0;

	if (state->thermal_highest_temperature <= state->thermal_temperature_limit)
		return 0;

	return 1;
}

void state_thermal_cooldown(struct snapraid_state* state)
{
	int sleep_time = state->thermal_cooldown_time;

	if (sleep_time == 0)
		sleep_time = 5 * 60; /* default sleep time */
	if (sleep_time < 5 * 60)
		sleep_time = 5 * 60; /* minimum sleep time */

	/* from now on, stop any further data gathering as the heating is interrupted */
	state->thermal_stop_gathering = 1;

	log_tag("thermal:spindown\n");
	state_device(state, DEVICE_DOWN, 0);

	msg_progress("Cooldown...\n");

	log_tag("thermal:cooldown:%d\n", sleep_time);
	printf("Waiting for %d minutes...\n", sleep_time / 60);

	log_flush();

	/* every 30 seconds spin down any disk that was spunup */
	while (sleep_time > 0) {
		state_device(state, DEVICE_DOWNIFUP, 0);

		sleep(30);
		sleep_time -= 30;
	}

	if (!global_interrupt) { /* don't wake-up if we are interrupting */
		log_tag("thermal:spinup\n");

		/* spinup */
		state_device(state, DEVICE_UP, 0);

		/* log new thermal info */
		state_thermal(state, 0);
	}
}

int state_thermal_begin(struct snapraid_state* state, time_t now)
{
	if (state->thermal_temperature_limit == 0)
		return 1;

	/* initial thermal measure */
	state->thermal_first = now;
	state->thermal_latest = now;
	state_thermal(state, now);

	if (state->thermal_ambient_temperature != 0) {
		printf("System temperature is %u degrees\n", state->thermal_ambient_temperature);

		if (state->thermal_temperature_limit != 0 && state->thermal_temperature_limit <= state->thermal_ambient_temperature) {
			/* LCOV_EXCL_START */
			log_fatal(EENVIRONMENT, "DANGER! System temperature of %d degrees is higher than the temperature limit of %d degrees. Unable to proceed!\n", state->thermal_ambient_temperature, state->thermal_temperature_limit);
			log_flush();
			return 0;
			/* LCOV_EXCL_STOP */
		}
	}

	if (state_thermal_alarm(state)) {
		/* LCOV_EXCL_START */
		log_fatal(EENVIRONMENT, "DANGER! Hard disk temperature of %d degrees is already outside the operating range. Unable to proceed!\n", state->thermal_highest_temperature);
		log_flush();
		return 0;
		/* LCOV_EXCL_STOP */
	}

	return 1;
}

