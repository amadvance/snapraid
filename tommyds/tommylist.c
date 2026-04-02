// SPDX-License-Identifier: BSD-2-Clause
// Copyright (C) 2010 Andrea Mazzoleni

#include "tommylist.h"
#include "tommychain.h"

/** \internal
 * Setup a list.
 */
tommy_inline void tommy_list_set(tommy_list* list, tommy_node* head, tommy_node* tail)
{
	head->prev = tail;
	tail->next = 0;
	*list = head;
}

TOMMY_API void tommy_list_sort(tommy_list* list, tommy_compare_func* cmp)
{
	tommy_chain chain;
	tommy_node* head;

	if (tommy_list_empty(list))
		return;

	head = tommy_list_head(list);

	/* create a chain from the list */
	chain.head = head;
	chain.tail = head->prev;

	tommy_chain_mergesort(&chain, cmp);

	/* restore the list */
	tommy_list_set(list, chain.head, chain.tail);
}

