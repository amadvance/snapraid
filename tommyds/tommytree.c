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

TOMMY_API void* tommy_tree_insert(tommy_tree* tree, tommy_tree_node* node, void* data)
{
	tommy_tree_node* parent = 0;
	tommy_tree_node** link = &tree->root;

	node->data = data;
	node->prev = 0;
	node->next = 0;
	node->index = 0;

	while (*link) {
		int cmp = tree->cmp(data, (*link)->data);

		parent = *link;
		if (cmp < 0)
			link = &parent->prev;
		else if (cmp > 0)
			link = &parent->next;
		else
			return parent->data;
	}

	node->index = (tommy_size_t)(tommy_uintptr_t)parent;
	*link = node;
	++tree->count;

	tommy_tree_insert_balance(tree, node);

	return node->data;
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
			tommy_tree_node* parent;

			tommy_tree_balance_set(node, balance);
			parent = tommy_tree_parent(node);
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

TOMMY_API void tommy_tree_foreach_node(tommy_tree_node* root, tommy_foreach_func* func)
{
	while (root) {
		tommy_tree_node* next;

		tommy_tree_foreach_node(root->prev, func);

		/* make a copy in case func is free() */
		next = root->next;

		func(root->data);

		root = next;
	}
}

TOMMY_API void tommy_tree_foreach_arg_node(tommy_tree_node* root, tommy_foreach_arg_func* func, void* arg)
{
	while (root) {
		tommy_tree_node* next;

		tommy_tree_foreach_arg_node(root->prev, func, arg);

		/* make a copy in case func is free() */
		next = root->next;

		func(arg, root->data);

		root = next;
	}
}

TOMMY_API tommy_size_t tommy_tree_memory_usage(tommy_tree* tree)
{
	return tommy_tree_count(tree) * sizeof(tommy_tree_node);
}

