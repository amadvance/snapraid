/*
 * Copyright (C) 2011 Andrea Mazzoleni
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

#include "util.h"
#include "elem.h"
#include "state.h"
#include "parity.h"
#include "handle.h"

/****************************************************************************/
/* dup */

struct snapraid_hash {
	struct snapraid_disk* disk; /**< Disk. */
	struct snapraid_file* file; /**< File. */
	unsigned char hash[HASH_SIZE]; /**< Hash of the whole file. */

	/* nodes for data structures */
	tommy_node nodelist;
	tommy_hashdyn_node nodeset;
};

struct snapraid_hash* hash_alloc(struct snapraid_state* state, struct snapraid_disk* disk, struct snapraid_file* file)
{
	struct snapraid_hash* hash;
	block_off_t i;
	unsigned char* buf;

	hash = malloc_nofail(sizeof(struct snapraid_hash));
	hash->disk = disk;
	hash->file = file;

	buf = malloc_nofail(file->blockmax * HASH_SIZE);

	/* set the back pointer */
	for(i=0;i<file->blockmax;++i) {
		memcpy(buf + i * HASH_SIZE, file->blockvec[i].hash, HASH_SIZE);

		if (!block_has_updated_hash(&file->blockvec[i])) {
			free(buf);
			return 0;
		}
	}

	memhash(state->besthash, state->hashseed, hash->hash, buf, file->blockmax * HASH_SIZE);

	free(buf);

	return hash;
}

static inline tommy_uint32_t hash_hash(struct snapraid_hash* hash)
{
	return tommy_hash_u32(0, hash->hash, HASH_SIZE);
}

void hash_free(struct snapraid_hash* hash)
{
	free(hash);
}

int hash_compare(const void* void_arg, const void* void_data)
{
	const char* arg = void_arg;
	const struct snapraid_hash* hash = void_data;
	return memcmp(arg, hash->hash, HASH_SIZE);
}

void state_dup(struct snapraid_state* state)
{
	tommy_hashdyn hashset;
	tommy_list hashlist;
	tommy_node* i;
	unsigned count;
	data_off_t size;

	tommy_hashdyn_init(&hashset);
	tommy_list_init(&hashlist);

	count = 0;
	size = 0;
	
	/* for each disk */
	for(i=state->disklist;i!=0;i=i->next) {
		tommy_node* j;
		struct snapraid_disk* disk = i->data;

		/* for each file */
		for(j=disk->filelist;j!=0;j=j->next) {
			struct snapraid_file* file = j->data;
			struct snapraid_hash* hash;
			tommy_hash_t hash32;

			/* if empty, skip it */
			if (file->size == 0)
				continue;

			hash = hash_alloc(state, disk, file);

			/* if no hash, skip it */
			if (!hash)
				continue;

			hash32 = hash_hash(hash);

			struct snapraid_hash* dup = tommy_hashdyn_search(&hashset, hash_compare, hash->hash, hash32);
			if (dup) {
				++count;
				size += dup->file->size;
				if (state->opt.gui) {
					fprintf(stdlog, "dup:%s:%s:%s:%s:%"PRIu64": dup\n", disk->name, file->sub, dup->disk->name, dup->file->sub, dup->file->size);
				} else {
					printf("%"PRIu64" '%s%s'\n", file->size / (1024*1024), disk->dir, file->sub);
					printf("dup '%s%s'\n", dup->disk->dir, dup->file->sub);
				}
				hash_free(hash);
			} else {
				tommy_hashdyn_insert(&hashset, &hash->nodeset, hash, hash32);
				tommy_list_insert_tail(&hashlist, &hash->nodelist, hash);
			}
		}
	}

	tommy_list_foreach(&hashlist, (tommy_foreach_func*)hash_free);
	tommy_hashdyn_done(&hashset);

	if (!state->opt.gui) {
		if (count)
			printf("%u duplicates, for %"PRIu64" MiB.\n", count, size / (1024*1024));
		else
			printf("No duplicate\n");
	}
}

