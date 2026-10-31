
#ifndef _LINUX_RBTREE_H
#define _LINUX_RBTREE_H

struct rb_node {
	unsigned long __rb_parent_color;
	struct rb_node *rb_right, *rb_left;
} __attribute__((aligned(sizeof(long))));

struct rb_root {
	struct rb_node *rb_node;
};

#define RB_ROOT          \
	(struct rb_root) \
	{                \
		NULL,    \
	}
/* end rbtree_types.h */

#include <linux/kernel.h>
#include <linux/rcupdate.h>

#define rb_parent(r) ((struct rb_node *)((r)->__rb_parent_color & ~3))

#define rb_entry(ptr, type, member) container_of(ptr, type, member)

#define RB_EMPTY_NODE(node) ((node)->__rb_parent_color == (unsigned long)(node))
extern void rb_insert_color(struct rb_node *, struct rb_root *);
extern void rb_erase(struct rb_node *, struct rb_root *);

extern struct rb_node *rb_next(const struct rb_node *);

static inline void rb_link_node(struct rb_node *node, struct rb_node *parent,
				struct rb_node **rb_link)
{
	node->__rb_parent_color = (unsigned long)parent;
	node->rb_left = node->rb_right = NULL;

	*rb_link = node;
}

/* Removed: rb_replace_node_cached - never called (~7 LOC) */

/* rb_add, rb_find_add, rb_find, rb_find_first, rb_next_match - unused */

#endif
