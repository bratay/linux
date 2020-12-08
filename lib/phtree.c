// SPDX-License-Identifier: GPL-2.0-or-later
/*
  Paring heap
  (C) 2020 Branden Taylor <violinbt3@gmail.com>

  linux/lib/phtree.c
*/

#include <linux/phtree_augmented.h>e
#include <linux/export.h>

/*
 * Pairing heap properties:  https://en.wikipedia.org/wiki/Pairing_heap
 *
 * find-min: simply return the top element of the heap.
 * meld: compare the two root elements, the smaller remains the root of the result, the larger element and its subtree is appended as a child of this root.
 * insert: create a new heap for the inserted element and meld into the original heap.
 * delete-min: remove the root and do repeated melds of its subtrees until one tree remains. Various merging strategies are employed.
 */


static __always_inline void
__ph_insert(struct ph_node *node, struct ph_root *root,
	    void (*augment_rotate)(struct ph_node *old, struct ph_node *new))
{
	struct ph_node *parent = ph_red_parent(node), *gparent, *tmp;

	while (true) {
		/*
		 * Loop invariant: node is red.
		 */
		if (unlikely(!parent)) {
			/*
			 * The inserted node is root. Either this is the
			 * first node, or we recursed at Case 1 below and
			 * are no longer violating 4).
			 */
			ph_set_parent_color(node, NULL, ph_BLACK);
			break;
		}

		/*
		 * If there is a black parent, we are done.
		 * Otherwise, take some corrective action as,
		 * per 4), we don't want a red root or two
		 * consecutive red nodes.
		 */
		if(ph_is_black(parent))
			break;

		gparent = ph_red_parent(parent);

		tmp = gparent->ph_right;
		if (parent != tmp) {	/* parent == gparent->ph_left */
			if (tmp && ph_is_red(tmp)) {
				/*
				 * Case 1 - node's uncle is red (color flips).
				 *
				 *       G            g
				 *      / \          / \
				 *     p   u  -->   P   U
				 *    /            /
				 *   n            n
				 *
				 * However, since g's parent might be red, and
				 * 4) does not allow this, we need to recurse
				 * at g.
				 */
				ph_set_parent_color(tmp, gparent, ph_BLACK);
				ph_set_parent_color(parent, gparent, ph_BLACK);
				node = gparent;
				parent = ph_parent(node);
				ph_set_parent_color(node, parent, ph_RED);
				continue;
			}

			tmp = parent->ph_right;
			if (node == tmp) {
				/*
				 * Case 2 - node's uncle is black and node is
				 * the parent's right child (left rotate at parent).
				 *
				 *      G             G
				 *     / \           / \
				 *    p   U  -->    n   U
				 *     \           /
				 *      n         p
				 *
				 * This still leaves us in violation of 4), the
				 * continuation into Case 3 will fix that.
				 */
				tmp = node->ph_left;
				WRITE_ONCE(parent->ph_right, tmp);
				WRITE_ONCE(node->ph_left, parent);
				if (tmp)
					ph_set_parent_color(tmp, parent,
							    ph_BLACK);
				ph_set_parent_color(parent, node, ph_RED);
				augment_rotate(parent, node);
				parent = node;
				tmp = node->ph_right;
			}

			/*
			 * Case 3 - node's uncle is black and node is
			 * the parent's left child (right rotate at gparent).
			 *
			 *        G           P
			 *       / \         / \
			 *      p   U  -->  n   g
			 *     /                 \
			 *    n                   U
			 */
			WRITE_ONCE(gparent->ph_left, tmp); /* == parent->ph_right */
			WRITE_ONCE(parent->ph_right, gparent);
			if (tmp)
				ph_set_parent_color(tmp, gparent, ph_BLACK);
			__ph_rotate_set_parents(gparent, parent, root, ph_RED);
			augment_rotate(gparent, parent);
			break;
		} else {
			tmp = gparent->ph_left;
			if (tmp && ph_is_red(tmp)) {
				/* Case 1 - color flips */
				ph_set_parent_color(tmp, gparent, ph_BLACK);
				ph_set_parent_color(parent, gparent, ph_BLACK);
				node = gparent;
				parent = ph_parent(node);
				ph_set_parent_color(node, parent, ph_RED);
				continue;
			}

			tmp = parent->ph_left;
			if (node == tmp) {
				/* Case 2 - right rotate at parent */
				tmp = node->ph_right;
				WRITE_ONCE(parent->ph_left, tmp);
				WRITE_ONCE(node->ph_right, parent);
				if (tmp)
					ph_set_parent_color(tmp, parent,
							    ph_BLACK);
				ph_set_parent_color(parent, node, ph_RED);
				augment_rotate(parent, node);
				parent = node;
				tmp = node->ph_left;
			}

			/* Case 3 - left rotate at gparent */
			WRITE_ONCE(gparent->ph_right, tmp); /* == parent->ph_left */
			WRITE_ONCE(parent->ph_left, gparent);
			if (tmp)
				ph_set_parent_color(tmp, gparent, ph_BLACK);
			__ph_rotate_set_parents(gparent, parent, root, ph_RED);
			augment_rotate(gparent, parent);
			break;
		}
	}
}

/*
 * Non-augmented phtree manipulation functions.
 *
 * We use dummy augmented callbacks here, and have the compiler optimize them
 * out of the ph_insert_color() and ph_erase() function definitions.
 */

static inline void dummy_propagate(struct ph_node *node, struct ph_node *stop) {}
static inline void dummy_copy(struct ph_node *old, struct ph_node *new) {}
static inline void dummy_rotate(struct ph_node *old, struct ph_node *new) {}

static const struct ph_augment_callbacks dummy_callbacks = {
	.propagate = dummy_propagate,
	.copy = dummy_copy,
	.rotate = dummy_rotate
};

void ph_insert_color(struct ph_node *node, struct ph_root *root)
{
	__ph_insert(node, root, dummy_rotate);
}
EXPORT_SYMBOL(ph_insert_color);

void ph_erase(struct ph_node *node, struct ph_root *root)
{
	struct ph_node *rebalance;
	rebalance = __ph_erase_augmented(node, root, &dummy_callbacks);
	if (rebalance)
		____ph_erase_color(rebalance, root, dummy_rotate);
}
EXPORT_SYMBOL(ph_erase);

/*
 * Augmented phtree manipulation functions.
 *
 * This instantiates the same __always_inline functions as in the non-augmented
 * case, but this time with user-defined callbacks.
 */

void __ph_insert_augmented(struct ph_node *node, struct ph_root *root,
	void (*augment_rotate)(struct ph_node *old, struct ph_node *new))
{
	__ph_insert(node, root, augment_rotate);
}
EXPORT_SYMBOL(__ph_insert_augmented);

/*
 * This function returns the first node (in sort order) of the tree.
 */
struct ph_node *ph_first(const struct ph_root *root)
{
	struct ph_node	*n;

	n = root->ph_node;
	if (!n)
		return NULL;
	while (n->ph_left)
		n = n->ph_left;
	return n;
}
EXPORT_SYMBOL(ph_first);

struct ph_node *ph_last(const struct ph_root *root)
{
	struct ph_node	*n;

	n = root->ph_node;
	if (!n)
		return NULL;
	while (n->ph_right)
		n = n->ph_right;
	return n;
}
EXPORT_SYMBOL(ph_last);

struct ph_node *ph_next(const struct ph_node *node)
{
	struct ph_node *parent;

	if (ph_EMPTY_NODE(node))
		return NULL;

	/*
	 * If we have a right-hand child, go down and then left as far
	 * as we can.
	 */
	if (node->ph_right) {
		node = node->ph_right;
		while (node->ph_left)
			node = node->ph_left;
		return (struct ph_node *)node;
	}

	/*
	 * No right-hand children. Everything down and left is smaller than us,
	 * so any 'next' node must be in the general direction of our parent.
	 * Go up the tree; any time the ancestor is a right-hand child of its
	 * parent, keep going up. First time it's a left-hand child of its
	 * parent, said parent is our 'next' node.
	 */
	while ((parent = ph_parent(node)) && node == parent->ph_right)
		node = parent;

	return parent;
}
EXPORT_SYMBOL(ph_next);

struct ph_node *ph_prev(const struct ph_node *node)
{
	struct ph_node *parent;

	if (ph_EMPTY_NODE(node))
		return NULL;

	/*
	 * If we have a left-hand child, go down and then right as far
	 * as we can.
	 */
	if (node->ph_left) {
		node = node->ph_left;
		while (node->ph_right)
			node = node->ph_right;
		return (struct ph_node *)node;
	}

	/*
	 * No left-hand children. Go up till we find an ancestor which
	 * is a right-hand child of its parent.
	 */
	while ((parent = ph_parent(node)) && node == parent->ph_left)
		node = parent;

	return parent;
}
EXPORT_SYMBOL(ph_prev);

void ph_replace_node(struct ph_node *victim, struct ph_node *new,
		     struct ph_root *root)
{
	struct ph_node *parent = ph_parent(victim);

	/* Copy the pointers/colour from the victim to the replacement */
	*new = *victim;

	/* Set the surrounding nodes to point to the replacement */
	if (victim->ph_left)
		ph_set_parent(victim->ph_left, new);
	if (victim->ph_right)
		ph_set_parent(victim->ph_right, new);
	__ph_change_child(victim, new, parent, root);
}
EXPORT_SYMBOL(ph_replace_node);

void ph_replace_node_rcu(struct ph_node *victim, struct ph_node *new,
			 struct ph_root *root)
{
	struct ph_node *parent = ph_parent(victim);

	/* Copy the pointers/colour from the victim to the replacement */
	*new = *victim;

	/* Set the surrounding nodes to point to the replacement */
	if (victim->ph_left)
		ph_set_parent(victim->ph_left, new);
	if (victim->ph_right)
		ph_set_parent(victim->ph_right, new);

	/* Set the parent's pointer to the new node last after an RCU barrier
	 * so that the pointers onwards are seen to be set correctly when doing
	 * an RCU walk over the tree.
	 */
	__ph_change_child_rcu(victim, new, parent, root);
}
EXPORT_SYMBOL(ph_replace_node_rcu);

static struct ph_node *ph_left_deepest_node(const struct ph_node *node)
{
	for (;;) {
		if (node->ph_left)
			node = node->ph_left;
		else if (node->ph_right)
			node = node->ph_right;
		else
			return (struct ph_node *)node;
	}
}

struct ph_node *ph_next_postorder(const struct ph_node *node)
{
	const struct ph_node *parent;
	if (!node)
		return NULL;
	parent = ph_parent(node);

	/* If we're sitting on node, we've already seen our children */
	if (parent && node == parent->ph_left && parent->ph_right) {
		/* If we are the parent's left node, go to the parent's right
		 * node then all the way down to the left */
		return ph_left_deepest_node(parent->ph_right);
	} else
		/* Otherwise we are the parent's right node, and the parent
		 * should be next */
		return (struct ph_node *)parent;
}
EXPORT_SYMBOL(ph_next_postorder);

struct ph_node *ph_first_postorder(const struct ph_root *root)
{
	if (!root->ph_node)
		return NULL;

	return ph_left_deepest_node(root->ph_node);
}

EXPORT_SYMBOL(ph_first_postorder);
