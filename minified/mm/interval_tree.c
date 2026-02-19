
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/rmap.h>
#include <linux/rbtree_augmented.h>

#define INTERVAL_TREE_DEFINE(ITSTRUCT, ITRB, ITTYPE, ITSUBTREE, ITSTART,     \
			     ITLAST, ITSTATIC, ITPREFIX)                     \
                                                                             \
	RB_DECLARE_CALLBACKS_MAX(static, ITPREFIX##_augment, ITSTRUCT, ITRB, \
				 ITTYPE, ITSUBTREE, ITLAST)                  \
                                                                             \
	ITSTATIC void ITPREFIX##_insert(ITSTRUCT *node,                      \
					struct rb_root_cached *root)         \
	{                                                                    \
		struct rb_node **link = &root->rb_root.rb_node,              \
			       *rb_parent = NULL;                            \
		ITTYPE start = ITSTART(node), last = ITLAST(node);           \
		ITSTRUCT *parent;                                            \
		bool leftmost = true;                                        \
                                                                             \
		while (*link) {                                              \
			rb_parent = *link;                                   \
			parent = rb_entry(rb_parent, ITSTRUCT, ITRB);        \
			if (parent->ITSUBTREE < last)                        \
				parent->ITSUBTREE = last;                    \
			if (start < ITSTART(parent))                         \
				link = &parent->ITRB.rb_left;                \
			else {                                               \
				link = &parent->ITRB.rb_right;               \
				leftmost = false;                            \
			}                                                    \
		}                                                            \
                                                                             \
		node->ITSUBTREE = last;                                      \
		rb_link_node(&node->ITRB, rb_parent, link);                  \
		rb_insert_augmented_cached(&node->ITRB, root, leftmost,      \
					   &ITPREFIX##_augment);             \
	}                                                                    \
                                                                             \
	ITSTATIC void ITPREFIX##_remove(ITSTRUCT *node,                      \
					struct rb_root_cached *root)         \
	{                                                                    \
		rb_erase_augmented_cached(&node->ITRB, root,                 \
					  &ITPREFIX##_augment);              \
	}

static inline unsigned long vma_start_pgoff(struct vm_area_struct *v)
{
	return v->vm_pgoff;
}

static inline unsigned long vma_last_pgoff(struct vm_area_struct *v)
{
	return v->vm_pgoff + vma_pages(v) - 1;
}

INTERVAL_TREE_DEFINE(struct vm_area_struct, shared.rb, unsigned long,
		     shared.rb_subtree_last, vma_start_pgoff, vma_last_pgoff, ,
		     vma_interval_tree)

static inline unsigned long avc_start_pgoff(struct anon_vma_chain *avc)
{
	return vma_start_pgoff(avc->vma);
}

static inline unsigned long avc_last_pgoff(struct anon_vma_chain *avc)
{
	return vma_last_pgoff(avc->vma);
}

INTERVAL_TREE_DEFINE(struct anon_vma_chain, rb, unsigned long, rb_subtree_last,
		     avc_start_pgoff, avc_last_pgoff, static inline,
		     __anon_vma_interval_tree)

void anon_vma_interval_tree_insert(struct anon_vma_chain *node,
				   struct rb_root_cached *root)
{
	__anon_vma_interval_tree_insert(node, root);
}

void anon_vma_interval_tree_remove(struct anon_vma_chain *node,
				   struct rb_root_cached *root)
{
	__anon_vma_interval_tree_remove(node, root);
}
