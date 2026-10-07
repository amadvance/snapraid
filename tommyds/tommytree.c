// SPDX-License-Identifier: BSD-2-Clause
// Copyright (C) 2015 Andrea Mazzoleni

#include "tommytree.h"

/******************************************************************************/

/**
 * The parent pointer and the AVL balance factor share the index field.
 * tommy_tree_node is pointer aligned, leaving its two least significant bits
 * available for the balance factor.
 */
#define TOMMY_TREE_BALANCE_MASK ((tommy_size_t)3)
#define TOMMY_TREE_BALANCE_LEFT ((tommy_size_t)1)
#define TOMMY_TREE_BALANCE_RIGHT ((tommy_size_t)2)

tommy_inline int tommy_tree_balance_get(tommy_tree_node* node)
{
	tommy_size_t balance = node->index & TOMMY_TREE_BALANCE_MASK;

	if (balance == TOMMY_TREE_BALANCE_LEFT)
		return -1;
	if (balance == TOMMY_TREE_BALANCE_RIGHT)
		return 1;
	return 0;
}

tommy_inline void tommy_tree_balance_set(tommy_tree_node* node, int balance)
{
	tommy_size_t value = 0;

	if (balance < 0)
		value = TOMMY_TREE_BALANCE_LEFT;
	else if (balance > 0)
		value = TOMMY_TREE_BALANCE_RIGHT;

	node->index = (node->index & ~TOMMY_TREE_BALANCE_MASK) | value;
}

tommy_inline void tommy_tree_parent_set(tommy_tree_node* node, tommy_tree_node* parent)
{
	node->index = (tommy_size_t)(tommy_uintptr_t)parent | (node->index & TOMMY_TREE_BALANCE_MASK);
}

tommy_inline void tommy_tree_replace(tommy_tree* tree, tommy_tree_node* root, tommy_tree_node* node)
{
	tommy_tree_node* parent = tommy_tree_parent(root);

	if (!parent)
		tree->root = node;
	else if (parent->prev == root)
		parent->prev = node;
	else {
		parent->next = node;
	}

	if (node)
		tommy_tree_parent_set(node, parent);
}

tommy_inline tommy_tree_node* tommy_tree_rotate_left(tommy_tree* tree, tommy_tree_node* root)
{
	tommy_tree_node* next = root->next;

	tommy_tree_replace(tree, root, next);
	root->next = next->prev;
	if (root->next)
		tommy_tree_parent_set(root->next, root);

	next->prev = root;
	tommy_tree_parent_set(root, next);

	return next;
}

tommy_inline tommy_tree_node* tommy_tree_rotate_right(tommy_tree* tree, tommy_tree_node* root)
{
	tommy_tree_node* prev = root->prev;

	tommy_tree_replace(tree, root, prev);
	root->prev = prev->next;
	if (root->prev)
		tommy_tree_parent_set(root->prev, root);

	prev->next = root;
	tommy_tree_parent_set(root, prev);

	return prev;
}

tommy_inline void tommy_tree_insert_balance(tommy_tree* tree, tommy_tree_node* node)
{
	tommy_tree_node* parent = tommy_tree_parent(node);

	while (parent) {
		int balance = tommy_tree_balance_get(parent);

		if (node == parent->prev)
			--balance;
		else {
			++balance;
		}

		if (balance == 0) {
			tommy_tree_balance_set(parent, balance);
			return;
		}

		if (balance >= -1 && balance <= 1) {
			tommy_tree_balance_set(parent, balance);
			node = parent;
			parent = tommy_tree_parent(parent);
			continue;
		}

		if (balance == -2) {
			tommy_tree_node* left = parent->prev;
			int left_balance = tommy_tree_balance_get(left);

			if (left_balance < 0) {
				tommy_tree_rotate_right(tree, parent);
				tommy_tree_balance_set(parent, 0);
				tommy_tree_balance_set(left, 0);
			} else {
				tommy_tree_node* middle = left->next;
				int middle_balance = tommy_tree_balance_get(middle);

				tommy_tree_rotate_left(tree, left);
				tommy_tree_rotate_right(tree, parent);
				tommy_tree_balance_set(parent, middle_balance < 0 ? 1 : 0);
				tommy_tree_balance_set(left, middle_balance > 0 ? -1 : 0);
				tommy_tree_balance_set(middle, 0);
			}
		} else {
			tommy_tree_node* right = parent->next;
			int right_balance = tommy_tree_balance_get(right);

			if (right_balance > 0) {
				tommy_tree_rotate_left(tree, parent);
				tommy_tree_balance_set(parent, 0);
				tommy_tree_balance_set(right, 0);
			} else {
				tommy_tree_node* middle = right->prev;
				int middle_balance = tommy_tree_balance_get(middle);

				tommy_tree_rotate_right(tree, right);
				tommy_tree_rotate_left(tree, parent);
				tommy_tree_balance_set(parent, middle_balance > 0 ? -1 : 0);
				tommy_tree_balance_set(right, middle_balance < 0 ? 1 : 0);
				tommy_tree_balance_set(middle, 0);
			}
		}

		return;
	}
}

tommy_inline void* tommy_tree_insert_impl(tommy_tree* tree, tommy_tree_node* node, void* data, tommy_bool_t unique)
{
	tommy_tree_node* parent = 0;
	tommy_tree_node* existing = 0;
	tommy_tree_node** link = &tree->root;

	while (*link) {
		int cmp = tree->cmp(data, (*link)->data);

		parent = *link;
		if (cmp < 0)
			link = &parent->prev;
		else if (cmp == 0 && unique) {
			existing = parent;
			link = &parent->prev;
		} else {
			/* placing equal keys after existing ones preserves insertion order through rotations */
			link = &parent->next;
		}
	}

	if (existing)
		return existing->data;

	node->data = data;
	node->prev = 0;
	node->next = 0;
	node->index = (tommy_size_t)(tommy_uintptr_t)parent;
	*link = node;
	++tree->count;

	tommy_tree_insert_balance(tree, node);

	return node->data;
}

TOMMY_API void tommy_tree_insert(tommy_tree* tree, tommy_tree_node* node, void* data)
{
	tommy_tree_insert_impl(tree, node, data, 0);
}

TOMMY_API void* tommy_tree_insert_unique(tommy_tree* tree, tommy_tree_node* node, void* data)
{
	return tommy_tree_insert_impl(tree, node, data, 1);
}

tommy_inline void tommy_tree_remove_balance(tommy_tree* tree, tommy_tree_node* node, tommy_bool_t left_shrunk)
{
	while (node) {
		int balance = tommy_tree_balance_get(node);

		if (left_shrunk)
			++balance;
		else
			--balance;

		if (balance == -1 || balance == 1) {
			tommy_tree_balance_set(node, balance);
			return;
		}

		if (balance == 0) {
			tommy_tree_balance_set(node, balance);
			tommy_tree_node* parent = tommy_tree_parent(node);
			if (!parent)
				return;
			left_shrunk = node == parent->prev;
			node = parent;
			continue;
		}

		if (balance == -2) {
			tommy_tree_node* left = node->prev;
			int left_balance = tommy_tree_balance_get(left);
			tommy_tree_node* root;

			if (left_balance <= 0) {
				root = tommy_tree_rotate_right(tree, node);
				if (left_balance == 0) {
					tommy_tree_balance_set(node, -1);
					tommy_tree_balance_set(left, 1);
					return;
				}
				tommy_tree_balance_set(node, 0);
				tommy_tree_balance_set(left, 0);
			} else {
				tommy_tree_node* middle = left->next;
				int middle_balance = tommy_tree_balance_get(middle);

				tommy_tree_rotate_left(tree, left);
				root = tommy_tree_rotate_right(tree, node);
				tommy_tree_balance_set(node, middle_balance < 0 ? 1 : 0);
				tommy_tree_balance_set(left, middle_balance > 0 ? -1 : 0);
				tommy_tree_balance_set(middle, 0);
			}

			node = tommy_tree_parent(root);
			if (!node)
				return;
			left_shrunk = root == node->prev;
			continue;
		}

		{
			tommy_tree_node* right = node->next;
			int right_balance = tommy_tree_balance_get(right);
			tommy_tree_node* root;

			if (right_balance >= 0) {
				root = tommy_tree_rotate_left(tree, node);
				if (right_balance == 0) {
					tommy_tree_balance_set(node, 1);
					tommy_tree_balance_set(right, -1);
					return;
				}
				tommy_tree_balance_set(node, 0);
				tommy_tree_balance_set(right, 0);
			} else {
				tommy_tree_node* middle = right->prev;
				int middle_balance = tommy_tree_balance_get(middle);

				tommy_tree_rotate_right(tree, right);
				root = tommy_tree_rotate_left(tree, node);
				tommy_tree_balance_set(node, middle_balance > 0 ? -1 : 0);
				tommy_tree_balance_set(right, middle_balance < 0 ? 1 : 0);
				tommy_tree_balance_set(middle, 0);
			}

			node = tommy_tree_parent(root);
			if (!node)
				return;
			left_shrunk = root == node->prev;
		}
	}
}

TOMMY_API void* tommy_tree_remove_existing(tommy_tree* tree, tommy_tree_node* node)
{
	tommy_tree_node* parent;
	tommy_bool_t left_shrunk;
	void* data = node->data;

	if (!node->prev || !node->next) {
		tommy_tree_node* child = node->prev ? node->prev : node->next;

		parent = tommy_tree_parent(node);
		left_shrunk = parent && node == parent->prev;
		tommy_tree_replace(tree, node, child);
	} else {
		tommy_tree_node* next = node->next;

		while (next->prev)
			next = next->prev;

		parent = tommy_tree_parent(next);
		if (parent == node) {
			tommy_tree_replace(tree, node, next);
			next->prev = node->prev;
			tommy_tree_parent_set(next->prev, next);
			tommy_tree_balance_set(next, tommy_tree_balance_get(node));
			parent = next;
			left_shrunk = 0;
		} else {
			parent->prev = next->next;
			if (parent->prev)
				tommy_tree_parent_set(parent->prev, parent);

			tommy_tree_replace(tree, node, next);
			next->prev = node->prev;
			next->next = node->next;
			tommy_tree_parent_set(next->prev, next);
			tommy_tree_parent_set(next->next, next);
			tommy_tree_balance_set(next, tommy_tree_balance_get(node));
			left_shrunk = 1;
		}
	}

	--tree->count;
	if (parent)
		tommy_tree_remove_balance(tree, parent, left_shrunk);

	return data;
}

tommy_inline void tommy_tree_to_list_node(tommy_tree_node* node, tommy_list* list)
{
	if (node) {
		tommy_tree_node* right = node->next;

		tommy_tree_to_list_node(node->prev, list);
		/* list insertion overwrites child links, so keep the right subtree reachable */
		tommy_list_insert_tail(list, node, node->data);
		tommy_tree_to_list_node(right, list);
	}
}

TOMMY_API void tommy_tree_to_list(tommy_tree* tree, tommy_list* list)
{
	tommy_tree_to_list_node(tree->root, list);
	tommy_tree_clear(tree);
}

TOMMY_API void tommy_tree_foreach(tommy_tree* tree, tommy_foreach_func* func)
{
	/* AVL height is less than twice the bit width of its node count */
	tommy_tree_node* stack[2 * TOMMY_SIZE_BIT];
	tommy_size_t depth = 0;
	tommy_tree_node* node = tree->root;

	while (node || depth) {
		while (node) {
			stack[depth] = node;
			++depth;
			node = node->prev;
		}

		node = stack[--depth];
		/* save the next subtree before the callback can free this node */
		tommy_tree_node* next = node->next;
		func(node->data);
		node = next;
	}
}

TOMMY_API void tommy_tree_foreach_arg(tommy_tree* tree, tommy_foreach_arg_func* func, void* arg)
{
	/* AVL height is less than twice the bit width of its node count */
	tommy_tree_node* stack[2 * TOMMY_SIZE_BIT];
	tommy_size_t depth = 0;
	tommy_tree_node* node = tree->root;

	while (node || depth) {
		while (node) {
			stack[depth] = node;
			++depth;
			node = node->prev;
		}

		node = stack[--depth];
		/* save the next subtree before the callback can free this node */
		tommy_tree_node* next = node->next;
		func(arg, node->data);
		node = next;
	}
}

TOMMY_API tommy_size_t tommy_tree_memory_usage(tommy_tree* tree)
{
	return tommy_tree_count(tree) * sizeof(tommy_tree_node);
}

