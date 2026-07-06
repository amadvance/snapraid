// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2011 Andrea Mazzoleni

#include "os/portable.h"

#include "support.h"
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
	unsigned char hash[HASH_MAX]; /**< Hash of the whole file. */

	/* nodes for data structures */
	tommy_hashdyn_node node;
};

struct snapraid_hash* hash_alloc(struct snapraid_state* state, struct snapraid_disk* disk, struct snapraid_file* file)
{
	struct snapraid_hash* hash;
	block_off_t i;
	unsigned char* buf;
	size_t hash_size = BLOCK_HASH_SIZE;

	hash = malloc_nofail(sizeof(struct snapraid_hash));
	hash->disk = disk;
	hash->file = file;

	buf = nalloc_nofail(file->blockmax, hash_size);

	/* set the back pointer */
	for (i = 0; i < file->blockmax; ++i) {
		struct snapraid_block* block = fs_file2block_get(file, i);

		memcpy(buf + i * hash_size, block->hash, hash_size);

		if (!block_has_updated_hash(block)) {
			free(buf);
			free(hash);
			return 0;
		}
	}

	memhash(state->besthash, state->hashseed, hash->hash, buf, file->blockmax * hash_size);

	free(buf);

	return hash;
}

static inline tommy_uint32_t hash_hash(struct snapraid_hash* hash)
{
	return tommy_hash_u32(0, hash->hash, HASH_MAX);
}

void hash_free(struct snapraid_hash* hash)
{
	free(hash);
}

int hash_compare(const void* void_arg, const void* void_data)
{
	const char* arg = void_arg;
	const struct snapraid_hash* hash = void_data;

	return memcmp(arg, hash->hash, HASH_MAX);
}

void state_dup(struct snapraid_state* state)
{
	tommy_hashdyn hashset;
	tommy_node* i;
	unsigned count;
	data_off_t size;
	tommy_hashdyn_init(&hashset);

	count = 0;
	size = 0;

	msg_progress("Comparing...\n");

	/* for each disk */
	for (i = state->disklist; i != 0; i = i->next) {
		tommy_node* j;
		struct snapraid_disk* disk = i->data;

		/* for each file */
		for (j = disk->filelist; j != 0; j = j->next) {
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

			struct snapraid_hash* found = tommy_hashdyn_search(&hashset, hash_compare, hash->hash, hash32);
			if (found) {
				++count;
				size += found->file->size;
				log_tag("dup:%s:%s:%s:%s:%" PRIu64 ": dup\n", disk->name, esc_tag(file->sub), found->disk->name, esc_tag(found->file->sub), found->file->size);
				printf("%12" PRIu64 " %s = %s\n", file->size, fmt_term(disk, file->sub), fmt_term(found->disk, found->file->sub));
				hash_free(hash);
			} else {
				tommy_hashdyn_insert(&hashset, &hash->node, hash, hash32);
			}
		}
	}

	tommy_hashdyn_foreach(&hashset, (tommy_foreach_func*)hash_free);
	tommy_hashdyn_done(&hashset);

	msg_status("\n");
	msg_status("%8u duplicates, for %" PRIu64 " GB\n", count, size / GIGA);
	if (count)
		msg_status("There are duplicates!\n");
	else
		msg_status("No duplicates\n");

	log_tag("summary:dup_count:%u\n", count);
	log_tag("summary:dup_size:%" PRIu64 "\n", size);
	if (count == 0) {
		log_tag("summary:exit:unique\n");
	} else {
		log_tag("summary:exit:dup\n");
	}
	log_flush();
}

