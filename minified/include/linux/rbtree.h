#ifndef	_LINUX_RBTREE_H
#define	_LINUX_RBTREE_H
struct rb_node { unsigned long __rb_parent_color; struct rb_node *rb_right; struct rb_node *rb_left; } __attribute__((aligned(sizeof(long))));
struct rb_root { struct rb_node *rb_node; };
struct rb_root_cached { struct rb_root rb_root; struct rb_node *rb_leftmost; };
#define RB_ROOT (struct rb_root) { NULL, }
#define RB_ROOT_CACHED (struct rb_root_cached) { {NULL, }, NULL }
#include <linux/kernel.h>
#include <linux/stddef.h>
#include <linux/rcupdate.h>
#define rb_parent(r)   ((struct rb_node *)((r)->__rb_parent_color & ~3))
#define rb_entry(ptr, type, member) container_of(ptr, type, member)
#define RB_EMPTY_ROOT(root)  (READ_ONCE((root)->rb_node) == NULL)
#define RB_EMPTY_NODE(node)  ((node)->__rb_parent_color == (unsigned long)(node))
#define RB_CLEAR_NODE(node)  ((node)->__rb_parent_color = (unsigned long)(node))
extern void rb_insert_color(struct rb_node *, struct rb_root *);
extern void rb_erase(struct rb_node *, struct rb_root *);
extern struct rb_node *rb_next(const struct rb_node *);
static inline void rb_link_node(struct rb_node *node, struct rb_node *parent, struct rb_node **rb_link) { node->__rb_parent_color = (unsigned long)parent; node->rb_left = node->rb_right = NULL; *rb_link = node; }
#define rb_entry_safe(ptr, type, member) ({ typeof(ptr) ____ptr = (ptr); ____ptr ? rb_entry(____ptr, type, member) : NULL; })
#define rb_first_cached(root) (root)->rb_leftmost
static inline void rb_insert_color_cached(struct rb_node *node, struct rb_root_cached *root, bool leftmost) { if (leftmost) root->rb_leftmost = node; rb_insert_color(node, &root->rb_root); }
static inline struct rb_node *rb_erase_cached(struct rb_node *node, struct rb_root_cached *root) { struct rb_node *leftmost = NULL; if (root->rb_leftmost == node) leftmost = root->rb_leftmost = rb_next(node); rb_erase(node, &root->rb_root); return leftmost; }
static __always_inline struct rb_node *rb_add_cached(struct rb_node *node, struct rb_root_cached *tree, bool (*less)(struct rb_node *, const struct rb_node *)) {
	struct rb_node **link = &tree->rb_root.rb_node; struct rb_node *parent = NULL; bool leftmost = true;
	while (*link) { parent = *link; if (less(node, parent)) link = &parent->rb_left; else { link = &parent->rb_right; leftmost = false; } }
	rb_link_node(node, parent, link); rb_insert_color_cached(node, tree, leftmost); return leftmost ? node : NULL;
}
#endif
