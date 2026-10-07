// SPDX-License-Identifier: BSD-2-Clause
// Copyright (C) 2015 Andrea Mazzoleni

/** \file
 * AVL tree.
 *
 * This tree is a standard AVL tree implementation that stores elements in the
 * order defined by the comparison function.
 *
 * Duplicate elements are allowed. They are visited in insertion order.
 * Use tommy_tree_insert_unique() to reject duplicate keys.
 *
 * To initialize a tree you have to call tommy_tree_init() specifying a comparison
 * function that will define the order in the tree.
 *
 * \code
 * tommy_tree tree;
 *
 * tommy_tree_init(&tree, cmp);
 * \endcode
 *
 * To insert elements in the tree you have to call tommy_tree_insert() for
 * each element.
 * In the insertion call you have to specify the address of the node and the
 * address of the element.
 * The address of the element is used to initialize the tommy_node::data field
 * of the node.
 *
 * \code
 * struct element {
 *     int value;
 *     // other fields
 *     tommy_tree_node node;
 * };
 *
 * struct element* elem = malloc(sizeof(struct element)); // creates the element
 *
 * elem->value = ...; // initializes the element
 *
 * tommy_tree_insert(&tree, &elem->node, elem); // inserts the element
 * \endcode
 *
 * To find the first element with a given key in tree order, call
 * tommy_tree_search() providing the key to search.
 *
 * \code
 * struct element value_to_find = { 1 };
 * struct element* elem = tommy_tree_search(&tree, &value_to_find);
 * if (!elem) {
 *     // not found
 * } else {
 *     // found
 * }
 * \endcode
 *
 * To iterate over all the elements in the tree you can call
 * tommy_tree_head() to get the head of the tree and follow the
 * next elements with tommy_tree_next().
 *
 * \code
 * tommy_tree_node* i = tommy_tree_head(&tree);
 * while (i) {
 *     struct element* elem = i->data; // gets the element pointer
 *
 *     printf("%d\n", elem->value); // process the element
 *
 *     i = tommy_tree_next(i); // go to the next element
 * }
 * \endcode
 *
 * \anchor tommy_tree_iterator_validity
 * Iterators are pointers to nodes contained in the tree.
 * Inserting elements or removing other nodes between iterator calls does not
 * invalidate pointers to nodes that remain in the tree.
 * Subsequent next/prev calls use the current contents and ordering;
 * inserted elements may become visible and removed elements are skipped.
 *
 * Removing the pointed node invalidates its use as an iterator, even if
 * its storage has not been freed. To continue traversal after removing
 * the current node, obtain the next or previous node before removal.
 *
 * Keys and fields used for ordering must not be modified while contained.
 * Concurrent traversal and modification require external synchronization.
 *
 * To remove the first element with a given key in tree order, call
 * tommy_tree_remove() providing the key to search and remove.
 *
 * \code
 * struct element value_to_remove = { 1 };
 * struct element* elem = tommy_tree_remove(&tree, &value_to_remove);
 * if (elem) {
 *     free(elem); // frees the element allocated memory
 * }
 * \endcode
 *
 * You can also remove an element from the tree if you already have the node
 * address using tommy_tree_remove_existing().
 *
 * \code
 * struct element* elem = ...;
 * tommy_tree_remove_existing(&tree, &elem->node);
 * free(elem);
 * \endcode
 *
 * To remove the smallest or greatest element you can use tommy_tree_remove_head()
 * or tommy_tree_remove_tail().
 *
 * \code
 * struct element* elem = tommy_tree_remove_head(&tree);
 * if (elem) {
 *     free(elem);
 * }
 * \endcode
 *
 * To destroy the tree you have to remove or destroy all the contained elements.
 * The tree itself doesn't have or need a deallocation function.
 * This can be done with the tommy_tree_foreach() function.
 *
 * \code
 * // deallocates all the elements iterating the tree
 * tommy_tree_foreach(&tree, free);
 * \endcode
 */

#ifndef TOMMY_TREE_H
#define TOMMY_TREE_H

#include "tommytypes.h"
#include "tommylist.h"

/******************************************************************************/
/* tree */

/**
 * Tree node.
 * This is the node that you have to include inside your elements.
 * The node must preserve its natural alignment because the tree stores the
 * balance factor in the two least significant bits of the parent pointer.
 */
typedef tommy_node tommy_tree_node;

/**
 * Tree container type.
 * \note Don't use internal fields directly, but access the container only using functions.
 */
typedef struct tommy_tree_struct {
	tommy_tree_node* root; /**< Root node. */
	tommy_compare_func* cmp; /**< Comparison function. */
	tommy_size_t count; /**< Number of elements. */
} tommy_tree;

/**
 * Initializes the tree.
 * \param tree The tree.
 * \param cmp The comparison function that defines the order in the tree.
 */
tommy_inline void tommy_tree_init(tommy_tree* tree, tommy_compare_func* cmp)
{
	tree->root = 0;
	tree->count = 0;
	tree->cmp = cmp;
}

/**
 * Exchanges the contents of two initialized trees, including their comparison functions.
 * Each comparison function moves with its root to preserve the tree order.
 * The trees must not share nodes. Nodes are not accessed or modified.
 * Passing the same tree twice has no effect.
 * \param first The first tree.
 * \param second The second tree.
 * \note This operation is O(1).
 */
tommy_inline void tommy_tree_swap(tommy_tree* first, tommy_tree* second)
{
	tommy_tree tmp = *first;
	*first = *second;
	*second = tmp;
}

/**
 * Removes all elements, preserving the comparison function.
 * The tree remains initialized and can be reused immediately.
 * Elements are not freed and nodes are not accessed or modified.
 * Their links must not be used to traverse the previous contents.
 * You can call this function after tommy_tree_foreach() has freed the elements.
 * \param tree The tree.
 * \note This operation is O(1).
 */
tommy_inline void tommy_tree_clear(tommy_tree* tree)
{
	tree->root = 0;
	tree->count = 0;
}

/**
 * Inserts an element in the tree, allowing duplicate keys.
 * Equal elements are visited in insertion order.
 * You have to provide the pointer of the node embedded into the element and
 * the pointer to the element.
 * \param tree The tree.
 * \param node Pointer to the node embedded into the element to insert.
 * \param data Pointer to the element to insert.
 */
TOMMY_API void tommy_tree_insert(tommy_tree* tree, tommy_tree_node* node, void* data);

/**
 * Inserts an element only if no equal element is already contained.
 * If found, the first equal element in tree order is returned and the
 * candidate node is left unchanged. Otherwise the candidate is inserted
 * and data is returned.
 * \param tree The tree.
 * \param node The candidate node. It must not belong to any container.
 * \param data Pointer to the element to insert.
 * \return The first equal element, or data if the candidate was inserted.
 */
TOMMY_API void* tommy_tree_insert_unique(tommy_tree* tree, tommy_tree_node* node, void* data);

/**
 * Transfers all elements from the tree to the tail of a list in tree order.
 * Existing list elements remain before the transferred elements. The tree is
 * left empty and initialized with its original comparison function.
 * The list must be initialized and must not share nodes with the tree.
 * Elements are not freed, and the node data and index fields are left unchanged.
 * The index field no longer represents a valid tree parent after the transfer.
 * \param tree The tree to drain.
 * \param list The destination list.
 * \note This operation is O(n), using O(log n) stack space.
 */
TOMMY_API void tommy_tree_to_list(tommy_tree* tree, tommy_list* list);

/**
 * Removes an element from the tree.
 * You must already have the address of the node to remove.
 * \param tree The tree.
 * \param node The node to remove.
 * \return The removed element.
 */
TOMMY_API void* tommy_tree_remove_existing(tommy_tree* tree, tommy_tree_node* node);

/**
 * Gets the parent of the specified node.
 * \param node Node contained in the tree.
 * \return The parent node. For the root node 0 is returned.
 */
tommy_inline tommy_tree_node* tommy_tree_parent(tommy_tree_node* node)
{
	return (tommy_tree_node*)(tommy_uintptr_t)(node->index & ~(tommy_size_t)3);
}

/**
 * Gets the node following the specified one in tree order.
 * See \ref tommy_tree_iterator_validity for iterator validity across insertions and removals.
 * \param node Node contained in the tree.
 * \return The following node, or 0 for the tail node.
 * \note This operation is O(log n), and amortized O(1) during full traversal.
 */
tommy_inline tommy_tree_node* tommy_tree_next(tommy_tree_node* node)
{
	if (node->next) {
		node = node->next;
		while (node->prev)
			node = node->prev;
		return node;
	}

	tommy_tree_node* parent = tommy_tree_parent(node);
	while (parent && node == parent->next) {
		node = parent;
		parent = tommy_tree_parent(parent);
	}

	return parent;
}

/**
 * Gets the node preceding the specified one in tree order.
 * See \ref tommy_tree_iterator_validity for iterator validity across insertions and removals.
 * \param node Node contained in the tree.
 * \return The preceding node, or 0 for the head node.
 * \note This operation is O(log n), and amortized O(1) during full traversal.
 */
tommy_inline tommy_tree_node* tommy_tree_prev(tommy_tree_node* node)
{
	if (node->prev) {
		node = node->prev;
		while (node->next)
			node = node->next;
		return node;
	}

	tommy_tree_node* parent = tommy_tree_parent(node);
	while (parent && node == parent->prev) {
		node = parent;
		parent = tommy_tree_parent(parent);
	}

	return parent;
}

/**
 * Gets the head (smallest) node in the tree.
 * \param tree The tree.
 * \return The head node, or 0 if the tree is empty.
 * \note This operation is O(log n).
 */
tommy_inline tommy_tree_node* tommy_tree_head(tommy_tree* tree)
{
	tommy_tree_node* node = tree->root;

	if (!node)
		return 0;

	while (node->prev)
		node = node->prev;

	return node;
}

/**
 * Gets the tail (greatest) node in the tree.
 * \param tree The tree.
 * \return The tail node, or 0 if the tree is empty.
 * \note This operation is O(log n).
 */
tommy_inline tommy_tree_node* tommy_tree_tail(tommy_tree* tree)
{
	tommy_tree_node* node = tree->root;

	if (!node)
		return 0;

	while (node->next)
		node = node->next;

	return node;
}

/**
 * Removes and returns the head (smallest) element.
 * If the tree is empty, 0 is returned.
 * \param tree The tree.
 * \return The removed element, or 0 if the tree is empty.
 */
tommy_inline void* tommy_tree_remove_head(tommy_tree* tree)
{
	tommy_tree_node* node = tommy_tree_head(tree);

	if (!node)
		return 0;

	return tommy_tree_remove_existing(tree, node);
}

/**
 * Removes and returns the tail (greatest) element.
 * If the tree is empty, 0 is returned.
 * \param tree The tree.
 * \return The removed element, or 0 if the tree is empty.
 */
tommy_inline void* tommy_tree_remove_tail(tommy_tree* tree)
{
	tommy_tree_node* node = tommy_tree_tail(tree);

	if (!node)
		return 0;

	return tommy_tree_remove_existing(tree, node);
}

/**
 * Finds the first equal node in the tree with a specific comparison function.
 *
 * Like tommy_tree_find() but you can specify a different comparison function.
 * Note that this function must define a suborder of the original one.
 * The first equal node in tree order is returned.
 *
 * The cmp_arg argument will be the first argument of the comparison function,
 * and it can be of a different type than the elements in the tree.
 * \param tree The tree.
 * \param cmp Comparison function called with cmp_arg and the element in the tree.
 * \param cmp_arg Search key, which may have a different type from tree elements.
 * \return The first equal node in tree order, or 0 if none.
 */
tommy_inline tommy_tree_node* tommy_tree_find_compare(tommy_tree* tree, tommy_compare_func* cmp, const void* cmp_arg)
{
	tommy_tree_node* node = tree->root;
	tommy_tree_node* candidate = 0;

	while (node) {
		/* comparators must not change the tree, so retain the data across the call */
		void* data = node->data;
		int c = cmp(cmp_arg, data);

		if (c < 0)
			node = node->prev;
		else if (c > 0)
			node = node->next;
		else {
			candidate = node;
			node = node->prev;
		}
	}

	return candidate;
}

/**
 * Finds the first equal node in the tree.
 * If the element is not found, 0 is returned.
 * \param tree The tree.
 * \param data Element used for comparison.
 * \return The first equal node in tree order, or 0 if none.
 */
tommy_inline tommy_tree_node* tommy_tree_find(tommy_tree* tree, const void* data)
{
	return tommy_tree_find_compare(tree, tree->cmp, data);
}

/**
 * Searches an element in the tree with a specific comparison function.
 *
 * Like tommy_tree_search() but you can specify a different comparison function.
 * Note that this function must define a suborder of the original one.
 * The first equal element in tree order is returned.
 *
 * The cmp_arg argument will be the first argument of the comparison function,
 * and it can be of a different type than the elements in the tree.
 * \param tree The tree.
 * \param cmp Comparison function called with cmp_arg and the element in the tree.
 * \param cmp_arg Search key, which may have a different type from tree elements.
 * \return The first equal element in tree order, or 0 if none.
 */
tommy_inline void* tommy_tree_search_compare(tommy_tree* tree, tommy_compare_func* cmp, const void* cmp_arg)
{
	tommy_tree_node* node = tommy_tree_find_compare(tree, cmp, cmp_arg);

	return node ? node->data : 0;
}

/**
 * Searches an element in the tree.
 * If the element is not found, 0 is returned.
 * \param tree The tree.
 * \param data Element used for comparison.
 * \return The first equal element in tree order, or 0 if none.
 */
tommy_inline void* tommy_tree_search(tommy_tree* tree, const void* data)
{
	tommy_tree_node* node = tommy_tree_find(tree, data);

	return node ? node->data : 0;
}

/**
 * Finds a node in the tree with key greater or equal than the specified one with a specific comparison function.
 *
 * Like tommy_tree_find_greater_equal() but you can specify a different comparison function.
 * Note that this function must define a suborder of the original one.
 * The first qualifying node in tree order is returned.
 *
 * The cmp_arg argument will be the first argument of the comparison function,
 * and it can be of a different type than the elements in the tree.
 * \param tree The tree.
 * \param cmp Comparison function called with cmp_arg and the element in the tree.
 * \param cmp_arg Search key, which may have a different type from tree elements.
 * \return The first node in tree order with key greater or equal, or 0 if none.
 */
tommy_inline tommy_tree_node* tommy_tree_find_greater_equal_compare(tommy_tree* tree, tommy_compare_func* cmp, const void* cmp_arg)
{
	tommy_tree_node* node = tree->root;
	tommy_tree_node* candidate = 0;

	while (node) {
		/* comparators must not change the tree, so retain the data across the call */
		void* data = node->data;
		int c = cmp(cmp_arg, data);

		if (c <= 0) {
			candidate = node;
			node = node->prev;
		} else {
			node = node->next;
		}
	}

	return candidate;
}

/**
 * Finds a node in the tree with key greater or equal than the specified one.
 * If no such element exists, 0 is returned.
 * \param tree The tree.
 * \param data Element used for comparison.
 * \return The first node in tree order with key greater or equal, or 0 if none.
 */
tommy_inline tommy_tree_node* tommy_tree_find_greater_equal(tommy_tree* tree, const void* data)
{
	return tommy_tree_find_greater_equal_compare(tree, tree->cmp, data);
}

/**
 * Searches an element in the tree with key greater or equal than the specified one with a specific comparison function.
 *
 * Like tommy_tree_search_greater_equal() but you can specify a different comparison function.
 * Note that this function must define a suborder of the original one.
 * The first qualifying element in tree order is returned.
 *
 * The cmp_arg argument will be the first argument of the comparison function,
 * and it can be of a different type than the elements in the tree.
 * \param tree The tree.
 * \param cmp Comparison function called with cmp_arg and the element in the tree.
 * \param cmp_arg Search key, which may have a different type from tree elements.
 * \return The first element in tree order with key greater or equal, or 0 if none.
 */
tommy_inline void* tommy_tree_search_greater_equal_compare(tommy_tree* tree, tommy_compare_func* cmp, const void* cmp_arg)
{
	tommy_tree_node* node = tommy_tree_find_greater_equal_compare(tree, cmp, cmp_arg);

	return node ? node->data : 0;
}

/**
 * Searches an element in the tree with key greater or equal than the specified one.
 * If no such element exists, 0 is returned.
 * \param tree The tree.
 * \param data Element used for comparison.
 * \return The first element in tree order with key greater or equal, or 0 if none.
 */
tommy_inline void* tommy_tree_search_greater_equal(tommy_tree* tree, const void* data)
{
	tommy_tree_node* node = tommy_tree_find_greater_equal(tree, data);

	return node ? node->data : 0;
}

/**
 * Finds the first node in tree order with key strictly greater than the specified one using a specific comparison function.
 * The function must define a suborder of the tree comparison function.
 * Equivalent elements are excluded from the result.
 * \param tree The tree.
 * \param cmp Comparison function called with cmp_arg and the element in the tree.
 * \param cmp_arg Search key, which may have a different type from tree elements.
 * \return The first strictly greater node, or 0 if none.
 */
tommy_inline tommy_tree_node* tommy_tree_find_greater_compare(tommy_tree* tree, tommy_compare_func* cmp, const void* cmp_arg)
{
	tommy_tree_node* node = tree->root;
	tommy_tree_node* candidate = 0;

	while (node) {
		/* comparators must not change the tree, so retain the data across the call */
		void* data = node->data;
		int c = cmp(cmp_arg, data);

		if (c < 0) {
			candidate = node;
			node = node->prev;
		} else {
			node = node->next;
		}
	}

	return candidate;
}

/**
 * Finds the first node in tree order with key strictly greater than the specified one.
 * If no such element exists, 0 is returned.
 * \param tree The tree.
 * \param data Element used for comparison.
 * \return The first strictly greater node, or 0 if none.
 */
tommy_inline tommy_tree_node* tommy_tree_find_greater(tommy_tree* tree, const void* data)
{
	return tommy_tree_find_greater_compare(tree, tree->cmp, data);
}

/**
 * Searches the first element in tree order with key strictly greater than the specified one using a specific comparison function.
 * The function must define a suborder of the tree comparison function.
 * Equivalent elements are excluded from the result.
 * \param tree The tree.
 * \param cmp Comparison function called with cmp_arg and the element in the tree.
 * \param cmp_arg Search key, which may have a different type from tree elements.
 * \return The first strictly greater element, or 0 if none.
 */
tommy_inline void* tommy_tree_search_greater_compare(tommy_tree* tree, tommy_compare_func* cmp, const void* cmp_arg)
{
	tommy_tree_node* node = tommy_tree_find_greater_compare(tree, cmp, cmp_arg);

	return node ? node->data : 0;
}

/**
 * Searches the first element in tree order with key strictly greater than the specified one.
 * If no such element exists, 0 is returned.
 * \param tree The tree.
 * \param data Element used for comparison.
 * \return The first strictly greater element, or 0 if none.
 */
tommy_inline void* tommy_tree_search_greater(tommy_tree* tree, const void* data)
{
	tommy_tree_node* node = tommy_tree_find_greater(tree, data);

	return node ? node->data : 0;
}

/**
 * Finds a node in the tree with key less or equal than the specified one with a specific comparison function.
 *
 * Like tommy_tree_find_less_equal() but you can specify a different comparison function.
 * Note that this function must define a suborder of the original one.
 * The last qualifying node in tree order is returned.
 *
 * The cmp_arg argument will be the first argument of the comparison function,
 * and it can be of a different type than the elements in the tree.
 * \param tree The tree.
 * \param cmp Comparison function called with cmp_arg and the element in the tree.
 * \param cmp_arg Search key, which may have a different type from tree elements.
 * \return The last node in tree order with key less or equal, or 0 if none.
 */
tommy_inline tommy_tree_node* tommy_tree_find_less_equal_compare(tommy_tree* tree, tommy_compare_func* cmp, const void* cmp_arg)
{
	tommy_tree_node* node = tree->root;
	tommy_tree_node* candidate = 0;

	while (node) {
		/* comparators must not change the tree, so retain the data across the call */
		void* data = node->data;
		int c = cmp(cmp_arg, data);

		if (c < 0) {
			node = node->prev;
		} else {
			candidate = node;
			node = node->next;
		}
	}

	return candidate;
}

/**
 * Finds a node in the tree with key less or equal than the specified one.
 * If no such element exists, 0 is returned.
 * \param tree The tree.
 * \param data Element used for comparison.
 * \return The last node in tree order with key less or equal, or 0 if none.
 */
tommy_inline tommy_tree_node* tommy_tree_find_less_equal(tommy_tree* tree, const void* data)
{
	return tommy_tree_find_less_equal_compare(tree, tree->cmp, data);
}

/**
 * Searches an element in the tree with key less or equal than the specified one with a specific comparison function.
 *
 * Like tommy_tree_search_less_equal() but you can specify a different comparison function.
 * Note that this function must define a suborder of the original one.
 * The last qualifying element in tree order is returned.
 *
 * The cmp_arg argument will be the first argument of the comparison function,
 * and it can be of a different type than the elements in the tree.
 * \param tree The tree.
 * \param cmp Comparison function called with cmp_arg and the element in the tree.
 * \param cmp_arg Search key, which may have a different type from tree elements.
 * \return The last element in tree order with key less or equal, or 0 if none.
 */
tommy_inline void* tommy_tree_search_less_equal_compare(tommy_tree* tree, tommy_compare_func* cmp, const void* cmp_arg)
{
	tommy_tree_node* node = tommy_tree_find_less_equal_compare(tree, cmp, cmp_arg);

	return node ? node->data : 0;
}

/**
 * Searches an element in the tree with key less or equal than the specified one.
 * If no such element exists, 0 is returned.
 * \param tree The tree.
 * \param data Element used for comparison.
 * \return The last element in tree order with key less or equal, or 0 if none.
 */
tommy_inline void* tommy_tree_search_less_equal(tommy_tree* tree, const void* data)
{
	tommy_tree_node* node = tommy_tree_find_less_equal(tree, data);

	return node ? node->data : 0;
}

/**
 * Finds the last node in tree order with key strictly less than the specified one using a specific comparison function.
 * The function must define a suborder of the tree comparison function.
 * Equivalent elements are excluded from the result.
 * \param tree The tree.
 * \param cmp Comparison function called with cmp_arg and the element in the tree.
 * \param cmp_arg Search key, which may have a different type from tree elements.
 * \return The last strictly less node, or 0 if none.
 */
tommy_inline tommy_tree_node* tommy_tree_find_less_compare(tommy_tree* tree, tommy_compare_func* cmp, const void* cmp_arg)
{
	tommy_tree_node* node = tree->root;
	tommy_tree_node* candidate = 0;

	while (node) {
		/* comparators must not change the tree, so retain the data across the call */
		void* data = node->data;
		int c = cmp(cmp_arg, data);

		if (c > 0) {
			candidate = node;
			node = node->next;
		} else {
			node = node->prev;
		}
	}

	return candidate;
}

/**
 * Finds the last node in tree order with key strictly less than the specified one.
 * If no such element exists, 0 is returned.
 * \param tree The tree.
 * \param data Element used for comparison.
 * \return The last strictly less node, or 0 if none.
 */
tommy_inline tommy_tree_node* tommy_tree_find_less(tommy_tree* tree, const void* data)
{
	return tommy_tree_find_less_compare(tree, tree->cmp, data);
}

/**
 * Searches the last element in tree order with key strictly less than the specified one using a specific comparison function.
 * The function must define a suborder of the tree comparison function.
 * Equivalent elements are excluded from the result.
 * \param tree The tree.
 * \param cmp Comparison function called with cmp_arg and the element in the tree.
 * \param cmp_arg Search key, which may have a different type from tree elements.
 * \return The last strictly less element, or 0 if none.
 */
tommy_inline void* tommy_tree_search_less_compare(tommy_tree* tree, tommy_compare_func* cmp, const void* cmp_arg)
{
	tommy_tree_node* node = tommy_tree_find_less_compare(tree, cmp, cmp_arg);

	return node ? node->data : 0;
}

/**
 * Searches the last element in tree order with key strictly less than the specified one.
 * If no such element exists, 0 is returned.
 * \param tree The tree.
 * \param data Element used for comparison.
 * \return The last strictly less element, or 0 if none.
 */
tommy_inline void* tommy_tree_search_less(tommy_tree* tree, const void* data)
{
	tommy_tree_node* node = tommy_tree_find_less(tree, data);

	return node ? node->data : 0;
}

/**
 * Searches and removes an element from the tree with a specific comparison function.
 * The comparison function must define a suborder of the original one.
 * The cmp_arg argument is passed as the first argument of the comparison
 * function and may have a different type than the elements in the tree.
 * \param tree The tree.
 * \param cmp Comparison function called with cmp_arg and the element in the tree.
 * \param cmp_arg Search key, which may have a different type from tree elements.
 * \return The first equal element in tree order, or 0 if not found.
 */
tommy_inline void* tommy_tree_remove_compare(tommy_tree* tree, tommy_compare_func* cmp, const void* cmp_arg)
{
	tommy_tree_node* candidate = tommy_tree_find_compare(tree, cmp, cmp_arg);

	if (!candidate)
		return 0;

	return tommy_tree_remove_existing(tree, candidate);
}

/**
 * Searches and removes an element from the tree.
 * If the element is not found, 0 is returned.
 * \param tree The tree.
 * \param data Element used for comparison.
 * \return The first equal element in tree order, or 0 if not found.
 */
tommy_inline void* tommy_tree_remove(tommy_tree* tree, const void* data)
{
	return tommy_tree_remove_compare(tree, tree->cmp, data);
}

/**
 * Calls the specified function for each element in the tree.
 *
 * The elements are processed in order.
 *
 * You cannot add or remove elements from the inside of the callback,
 * but can use it to deallocate them.
 *
 * \code
 * tommy_tree tree;
 *
 * // initializes the tree
 * tommy_tree_init(&tree, cmp);
 *
 * ...
 *
 * // creates an element
 * struct element* elem = malloc(sizeof(struct element));
 *
 * ...
 *
 * // insert it in the tree
 * tommy_tree_insert(&tree, &elem->node, elem);
 *
 * ...
 *
 * // deallocates all the elements iterating the tree
 * tommy_tree_foreach(&tree, free);
 * tommy_tree_clear(&tree); // resets the tree before reuse
 * \endcode
 *
 * If the callback frees the elements, the tree retains dangling node
 * pointers. Call tommy_tree_clear() or tommy_tree_init() before reusing it.
 * The tree has no storage to release.
 * \param tree The tree.
 * \param func Function to call with each element.
 */
TOMMY_API void tommy_tree_foreach(tommy_tree* tree, tommy_foreach_func* func);

/**
 * Calls the specified function with an argument for each element in the tree.
 * The iteration order and callback rules are the same as tommy_tree_foreach().
 * The callback may deallocate the current element.
 * Adding or removing elements from inside the callback is not allowed.
 * \param tree The tree.
 * \param func Function to call with each element.
 * \param arg Argument to pass to the function.
 */
TOMMY_API void tommy_tree_foreach_arg(tommy_tree* tree, tommy_foreach_arg_func* func, void* arg);

/**
 * Checks whether the tree is empty.
 * \param tree The tree.
 * \return Nonzero if the tree is empty, 0 otherwise.
 * \note This operation is O(1).
 */
tommy_inline tommy_bool_t tommy_tree_empty(const tommy_tree* tree)
{
	return tree->root == 0;
}

/**
 * Gets the number of elements.
 * \param tree The tree.
 * \return The number of elements in the tree.
 * \note This operation is O(1).
 */
tommy_inline tommy_size_t tommy_tree_count(const tommy_tree* tree)
{
	return tree->count;
}

/**
 * Gets the size of allocated memory.
 * It includes the size of the ::tommy_tree_node of the stored elements.
 * \param tree The tree.
 * \return Total memory allocated by the tree, in bytes.
 */
TOMMY_API tommy_size_t tommy_tree_memory_usage(const tommy_tree* tree);

#endif

