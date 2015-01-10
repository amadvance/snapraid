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

void state_spin(struct snapraid_state* state, int operation)
{
	tommy_node* i;
	unsigned j;
	tommy_list list;

	switch (operation) {
	case SPIN_UP : printf("Spinup...\n"); break;
	case SPIN_DOWN : printf("Spindown...\n"); break;
	}

	tommy_list_init(&list);

	/* for all disks */
	for (i = state->disklist; i != 0; i = i->next) {
		struct snapraid_disk* disk = i->data;
		disk_t* entry;

		entry = malloc_nofail(sizeof(disk_t));

		pathcpy(entry->name, sizeof(entry->name), disk->name);
		pathprint(entry->path, sizeof(entry->path), "%s", disk->dir);
		entry->device = disk->device;

		tommy_list_insert_tail(&list, &entry->node, entry);
	}

	/* for all parities */
	for (j = 0; j < state->level; ++j) {
		disk_t* entry;

		entry = malloc_nofail(sizeof(disk_t));

		pathcpy(entry->name, sizeof(entry->name), lev_config_name(j));
		pathprint(entry->path, sizeof(entry->path), "%s", state->parity[j].path);
		pathcut(entry->path); /* remove the parity file */
		entry->device = state->parity[j].device;

		tommy_list_insert_tail(&list, &entry->node, entry);
	}

	if (diskspin(&list, operation) != 0) {
		switch (operation) {
		case SPIN_UP : fprintf(stderr, "Spinup"); break;
		case SPIN_DOWN : fprintf(stderr, "Spindown"); break;
		case SPIN_DEVICES : fprintf(stderr, "List devices"); break;
		}
		fprintf(stderr, " unsupported in this platform.\n");
	}

	tommy_list_foreach(&list, free);
}

