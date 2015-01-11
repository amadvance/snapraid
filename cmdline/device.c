/*
 * Copyright (C) 2015 Andrea Mazzoleni
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

#include "support.h"
#include "state.h"

void state_device(struct snapraid_state* state, int operation)
{
	tommy_node* i;
	unsigned j;
	tommy_list list;

	switch (operation) {
	case DEVICE_UP : printf("Spinup...\n"); break;
	case DEVICE_DOWN : printf("Spindown...\n"); break;
	}

	tommy_list_init(&list);

	/* for all disks */
	for (i = state->disklist; i != 0; i = i->next) {
		struct snapraid_disk* disk = i->data;
		devinfo_t* entry;

		entry = calloc_nofail(1, sizeof(devinfo_t));

		entry->device = disk->device;
		pathcpy(entry->name, sizeof(entry->name), disk->name);
		pathcpy(entry->mount, sizeof(entry->mount), disk->dir);

		tommy_list_insert_tail(&list, &entry->node, entry);
	}

	/* for all parities */
	for (j = 0; j < state->level; ++j) {
		devinfo_t* entry;

		entry = calloc_nofail(1, sizeof(devinfo_t));

		entry->device = state->parity[j].device;
		pathcpy(entry->name, sizeof(entry->name), lev_config_name(j));
		pathcpy(entry->mount, sizeof(entry->mount), state->parity[j].path);
		pathcut(entry->mount); /* remove the parity file */

		tommy_list_insert_tail(&list, &entry->node, entry);
	}

	if (devquery(&list, operation) != 0) {
		switch (operation) {
		case DEVICE_UP : fprintf(stderr, "Spinup"); break;
		case DEVICE_DOWN : fprintf(stderr, "Spindown"); break;
		case DEVICE_LIST : fprintf(stderr, "List"); break;
		}
		fprintf(stderr, " unsupported in this platform.\n");
	}

	tommy_list_foreach(&list, free);
}

