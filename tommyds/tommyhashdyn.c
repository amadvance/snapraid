// SPDX-License-Identifier: BSD-2-Clause
// Copyright (C) 2010 Andrea Mazzoleni

#include "tommyhashdyn.h"
#include "tommylist.h"

#include <string.h> /* for memset */

/******************************************************************************/
/* hashdyn */

TOMMY_API void tommy_hashdyn_init(tommy_hashdyn* hashdyn)
{
	/* fixed initial size */
	hashdyn->bucket_bit = TOMMY_HASHDYN_BIT;
	hashdyn->bucket_max = (tommy_size_t)1 << hashdyn->bucket_bit;
	hashdyn->bucket_mask = hashdyn->bucket_max - 1;
	hashdyn->bucket = tommy_cast(tommy_hashdyn_node**, tommy_calloc(hashdyn->bucket_max, sizeof(tommy_hashdyn_node*)));

	hashdyn->count = 0;
}

TOMMY_API void tommy_hashdyn_done(tommy_hashdyn* hashdyn)
{
	tommy_free(hashdyn->bucket);
}

TOMMY_API void tommy_hashdyn_clear(tommy_hashdyn* hashdyn)
{
	memset(hashdyn->bucket, 0, hashdyn->bucket_max * sizeof(tommy_hashdyn_node*));
	hashdyn->count = 0;
}

/**
 * Resize the bucket vector.
 */
static void tommy_hashdyn_resize(tommy_hashdyn* hashdyn, tommy_uint_t new_bucket_bit)
{
	tommy_size_t bucket_bit = hashdyn->bucket_bit;
	tommy_size_t bucket_max = hashdyn->bucket_max;
	tommy_size_t new_bucket_max = (tommy_size_t)1 << new_bucket_bit;
	tommy_size_t new_bucket_mask = new_bucket_max - 1;
	tommy_hashdyn_node** new_bucket;

	if (!hashdyn->count) {
		/* empty table: allocate zeroed memory with calloc without looping */
		new_bucket = tommy_cast(tommy_hashdyn_node**, tommy_calloc(new_bucket_max, sizeof(tommy_hashdyn_node*)));
	} else if (new_bucket_bit > bucket_bit) {
		if (new_bucket_bit == bucket_bit + 1) {
			/* grow by 1 bit: zero the two buckets inline in loop to preserve cache locality */
			new_bucket = tommy_cast(tommy_hashdyn_node**, tommy_malloc(new_bucket_max * sizeof(tommy_hashdyn_node*)));

			for (tommy_size_t i = 0; i < bucket_max; ++i) {
				new_bucket[i] = 0;
				new_bucket[i + bucket_max] = 0;

				tommy_hashdyn_node* j = hashdyn->bucket[i];
				while (j) {
					tommy_hashdyn_node* j_next = j->next;
					tommy_size_t pos = j->index & new_bucket_mask;
					if (new_bucket[pos])
						tommy_list_insert_tail_not_empty(new_bucket[pos], j);
					else
						tommy_list_insert_first(&new_bucket[pos], j);
					j = j_next;
				}
			}
		} else {
			/* grow by multiple bits with elements: allocate zeroed memory and reinsert */
			new_bucket = tommy_cast(tommy_hashdyn_node**, tommy_calloc(new_bucket_max, sizeof(tommy_hashdyn_node*)));

			for (tommy_size_t i = 0; i < bucket_max; ++i) {
				tommy_hashdyn_node* j = hashdyn->bucket[i];

				while (j) {
					tommy_hashdyn_node* j_next = j->next;
					tommy_size_t pos = j->index & new_bucket_mask;
					if (new_bucket[pos])
						tommy_list_insert_tail_not_empty(new_bucket[pos], j);
					else
						tommy_list_insert_first(&new_bucket[pos], j);
					j = j_next;
				}
			}
		}
	} else {
		/* all buckets are overwritten, no pre-zeroing needed */
		new_bucket = tommy_cast(tommy_hashdyn_node**, tommy_malloc(new_bucket_max * sizeof(tommy_hashdyn_node*)));

		if (new_bucket_bit + 1 == bucket_bit) {
			/* shrink by 1 bit: each new bucket joins exactly two old buckets */
			for (tommy_size_t i = 0; i < new_bucket_max; ++i) {
				new_bucket[i] = hashdyn->bucket[i];
				tommy_list_concat(&new_bucket[i], &hashdyn->bucket[i + new_bucket_max]);
			}
		} else {
			/* shrink by multiple bits */
			for (tommy_size_t i = 0; i < new_bucket_max; ++i) {
				/* all old buckets with the same new modulus must be concatenated. */
				new_bucket[i] = hashdyn->bucket[i];
				for (tommy_size_t j = i + new_bucket_max; j < bucket_max; j += new_bucket_max)
					tommy_list_concat(&new_bucket[i], &hashdyn->bucket[j]);
			}
		}
	}

	tommy_free(hashdyn->bucket);

	/* setup */
	hashdyn->bucket_bit = new_bucket_bit;
	hashdyn->bucket_max = new_bucket_max;
	hashdyn->bucket_mask = new_bucket_mask;
	hashdyn->bucket = new_bucket;
}

/**
 * Grow.
 */
tommy_inline void hashdyn_grow_step(tommy_hashdyn* hashdyn)
{
	/* grow if more than 50% full */
	if (hashdyn->count >= hashdyn->bucket_max / 2)
		tommy_hashdyn_resize(hashdyn, hashdyn->bucket_bit + 1);
}

/**
 * Shrink.
 */
tommy_inline void hashdyn_shrink_step(tommy_hashdyn* hashdyn)
{
	/* shrink if less than 12.5% full */
	if (hashdyn->count <= hashdyn->bucket_max / 8 && hashdyn->bucket_bit > TOMMY_HASHDYN_BIT)
		tommy_hashdyn_resize(hashdyn, hashdyn->bucket_bit - 1);
}

TOMMY_API void tommy_hashdyn_reserve(tommy_hashdyn* hashdyn, tommy_size_t count)
{
	tommy_uint_t bucket_bit = tommy_ilog2(count | 1) + 2;

	if (bucket_bit < TOMMY_HASHDYN_BIT)
		bucket_bit = TOMMY_HASHDYN_BIT;

	if (bucket_bit <= hashdyn->bucket_bit)
		return;

	tommy_hashdyn_resize(hashdyn, bucket_bit);
}

TOMMY_API void tommy_hashdyn_shrink(tommy_hashdyn* hashdyn)
{
	tommy_uint_t bucket_bit = tommy_ilog2(hashdyn->count | 1) + 2;

	if (bucket_bit < TOMMY_HASHDYN_BIT)
		bucket_bit = TOMMY_HASHDYN_BIT;

	if (bucket_bit >= hashdyn->bucket_bit)
		return;

	tommy_hashdyn_resize(hashdyn, bucket_bit);
}

TOMMY_API void tommy_hashdyn_insert(tommy_hashdyn* hashdyn, tommy_hashdyn_node* node, void* data, tommy_hash_t hash)
{
	tommy_size_t pos = hash & hashdyn->bucket_mask;

	tommy_list_insert_tail(&hashdyn->bucket[pos], node, data);

	node->index = hash;

	++hashdyn->count;

	hashdyn_grow_step(hashdyn);
}

TOMMY_API void* tommy_hashdyn_insert_unique(tommy_hashdyn* hashdyn, tommy_hashdyn_node* node, void* data, tommy_search_func* cmp, const void* cmp_arg, tommy_hash_t hash)
{
	void* existing = tommy_hashdyn_search(hashdyn, cmp, cmp_arg, hash);
	if (existing)
		return existing;

	tommy_hashdyn_insert(hashdyn, node, data, hash);
	return data;
}

TOMMY_API void tommy_hashdyn_rehash_existing(tommy_hashdyn* hashdyn, tommy_hashdyn_node* node, tommy_hash_t hash)
{
	if (node->index == hash)
		return;

	/* unlink using the stored hash, without invoking the resize policy. */
	tommy_list_remove_existing(&hashdyn->bucket[node->index & hashdyn->bucket_mask], node);
	node->index = hash;
	tommy_list_insert_tail(&hashdyn->bucket[hash & hashdyn->bucket_mask], node, node->data);
}

TOMMY_API void* tommy_hashdyn_remove_existing(tommy_hashdyn* hashdyn, tommy_hashdyn_node* node)
{
	tommy_size_t pos = node->index & hashdyn->bucket_mask;

	tommy_list_remove_existing(&hashdyn->bucket[pos], node);

	--hashdyn->count;

	hashdyn_shrink_step(hashdyn);

	return node->data;
}

TOMMY_API void* tommy_hashdyn_remove(tommy_hashdyn* hashdyn, tommy_search_func* cmp, const void* cmp_arg, tommy_hash_t hash)
{
	tommy_size_t pos = hash & hashdyn->bucket_mask;
	tommy_hashdyn_node* node = hashdyn->bucket[pos];

	while (node) {
		/* we first check if the hash matches, as in the same bucket we may have multiples hash values */
		if (node->index == hash && cmp(cmp_arg, node->data) == 0) {
			tommy_list_remove_existing(&hashdyn->bucket[pos], node);

			--hashdyn->count;

			hashdyn_shrink_step(hashdyn);

			return node->data;
		}
		node = node->next;
	}

	return 0;
}

TOMMY_API void tommy_hashdyn_foreach(tommy_hashdyn* hashdyn, tommy_foreach_func* func)
{
	tommy_size_t bucket_max = hashdyn->bucket_max;
	tommy_hashdyn_node** bucket = hashdyn->bucket;

	for (tommy_size_t pos = 0; pos < bucket_max; ++pos) {
		tommy_hashdyn_node* node = bucket[pos];

		while (node) {
			void* data = node->data;
			node = node->next;
			func(data);
		}
	}
}

TOMMY_API void tommy_hashdyn_foreach_arg(tommy_hashdyn* hashdyn, tommy_foreach_arg_func* func, void* arg)
{
	tommy_size_t bucket_max = hashdyn->bucket_max;
	tommy_hashdyn_node** bucket = hashdyn->bucket;

	for (tommy_size_t pos = 0; pos < bucket_max; ++pos) {
		tommy_hashdyn_node* node = bucket[pos];

		while (node) {
			void* data = node->data;
			node = node->next;
			func(arg, data);
		}
	}
}

TOMMY_API tommy_size_t tommy_hashdyn_memory_usage(tommy_hashdyn* hashdyn)
{
	return hashdyn->bucket_max * (tommy_size_t)sizeof(hashdyn->bucket[0])
	       + tommy_hashdyn_count(hashdyn) * (tommy_size_t)sizeof(tommy_hashdyn_node);
}

TOMMY_API void tommy_hashdyn_to_list(tommy_hashdyn* hashdyn, tommy_list* list)
{
	/* move everything to the list */
	for (tommy_size_t pos = 0; pos < hashdyn->bucket_max; ++pos)
		tommy_list_concat(list, &hashdyn->bucket[pos]);

	/* clear all */
	tommy_hashdyn_clear(hashdyn);
}

