/*
 * Copyright (c) 2013-2016, Dalian Futures Information Technology Co., Ltd.
 *
 * Xiaoye Meng <mengxiaoye at dce dot com dot cn>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/*
 * adapted from CLRS, Chapter 18
 */

#include "macros.h"
#include "mem.h"
#include "btree.h"

/* FIXME */
struct btree_t {
	/* minimum degree */
	int		t;
	unsigned long	length;
	btree_node_t	root, sentinel;
	int		(*cmp)(const void *x, const void *y);
	void		(*kfree)(const void *key);
	void		(*vfree)(void *value);
};
struct btree_node_t {
	unsigned char	leaf:1;
	/* the number of keys currently stored in the node */
	int		n;
	btree_node_t	parent;
	union {
		/* pointers to the node's children */
		btree_node_t	*children;
		struct {
			btree_node_t	prev, next;
		}		pn;
	}		u;
	struct {
		const void	*key;
		void		*value;
	}		*bindings;
};

#define BSEARCH(x, skey, i, k) \
	{ \
		int lo = 0, hi = x->n - 1; \
		while (lo <= hi) { \
			i = (lo + hi) >> 1; \
			if ((k = btree->cmp(skey, x->bindings[i].key)) == 0) \
				break; \
			k > 0 ? (lo = i + 1) : (hi = i - 1); \
		} \
	}

/* FIXME */
static btree_node_t node_new(int t) {
	btree_node_t node;

	if (unlikely(NEW0(node) == NULL))
		return NULL;
	if ((node->bindings = CALLOC(1, (2 * t - 1) * sizeof *node->bindings)) == NULL) {
		FREE(node);
		return NULL;
	}
	return node;
}

/* FIXME */
static void node_free(btree_node_t node) {
	if (node) {
		FREE(node->bindings);
		if (!node->leaf)
			FREE(node->u.children);
		FREE(node);
	}
}

static int cmpdefault(const void *x, const void *y) {
	return x - y;
}

static int btree_split_child(btree_t btree, btree_node_t x, int i) {
	btree_node_t y, z;
	int j;

	y = x->u.children[i];
	if ((z = node_new(btree->t)) == NULL)
		return -1;
	if (!y->leaf) {
		if ((z->u.children = CALLOC(1, 2 * btree->t * sizeof *z->u.children)) == NULL) {
			node_free(z);
			return -1;
		}
		/* FIXME: memmove? */
		for (j = 0; j < btree->t; ++j) {
			z->u.children[j] = y->u.children[j + btree->t];
			y->u.children[j + btree->t] = NULL;
		}
	} else {
		z->u.pn.prev = y;
		z->u.pn.next = y->u.pn.next;
		y->u.pn.next->u.pn.prev = z;
		y->u.pn.next = z;
	}
	z->leaf   = y->leaf;
	z->n      = btree->t - 1;
	z->parent = x;
	/* FIXME: memmove? */
	for (j = 0; j < btree->t - 1; ++j) {
		z->bindings[j].key   = y->bindings[j + btree->t].key;
		z->bindings[j].value = y->bindings[j + btree->t].value;
		y->bindings[j + btree->t].key   = NULL;
		y->bindings[j + btree->t].value = NULL;
	}
	for (j = x->n; j > i; --j)
		x->u.children[j + 1] = x->u.children[j];
	x->u.children[i + 1] = z;
	for (j = x->n - 1; j > i - 1; --j) {
		x->bindings[j + 1].key   = x->bindings[j].key;
		x->bindings[j + 1].value = x->bindings[j].value;
	}
	x->bindings[i].key   = y->bindings[btree->t - 1].key;
	x->bindings[i].value = y->bindings[btree->t - 1].value;
	if (!y->leaf) {
		y->bindings[btree->t - 1].key   = NULL;
		y->bindings[btree->t - 1].value = NULL;
		y->n = btree->t - 1;
	} else
		y->n = btree->t;
	++x->n;
	return 0;
}

static void *btree_put_nonfull(btree_t btree, btree_node_t node, const void *key, void *value) {
	/* avoid gcc warning */
	int i = -1, j, k = 1;

	while (!node->leaf) {
		BSEARCH(node, key, i, k);
		if (k > 0)
			++i;
		if (node->u.children[i]->n == 2 * btree->t - 1) {
			if (btree_split_child(btree, node, i) == -1)
				return NULL;
			if (btree->cmp(key, node->bindings[i].key) > 0)
				++i;
		}
		node = node->u.children[i];
	}
	i = -1, k = 1;
	BSEARCH(node, key, i, k);
	if (k == 0) {
		void *prev;

		prev = node->bindings[i].value;
		node->bindings[i].value = value;
		return prev;
	} else if (k < 0)
		--i;
	/* think about the first pair got inserted */
	for (j = node->n - 1; j > i; --j) {
		node->bindings[j + 1].key   = node->bindings[j].key;
		node->bindings[j + 1].value = node->bindings[j].value;
	}
	node->bindings[i + 1].key   = key;
	node->bindings[i + 1].value = value;
	++node->n;
	++btree->length;
	return NULL;
}

btree_t btree_new(int t, int cmp(const void *x, const void *y),
	void kfree(const void *key), void vfree(void *value)) {
	btree_node_t x, y;
	btree_t btree;

	/* 2-3-4 tree when t == 2 */
	if (t < 2)
		return NULL;
	if ((x = node_new(t)) == NULL)
		return NULL;
	if ((y = node_new(t)) == NULL) {
		node_free(x);
		return NULL;
	}
	x->leaf      = 1;
	y->leaf      = 1;
	x->n         = 0;
	y->n         = 0;
	x->u.pn.next = x->u.pn.prev = y;
	y->u.pn.next = y->u.pn.prev = x;
	if (unlikely(NEW(btree) == NULL)) {
		node_free(y);
		node_free(x);
		return NULL;
	}
	btree->t        = t;
	btree->length   = 0;
	btree->root     = x;
	btree->sentinel = y;
	btree->cmp      = cmp ? cmp : cmpdefault;
	btree->kfree    = kfree;
	btree->vfree    = vfree;
	return btree;
}

/* FIXME */
void btree_free(btree_t *bp) {
	btree_node_t x;

	if (unlikely(bp == NULL || *bp == NULL))
		return;
	x = (*bp)->sentinel->u.pn.next;
	while (x != (*bp)->sentinel) {
		btree_node_t y = x->parent, z = x;
		int i;

		while (y) {
			btree_node_t tmp = y;

			for (i = 0; i < y->n; ++i) {
				y->u.children[i]->parent = NULL;
				if ((*bp)->kfree)
					(*bp)->kfree(tmp->bindings[i].key);
				if ((*bp)->vfree)
					(*bp)->vfree(tmp->bindings[i].value);
			}
			y->u.children[y->n]->parent = NULL;
			y = y->parent;
			node_free(tmp);
		}
		for (i = 0; i < x->n; ++i) {
			if ((*bp)->kfree)
				(*bp)->kfree(z->bindings[i].key);
			if ((*bp)->vfree)
				(*bp)->vfree(z->bindings[i].value);
		}
		x = x->u.pn.next;
		node_free(z);
	}
	node_free(x);
	FREE(*bp);
}

unsigned long btree_length(btree_t btree) {
	if (unlikely(btree == NULL))
		return 0;
	return btree->length;
}

int btree_node_n(btree_node_t node) {
	if (unlikely(node == NULL))
		return 0;
	return node->n;
}

btree_node_t btree_node_next(btree_node_t node) {
	if (unlikely(node == NULL))
		return NULL;
	return node->u.pn.next;
}

const void *btree_node_key(btree_node_t node, int i) {
	if (unlikely(node == NULL))
		return NULL;
	return node->bindings[i].key;
}

void *btree_node_value(btree_node_t node, int i) {
	if (unlikely(node == NULL))
		return NULL;
	return node->bindings[i].value;
}

void *btree_insert(btree_t btree, const void *key, void *value) {
	btree_node_t x;

	if (unlikely(btree == NULL || key == NULL))
		return NULL;
	x = btree->root;
	if (x->n == 2 * btree->t - 1) {
		btree_node_t y;

		if ((y = node_new(btree->t)) == NULL)
			return NULL;
		y->leaf = 0;
		y->n    = 0;
		if ((y->u.children = CALLOC(1, 2 * btree->t * sizeof *y->u.children)) == NULL) {
			node_free(y);
			return NULL;
		}
		y->u.children[0] = x;
		x->parent = y;
		btree->root = y;
		if (btree_split_child(btree, y, 0) == -1)
			return NULL;
		return btree_put_nonfull(btree, y, key, value);
	} else
		return btree_put_nonfull(btree, x, key, value);
}

/* FIXME */
btree_node_t btree_find(btree_t btree, const void *key, int *ip) {
	btree_node_t node;
	/* avoid gcc warning */
	int i = -1, k = 1;

	if (unlikely(btree == NULL || key == NULL))
		return NULL;
	node = btree->root;
	while (!node->leaf) {
		BSEARCH(node, key, i, k);
		if (k > 0)
			++i;
		node = node->u.children[i];
	}
	i = -1, k = 1;
	BSEARCH(node, key, i, k);
	if (k <= 0) {
		*ip = i;
		return node;
	}
	return NULL;
}

/* FIXME */
void *btree_remove(btree_t btree, const void *key) {
	NOT_USED(btree);
	NOT_USED(key);

	return NULL;
}

