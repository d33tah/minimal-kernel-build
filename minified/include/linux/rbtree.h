#ifndef	_LINUX_RBTREE_H
#define	_LINUX_RBTREE_H
struct rb_node { unsigned long __rb_parent_color; struct rb_node *rb_right; struct rb_node *rb_left; } __attribute__((aligned(sizeof(long))));
struct rb_root { struct rb_node *rb_node; };
struct rb_root_cached { struct rb_root rb_root; struct rb_node *rb_leftmost; };
#define RB_ROOT (struct rb_root) { NULL, }
#define RB_ROOT_CACHED (struct rb_root_cached) { {NULL, }, NULL }
#include <linux/kernel.h>
#include <linux/stddef.h>
#define rb_parent(r)   ((struct rb_node *)((r)->__rb_parent_color & ~3))
#define rb_entry(ptr, type, member) container_of(ptr, type, member)
#define RB_EMPTY_NODE(node)  ((node)->__rb_parent_color == (unsigned long)(node))
static inline void rb_link_node(struct rb_node *node, struct rb_node *parent, struct rb_node **rb_link) { node->__rb_parent_color = (unsigned long)parent; node->rb_left = node->rb_right = NULL; *rb_link = node; }
#endif
