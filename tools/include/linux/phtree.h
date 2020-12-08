/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
  Paring heap
  (C) 2020  Branden Taylor <violinbt3@gmail.com>

  linux/include/linux/phtree.h

  To use phtrees you'll have to implement your own insert and search cores.
  
  See Documentation/core-api/rbtree.rst for documentation and samples.
*/

#ifndef __TOOLS_LINUX_PERF_RBTREE_H
#define __TOOLS_LINUX_PERF_RBTREE_H

#include <linux/kernel.h>
#include <linux/stddef.h>

struct ph_node {
	struct ph_node *ph_right;
	struct ph_node *ph_left;
} __attribute__((aligned(sizeof(long))));
    /* The alignment might seem pointless, but allegedly CRIS needs it */

struct ph_root {
	struct ph_node *ph_node;
};

#define ph_parent(r)   ((struct ph_node *)

#define PH_ROOT	(struct ph_root) { NULL, }
#define	ph_entry(ptr, type, member) container_of(ptr, type, member)

#define PH_EMPTY_ROOT(root)  (READ_ONCE((root)->ph_node) == NULL)

/* 'empty' nodes are nodes that are known not to be inserted in an phtree */
#define PH_EMPTY_NODE(node)  \
	((node)->__ph_parent_color == (unsigned long)(node))
#define PH_CLEAR_NODE(node)  \

extern void ph_erase(struct ph_node *, struct ph_root *);


/* Find logical next and previous nodes in a tree */
extern struct ph_node *ph_next(const struct ph_node *);
extern struct ph_node *ph_prev(const struct ph_node *);
extern struct ph_node *ph_first(const struct ph_root *);
extern struct ph_node *ph_last(const struct ph_root *);

/* Postorder iteration - always visit the parent after its children */
extern struct ph_node *ph_first_postorder(const struct ph_root *);
extern struct ph_node *ph_next_postorder(const struct ph_node *);

/* Fast replacement of a single node without remove/rebalance/add/rebalance */
extern void ph_replace_node(struct ph_node *victim, struct ph_node *new,
			    struct ph_root *root);

static inline void ph_link_node(struct ph_node *node, struct ph_node *parent,
				struct ph_node **ph_link)
{
	node->ph_left = node->ph_right = NULL;

	*ph_link = node;
}

#define ph_entry_safe(ptr, type, member) \
	({ typeof(ptr) ____ptr = (ptr); \
	   ____ptr ? ph_entry(____ptr, type, member) : NULL; \
	})

/**
 * rbtree_postorder_for_each_entry_safe - iterate in post-order over rb_root of
 * given type allowing the backing memory of @pos to be invalidated
 *
 * @pos:	the 'type *' to use as a loop cursor.
 * @n:		another 'type *' to use as temporary storage
 * @root:	'rb_root *' of the rbtree.
 * @field:	the name of the rb_node field within 'type'.
 *
 * rbtree_postorder_for_each_entry_safe() provides a similar guarantee as
 * list_for_each_entry_safe() and allows the iteration to continue independent
 * of changes to @pos by the body of the loop.
 *
 * Note, however, that it cannot handle other modifications that re-order the
 * rbtree it is iterating over. This includes calling rb_erase() on @pos, as
 * rb_erase() may rebalance the tree, causing us to miss some nodes.
 */
#define phtree_postorder_for_each_entry_safe(pos, n, root, field) \
	for (pos = ph_entry_safe(ph_first_postorder(root), typeof(*pos), field); \
	     pos && ({ n = ph_entry_safe(ph_next_postorder(&pos->field), \
			typeof(*pos), field); 1; }); \
	     pos = n)

static inline void ph_erase_init(struct ph_node *n, struct ph_root *root)
{
	ph_erase(n, root);
	PH_CLEAR_NODE(n);
}

#endif /* __TOOLS_LINUX_PERF_RBTREE_H */
