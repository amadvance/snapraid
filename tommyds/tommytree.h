// SPDX-License-Identifier: BSD-2-Clause
// Copyright (C) 2015 Andrea Mazzoleni

/** \file
 * AVL tree.
 *
 * This tree is a standard AVL tree implementation that stores elements in the
 * order defined by the comparison function.
 *
 * As a difference from other tommy containers, duplicate elements cannot be inserted.
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
 * address of the object.
 * The address of the object is used to initialize the tommy_node::data field
 * of the node.
 *
 * \code
 * struct object {
 *     int value;
 *     // other fields
 *     tommy_tree_node node;
 * };
 *
 * struct object* obj = malloc(sizeof(struct object)); // creates the object
 *
 * obj->value = ...; // initializes the object
 *
 * tommy_tree_insert(&tree, &obj->node, obj); // inserts the object
 * \endcode
 *
 * To find an element in the tree you have to call tommy_tree_search() providing
 * the key to search.
 *
 * \code
 * struct object value_to_find = { 1 };
 * struct object* obj = tommy_tree_search(&tree, &value_to_find);
 * if (!obj) {
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
 *     struct object* obj = i->data; // gets the object pointer
 *
 *     printf("%d\n", obj->value); // process the object
 *
 *     i = tommy_tree_next(i); // go to the next element
 * }
 * \endcode
 *
 * To remove an element from the tree you have to call tommy_tree_remove()
 * providing the key to search and remove.
 *
 * \code
 * struct object value_to_remove = { 1 };
 * struct object* obj = tommy_tree_remove(&tree, &value_to_remove);
 * if (obj) {
 *     free(obj); // frees the object allocated memory
 * }
 * \endcode
 *
 * You can also remove an element from the tree if you already have the node
 * address using tommy_tree_remove_existing().
 *
 * \code
 * struct object* obj = ...;
 * tommy_tree_remove_existing(&tree, &obj->node);
 * free(obj);
 * \endcode
 *
 * To remove the smallest or greatest element you can use tommy_tree_remove_head()
 * or tommy_tree_remove_tail().
 *
 * \code
 * struct object* obj = tommy_tree_remove_head(&tree);
 * if (obj) {
 *     free(obj);
 * }
 * \endcode
 *
 * To destroy the tree you have to remove or destroy all the contained elements.
 * The tree itself doesn't have or need a deallocation function.
 * This can be done with the tommy_tree_foreach() function.
 *
 * \code
 * // deallocates all the objects iterating the tree
 * tommy_tree_foreach(&tree, free);
 * \endcode
 */

#ifndef __TOMMYTREE_H
#define __TOMMYTREE_H

#include "tommytypes.h"

/******************************************************************************/
/* tree */

/**
 * Tree node.
 * This is the node that you have to include inside your objects.
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
 * \param cmp The comparison function that defines the order in the tree.
 */
tommy_inline void tommy_tree_init(tommy_tree* tree, tommy_compare_func* cmp)
{
	tree->root = 0;
	tree->count = 0;
	tree->cmp = cmp;
}

/**
 * Inserts an element in the tree.
 * If the element is already present, it's not inserted again.
 * Check the return value to identify if the element was already present or not.
 * You have to provide the pointer of the node embedded into the object and
 * the pointer to the object.
 * \param node Pointer to the node embedded into the object to insert.
 * \param data Pointer to the object to insert.
 * \return The element in the tree. Either the already existing one, or the one just inserted.
 */
TOMMY_API void* tommy_tree_insert(tommy_tree* tree, tommy_tree_node* node, void* data);

/**
 * Removes an element from the tree.
 * You must already have the address of the element to remove.
 * \return The tommy_node::data field of the node removed.
 */
TOMMY_API void* tommy_tree_remove_existing(tommy_tree* tree, tommy_tree_node* node);


/** \internal
 * Calls the specified function for each element in the tree.
 * \param root Root node of the tree.
 * \param func Function to call.
 */
TOMMY_API void tommy_tree_foreach_node(tommy_tree_node* root, tommy_foreach_func* func);

/** \internal
 * Calls the specified function with an argument for each element in the tree.
 * \param root Root node of the tree.
 * \param func Function to call.
 * \param arg Custom argument for the function.
 */
TOMMY_API void tommy_tree_foreach_arg_node(tommy_tree_node* root, tommy_foreach_arg_func* func, void* arg);

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
 * \param node Node contained in the tree.
 * \return The following node. For the tail node 0 is returned.
 */
tommy_inline tommy_tree_node* tommy_tree_next(tommy_tree_node* node)
{
	tommy_tree_node* parent;

	if (node->next) {
		node = node->next;
		while (node->prev)
			node = node->prev;
		return node;
	}

	parent = tommy_tree_parent(node);
	while (parent && node == parent->next) {
		node = parent;
		parent = tommy_tree_parent(parent);
	}

	return parent;
}

/**
 * Gets the node preceding the specified one in tree order.
 * \param node Node contained in the tree.
 * \return The preceding node. For the head node 0 is returned.
 */
tommy_inline tommy_tree_node* tommy_tree_prev(tommy_tree_node* node)
{
	tommy_tree_node* parent;

	if (node->prev) {
		node = node->prev;
		while (node->next)
			node = node->next;
		return node;
	}

	parent = tommy_tree_parent(node);
	while (parent && node == parent->prev) {
		node = parent;
		parent = tommy_tree_parent(parent);
	}

	return parent;
}

/**
 * Gets the head (smallest) element in the tree.
 * \return The head node. For empty trees 0 is returned.
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
 * Gets the tail (greatest) element in the tree.
 * \return The tail node. For empty trees 0 is returned.
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
 * \return The removed element, or 0 if empty.
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
 * \return The removed element, or 0 if empty.
 */
tommy_inline void* tommy_tree_remove_tail(tommy_tree* tree)
{
	tommy_tree_node* node = tommy_tree_tail(tree);

	if (!node)
		return 0;

	return tommy_tree_remove_existing(tree, node);
}

/** \internal
 * Searches a node in the tree.
 * If the element is not found, 0 is returned.
 * \param cmp Comparison function.
 * \param node Node to start the search from.
 * \param data Element used for comparison.
 * \return The first node found, or 0 if none.
 */
tommy_inline tommy_tree_node* tommy_tree_search_node(tommy_compare_func* cmp, tommy_tree_node* node, void* data)
{
	while (node) {
		int c = cmp(data, node->data);

		if (c < 0)
			node = node->prev;
		else if (c > 0)
			node = node->next;
		else
			return node;
	}

	return 0;
}

/**
 * Searches an element in the tree.
 * If the element is not found, 0 is returned.
 * \param data Element used for comparison.
 * \return The first element found, or 0 if none.
 */
tommy_inline void* tommy_tree_search(tommy_tree* tree, void* data)
{
	tommy_tree_node* node = tommy_tree_search_node(tree->cmp, tree->root, data);

	if (!node)
		return 0;

	return node->data;
}

/**
 * Searches an element in the tree with a specific comparison function.
 *
 * Like tommy_tree_search() but you can specify a different comparison function.
 * Note that this function must define a suborder of the original one.
 *
 * The ::data argument will be the first argument of the comparison function,
 * and it can be of a different type than the objects in the tree.
 */
tommy_inline void* tommy_tree_search_compare(tommy_tree* tree, tommy_compare_func* cmp, void* cmp_arg)
{
	tommy_tree_node* node = tommy_tree_search_node(cmp, tree->root, cmp_arg);

	if (!node)
		return 0;

	return node->data;
}

/** \internal
 * Searches a node in the tree with key greater or equal than the specified one.
 * If no such element exists, 0 is returned.
 * \param cmp Comparison function.
 * \param node Node to start the search from.
 * \param data Element used for comparison.
 * \return The first node found, or 0 if none.
 */
tommy_inline tommy_tree_node* tommy_tree_search_greater_equal_node(tommy_compare_func* cmp, tommy_tree_node* node, void* data)
{
	tommy_tree_node* candidate = 0;

	while (node) {
		int c = cmp(data, node->data);

		if (c < 0) {
			candidate = node;
			node = node->prev;
		} else if (c > 0) {
			node = node->next;
		} else {
			return node;
		}
	}

	return candidate;
}

/**
 * Searches an element in the tree with key greater or equal than the specified one.
 * If no such element exists, 0 is returned.
 * \param data Element used for comparison.
 * \return The first element found, or 0 if none.
 */
tommy_inline void* tommy_tree_search_greater_equal(tommy_tree* tree, void* data)
{
	tommy_tree_node* node = tommy_tree_search_greater_equal_node(tree->cmp, tree->root, data);

	if (!node)
		return 0;

	return node->data;
}

/**
 * Searches an element in the tree with key greater or equal than the specified one with a specific comparison function.
 *
 * Like tommy_tree_search_greater_equal() but you can specify a different comparison function.
 * Note that this function must define a suborder of the original one.
 *
 * The ::data argument will be the first argument of the comparison function,
 * and it can be of a different type than the objects in the tree.
 */
tommy_inline void* tommy_tree_search_greater_equal_compare(tommy_tree* tree, tommy_compare_func* cmp, void* cmp_arg)
{
	tommy_tree_node* node = tommy_tree_search_greater_equal_node(cmp, tree->root, cmp_arg);

	if (!node)
		return 0;

	return node->data;
}

/** \internal
 * Searches a node in the tree with key less or equal than the specified one.
 * If no such element exists, 0 is returned.
 * \param cmp Comparison function.
 * \param node Node to start the search from.
 * \param data Element used for comparison.
 * \return The first node found, or 0 if none.
 */
tommy_inline tommy_tree_node* tommy_tree_search_less_equal_node(tommy_compare_func* cmp, tommy_tree_node* node, void* data)
{
	tommy_tree_node* candidate = 0;

	while (node) {
		int c = cmp(data, node->data);

		if (c < 0) {
			node = node->prev;
		} else if (c > 0) {
			candidate = node;
			node = node->next;
		} else {
			return node;
		}
	}

	return candidate;
}

/**
 * Searches an element in the tree with key less or equal than the specified one.
 * If no such element exists, 0 is returned.
 * \param data Element used for comparison.
 * \return The first element found, or 0 if none.
 */
tommy_inline void* tommy_tree_search_less_equal(tommy_tree* tree, void* data)
{
	tommy_tree_node* node = tommy_tree_search_less_equal_node(tree->cmp, tree->root, data);

	if (!node)
		return 0;

	return node->data;
}

/**
 * Searches an element in the tree with key less or equal than the specified one with a specific comparison function.
 *
 * Like tommy_tree_search_less_equal() but you can specify a different comparison function.
 * Note that this function must define a suborder of the original one.
 *
 * The ::data argument will be the first argument of the comparison function,
 * and it can be of a different type than the objects in the tree.
 */
tommy_inline void* tommy_tree_search_less_equal_compare(tommy_tree* tree, tommy_compare_func* cmp, void* cmp_arg)
{
	tommy_tree_node* node = tommy_tree_search_less_equal_node(cmp, tree->root, cmp_arg);

	if (!node)
		return 0;

	return node->data;
}

/**
 * Searches and removes an element.
 * If the element is not found, 0 is returned.
 * \param data Element used for comparison.
 * \return The removed element, or 0 if not found.
 */
tommy_inline void* tommy_tree_remove(tommy_tree* tree, void* data)
{
	tommy_tree_node* node = tommy_tree_search_node(tree->cmp, tree->root, data);

	if (!node)
		return 0;

	return tommy_tree_remove_existing(tree, node);
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
 * // creates an object
 * struct object* obj = malloc(sizeof(struct object));
 *
 * ...
 *
 * // insert it in the tree
 * tommy_tree_insert(&tree, &obj->node, obj);
 *
 * ...
 *
 * // deallocates all the objects iterating the tree
 * tommy_tree_foreach(&tree, free);
 * \endcode
 */
tommy_inline void tommy_tree_foreach(tommy_tree* tree, tommy_foreach_func* func)
{
	tommy_tree_foreach_node(tree->root, func);
}

/**
 * Calls the specified function with an argument for each element in the tree.
 */
tommy_inline void tommy_tree_foreach_arg(tommy_tree* tree, tommy_foreach_arg_func* func, void* arg)
{
	tommy_tree_foreach_arg_node(tree->root, func, arg);
}

/**
 * Checks if empty.
 * \return If the tree is empty.
 */
tommy_inline tommy_bool_t tommy_tree_empty(tommy_tree* tree)
{
	return tree->root == 0;
}

/**
 * Gets the number of elements.
 */
tommy_inline tommy_size_t tommy_tree_count(tommy_tree* tree)
{
	return tree->count;
}

/**
 * Gets the size of allocated memory.
 * It includes the size of the ::tommy_tree_node of the stored elements.
 */
TOMMY_API tommy_size_t tommy_tree_memory_usage(tommy_tree* tree);

#endif

