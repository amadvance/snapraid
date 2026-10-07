// SPDX-License-Identifier: BSD-2-Clause
// Copyright (C) 2010 Andrea Mazzoleni

/** \file
 * Double linked list for collisions into hashtables.
 *
 * This list is a **doubly** linked list mainly targeted for handling collisions
 * into an hashtable, but **usable** also as a generic list.
 *
 * The main feature of this list is to require only one pointer to represent the
 * list, compared to a classic implementation requiring a head **and** a tail pointers.
 * This reduces the memory usage in hashtables.
 *
 * Another feature is to support the insertion at the end of the list. This **allows** to store
 * collisions in a stable order. Where for stable order we mean that equal elements keep
 * their insertion order.
 *
 * To initialize the list, you have to call tommy_list_init(), or to simply assign
 * to it NULL, as an empty list is represented by the NULL value.
 *
 * \code
 * tommy_list list;
 *
 * tommy_list_init(&list); // initializes the list
 * \endcode
 *
 * To insert elements in the list you have to call tommy_list_insert_tail()
 * or tommy_list_insert_head() for each element.
 * In the insertion call you have to specify the address of the node and the
 * address of the object.
 * The address of the object is used to initialize the tommy_node::data field
 * of the node.
 *
 * \code
 * struct object {
 *     int value;
 *     // other fields
 *     tommy_node node;
 * };
 *
 * struct object* obj = malloc(sizeof(struct object)); // creates the object
 *
 * obj->value = ...; // initializes the object
 *
 * tommy_list_insert_tail(&list, &obj->node, obj); // inserts the object
 * \endcode
 *
 * To iterate over all the elements in the list you have to call
 * tommy_list_head() to get the head of the list and follow the
 * tommy_node::next pointer until NULL.
 *
 * \code
 * tommy_node* i = tommy_list_head(&list);
 * while (i) {
 *     struct object* obj = i->data; // gets the object pointer
 *
 *     printf("%d\n", obj->value); // process the object
 *
 *     i = i->next; // go to the next element
 * }
 * \endcode
 *
 * To iterate backward, start at tommy_list_tail() and use tommy_list_prev().
 * Do not follow tommy_node::prev directly past the head, as it points to the tail.
 *
 * \code
 * tommy_node* i = tommy_list_tail(&list);
 * while (i) {
 *     struct object* obj = i->data;
 *
 *     printf("%d\n", obj->value);
 *
 *     i = tommy_list_prev(&list, i);
 * }
 * \endcode
 *
 * To reorder existing elements, use tommy_list_move_head(), tommy_list_move_tail()
 * or tommy_list_reverse(). To split a list or transfer elements between lists,
 * use tommy_list_split(), tommy_list_prepend(), tommy_list_splice_before()
 * and tommy_list_splice_after().
 * These operations preserve the object pointers and do not allocate memory.
 *
 * \code
 * tommy_list second;
 * tommy_list_init(&second);
 *
 * // transfers node and all following nodes to second
 * tommy_list_split(&list, node, &second);
 *
 * // appends the transferred nodes back to list
 * tommy_list_concat(&list, &second);
 * \endcode
 *
 * Two lists sorted with the same comparison function can be merged with
 * tommy_list_merge(). The merge is stable.
 *
 * To destroy the list you have to remove all the elements,
 * as the list is completely inplace and it doesn't allocate memory.
 * This can be done with the tommy_list_foreach() function.
 *
 * \code
 * // deallocates all the objects iterating the list
 * tommy_list_foreach(&list, free);
 * tommy_list_init(&list); // resets the list before reuse
 * \endcode
 */

#ifndef __TOMMYLIST_H
#define __TOMMYLIST_H

#include "tommytypes.h"

/******************************************************************************/
/* list */

/**
 * Doubly linked list type.
 */
typedef tommy_node* tommy_list;

/**
 * Initializes the list.
 * The list is completely inplace, so it doesn't need to be deinitialized.
 */
tommy_inline void tommy_list_init(tommy_list* list)
{
	*list = 0;
}

/**
 * Gets the head of the list.
 * \return The head node. For empty lists 0 is returned.
 */
tommy_inline tommy_node* tommy_list_head(tommy_list* list)
{
	return *list;
}

/**
 * Gets the tail of the list.
 * \return The tail node. For empty lists 0 is returned.
 */
tommy_inline tommy_node* tommy_list_tail(tommy_list* list)
{
	tommy_node* head = tommy_list_head(list);

	if (!head)
		return 0;

	return head->prev;
}

/**
 * Gets the previous node in the list.
 * Unlike tommy_node::prev, this function returns 0 at the head of the list,
 * allowing backward iteration starting from tommy_list_tail().
 * \param list The list.
 * \param node The current node. The node must be in the list.
 * \return The previous node, or 0 if the current node is the head.
 */
tommy_inline tommy_node* tommy_list_prev(tommy_list* list, tommy_node* node)
{
	if (node == tommy_list_head(list))
		return 0;

	return node->prev;
}

/** \internal
 * Creates a new list with a single element.
 * \param list The list to initialize.
 * \param node The node to insert.
 */
tommy_inline void tommy_list_insert_first(tommy_list* list, tommy_node* node)
{
	/* one element "circular" prev list */
	node->prev = node;

	/* one element "0 terminated" next list */
	node->next = 0;

	*list = node;
}

/** \internal
 * Inserts an element at the head of a non-empty list.
 * The element is inserted at the head of the list. The list cannot be empty.
 * \param list The list. The list cannot be empty.
 * \param node The node to insert.
 */
tommy_inline void tommy_list_insert_head_not_empty(tommy_list* list, tommy_node* node)
{
	tommy_node* head = tommy_list_head(list);

	/* insert in the "circular" prev list */
	node->prev = head->prev;
	head->prev = node;

	/* insert in the "0 terminated" next list */
	node->next = head;

	*list = node;
}

/** \internal
 * Inserts an element at the tail of a non-empty list.
 * The element is inserted at the tail of the list. The list cannot be empty.
 * \param head The node at the list head. It cannot be 0.
 * \param node The node to insert.
 */
tommy_inline void tommy_list_insert_tail_not_empty(tommy_node* head, tommy_node* node)
{
	/* insert in the "circular" prev list */
	node->prev = head->prev;
	head->prev = node;

	/* insert in the "0 terminated" next list */
	node->next = 0;
	node->prev->next = node;
}

/**
 * Inserts an element at the head of a list.
 * \param list The list.
 * \param node The node to insert.
 * \param data The object containing the node. It's used to set the tommy_node::data field of the node.
 */
tommy_inline void tommy_list_insert_head(tommy_list* list, tommy_node* node, void* data)
{
	tommy_node* head = tommy_list_head(list);

	if (head)
		tommy_list_insert_head_not_empty(list, node);
	else
		tommy_list_insert_first(list, node);

	node->data = data;
}

/**
 * Inserts an element at the tail of a list.
 * \param list The list.
 * \param node The node to insert.
 * \param data The object containing the node. It's used to set the tommy_node::data field of the node.
 */
tommy_inline void tommy_list_insert_tail(tommy_list* list, tommy_node* node, void* data)
{
	tommy_node* head = tommy_list_head(list);

	if (head)
		tommy_list_insert_tail_not_empty(head, node);
	else
		tommy_list_insert_first(list, node);

	node->data = data;
}

/**
 * Inserts an element before the specified reference node.
 * \param list The list.
 * \param reference The reference node. The new node will be inserted before this node. The reference node must be in the list.
 * \param node The node to insert.
 * \param data The object containing the node. It's used to set the tommy_node::data field of the node.
 */
tommy_inline void tommy_list_insert_before(tommy_list* list, tommy_node* reference, tommy_node* node, void* data)
{
	tommy_node* head = tommy_list_head(list);

	/* if inserting before the head, update the list pointer */
	if (head == reference) {
		tommy_list_insert_head_not_empty(list, node);
	} else {
		/* insert in the "circular" prev list */
		node->prev = reference->prev;
		reference->prev = node;

		/* insert in the "0 terminated" next list */
		node->next = reference;
		node->prev->next = node;
	}

	node->data = data;
}

/**
 * Inserts an element after the specified reference node.
 * \param list The list.
 * \param reference The reference node. The new node will be inserted after this node. The reference node must be in the list.
 * \param node The node to insert.
 * \param data The object containing the node. It's used to set the tommy_node::data field of the node.
 */
tommy_inline void tommy_list_insert_after(tommy_list* list, tommy_node* reference, tommy_node* node, void* data)
{
	tommy_node* head = tommy_list_head(list);

	/* if inserting after the tail (reference->next == 0), handle specially */
	if (reference->next == 0) {
		tommy_list_insert_tail_not_empty(head, node);
	} else {
		/* insert in the "circular" prev list */
		node->prev = reference;
		reference->next->prev = node;

		/* insert in the "0 terminated" next list */
		node->next = reference->next;
		reference->next = node;
	}

	node->data = data;
}

/**
 * Removes an element from the list.
 * You must already have the address of the element to remove.
 * \note The node content is left unchanged, including the tommy_node::next
 * and tommy_node::prev fields that still contain pointers **in** the list.
 * \param list The list.
 * \param node The node to remove. The node must be in the list.
 * \return The tommy_node::data field of the node removed.
 */
tommy_inline void* tommy_list_remove_existing(tommy_list* list, tommy_node* node)
{
	tommy_node* head = tommy_list_head(list);

	/* remove from the "circular" prev list */
	if (node->next)
		node->next->prev = node->prev;
	else
		head->prev = node->prev; /* the last */

	/* remove from the "0 terminated" next list */
	if (head == node)
		*list = node->next; /* the new head, in case 0 */
	else
		node->prev->next = node->next;

	return node->data;
}

/**
 * Removes the element at the head of the list.
 * The object is not deallocated.
 * \note The removed node content is left unchanged, as in tommy_list_remove_existing().
 * \param list The list.
 * \return The tommy_node::data field of the node removed, or 0 if the list is empty.
 */
tommy_inline void* tommy_list_remove_head(tommy_list* list)
{
	tommy_node* node = tommy_list_head(list);

	if (!node)
		return 0;

	return tommy_list_remove_existing(list, node);
}

/**
 * Removes the element at the tail of the list.
 * The object is not deallocated.
 * \note The removed node content is left unchanged, as in tommy_list_remove_existing().
 * \param list The list.
 * \return The tommy_node::data field of the node removed, or 0 if the list is empty.
 */
tommy_inline void* tommy_list_remove_tail(tommy_list* list)
{
	tommy_node* node = tommy_list_tail(list);

	if (!node)
		return 0;

	return tommy_list_remove_existing(list, node);
}

/**
 * Moves an existing node to the head of the list.
 * If the node is already at the head, nothing is done.
 * The tommy_node::data and tommy_node::index fields are left unchanged.
 * \param list The list.
 * \param node The node to move. The node must be in the list.
 */
tommy_inline void tommy_list_move_head(tommy_list* list, tommy_node* node)
{
	if (node == tommy_list_head(list))
		return;

	tommy_list_remove_existing(list, node);
	tommy_list_insert_head(list, node, node->data);
}

/**
 * Moves an existing node to the tail of the list.
 * If the node is already at the tail, nothing is done.
 * The tommy_node::data and tommy_node::index fields are left unchanged.
 * \param list The list.
 * \param node The node to move. The node must be in the list.
 */
tommy_inline void tommy_list_move_tail(tommy_list* list, tommy_node* node)
{
	if (node == tommy_list_tail(list))
		return;

	tommy_list_remove_existing(list, node);
	tommy_list_insert_tail(list, node, node->data);
}

/**
 * Splits a list before the specified node.
 * The list retains the nodes preceding the specified node. The second list
 * receives the specified node and all following nodes. The order of the elements is preserved.
 * The tommy_node::data and tommy_node::index fields are left unchanged.
 * \param list The list to split.
 * \param node The first node to transfer. The node must be in the list,
 * or 0 to leave list unchanged and set second to 0.
 * Specifying the head transfers the whole list.
 * \param second The list receiving node and all following nodes.
 * It must be distinct from list and need not be initialized. Its previous value
 * is overwritten without removing or deallocating any nodes previously referenced by second.
 * \note This operation is O(1).
 */
tommy_inline void tommy_list_split(tommy_list* list, tommy_node* node, tommy_list* second)
{
	tommy_node* head = tommy_list_head(list);
	tommy_node* tail;
	tommy_node* before;

	if (!node) {
		*second = 0;
		return;
	}

	if (node == head) {
		*second = *list;
		*list = 0;
		return;
	}

	tail = head->prev;
	before = node->prev;

	/* terminate the retained list */
	head->prev = before;
	before->next = 0;

	/* create the list of transferred nodes */
	node->prev = tail;
	*second = node;
}

/**
 * Exchanges the contents of two lists.
 * The tommy_node::data and tommy_node::index fields are left unchanged.
 * \param first The first list.
 * \param second The second list. The lists must not share nodes,
 * unless first and second are the same pointer, in which case nothing is done.
 * \note This operation is O(1).
 */
tommy_inline void tommy_list_swap(tommy_list* first, tommy_list* second)
{
	tommy_list tmp = *first;
	*first = *second;
	*second = tmp;
}

/**
 * Concats two lists.
 * The second list is concatenated at the first list.
 * \param first The first list.
 * \param second The second list. After this call the list content is undefined,
 * and you should not use it anymore.
 */
tommy_inline void tommy_list_concat(tommy_list* first, tommy_list* second)
{
	/* if the second is empty, nothing to do */
	tommy_node* second_head = tommy_list_head(second);
	if (second_head == 0)
		return;

	/* if the first is empty, copy the second */
	tommy_node* first_head = tommy_list_head(first);
	if (first_head == 0) {
		*first = *second;
		return;
	}

	/* tail of the first list */
	tommy_node* first_tail = first_head->prev;

	/* set the "circular" prev list */
	first_head->prev = second_head->prev;
	second_head->prev = first_tail;

	/* set the "0 terminated" next list */
	first_tail->next = second_head;
}

/**
 * Prepends two lists.
 * The second list is prepended at the first list.
 * \param first The first list.
 * \param second The second list. After this call the list content is undefined,
 * and you should not use it anymore.
 */
tommy_inline void tommy_list_prepend(tommy_list* first, tommy_list* second)
{
	tommy_list_concat(second, first);
	*first = *second;
}

/**
 * Transfers all elements of a list before the specified reference node.
 * The order of the transferred elements is preserved.
 * The tommy_node::data and tommy_node::index fields are left unchanged.
 * \param list The destination list.
 * \param reference The node before which to insert. It must be nonzero and in the destination list.
 * \param second The source list. After this call the list content is undefined,
 * and you should not use it anymore.
 * The lists must be distinct and must not share nodes.
 * \note This operation is O(1). An empty source list has no effect.
 */
TOMMY_API void tommy_list_splice_before(tommy_list* list, tommy_node* reference, tommy_list* second);

/**
 * Transfers all elements of a list after the specified reference node.
 * The order of the transferred elements is preserved.
 * The tommy_node::data and tommy_node::index fields are left unchanged.
 * \param list The destination list.
 * \param reference The node after which to insert. It must be nonzero and in the destination list.
 * \param second The source list. After this call the list content is undefined,
 * and you should not use it anymore.
 * The lists must be distinct and must not share nodes.
 * \note This operation is O(1). An empty source list has no effect.
 */
TOMMY_API void tommy_list_splice_after(tommy_list* list, tommy_node* reference, tommy_list* second);

/**
 * Merges two sorted lists.
 * Both lists must be sorted using the same comparison function.
 * The merge is stable: equal elements from the first list precede those from
 * the second list, and the relative order within each list is preserved.
 * The tommy_node::data and tommy_node::index fields are left unchanged.
 * \param first The destination list, receiving the sorted result.
 * \param second The source list. After this call the list content is undefined,
 * and you should not use it anymore.
 * The lists must be distinct and must not share nodes.
 * \param cmp Compare function called with two elements.
 * The function should return <0 if the first element is less than the second,
 * ==0 if equal, and >0 if greater. It must not modify the lists.
 * \note This operation is O(n+m), or O(1) when one list is empty or the ranges
 * can be concatenated directly. It uses O(1) auxiliary space.
 */
TOMMY_API void tommy_list_merge(tommy_list* first, tommy_list* second, tommy_compare_func* cmp);

/**
 * Reverses the order of the elements in a list.
 * Empty lists and lists containing one element are left unchanged.
 * The tommy_node::data and tommy_node::index fields are left unchanged.
 * \param list The list.
 * \note This operation is O(n) and uses O(1) auxiliary space.
 */
TOMMY_API void tommy_list_reverse(tommy_list* list);

/**
 * Sorts a list.
 * It's a stable merge sort with O(N*log(N)) worst complexity.
 * It's faster on degenerated cases like partially ordered lists.
 * \param cmp Compare function called with two elements.
 * The function should return <0 if the first element is less than the second, ==0 if equal, and >0 if greater.
 */
TOMMY_API void tommy_list_sort(tommy_list* list, tommy_compare_func* cmp);

/**
 * Checks if empty.
 * \return If the list is empty.
 */
tommy_inline tommy_bool_t tommy_list_empty(tommy_list* list)
{
	return tommy_list_head(list) == 0;
}

/**
 * Gets the number of elements.
 * \note This operation is O(n).
 */
tommy_inline tommy_size_t tommy_list_count(tommy_list* list)
{
	tommy_size_t count = 0;
	tommy_node* i = tommy_list_head(list);

	while (i) {
		++count;
		i = i->next;
	}

	return count;
}

/**
 * Calls the specified function for each element in the list.
 *
 * You cannot add or remove elements from the inside of the callback,
 * but can use it to deallocate them.
 *
 * \code
 * tommy_list list;
 *
 * // initializes the list
 * tommy_list_init(&list);
 *
 * ...
 *
 * // creates an object
 * struct object* obj = malloc(sizeof(struct object));
 *
 * ...
 *
 * // insert it in the list
 * tommy_list_insert_tail(&list, &obj->node, obj);
 *
 * ...
 *
 * // deallocates all the objects iterating the list
 * tommy_list_foreach(&list, free);
 * \endcode
 */
tommy_inline void tommy_list_foreach(tommy_list* list, tommy_foreach_func* func)
{
	tommy_node* node = tommy_list_head(list);

	while (node) {
		void* data = node->data;
		node = node->next;
		func(data);
	}
}

/**
 * Calls the specified function with an argument for each element in the list.
 * The iteration order and callback rules are the same as tommy_list_foreach().
 * The callback may deallocate the current element.
 * Adding or removing elements from inside the callback is not allowed.
 */
tommy_inline void tommy_list_foreach_arg(tommy_list* list, tommy_foreach_arg_func* func, void* arg)
{
	tommy_node* node = tommy_list_head(list);

	while (node) {
		void* data = node->data;
		node = node->next;
		func(arg, data);
	}
}

#endif

