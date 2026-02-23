
#include <linux/init.h>
#include <linux/list.h>
#include <asm/page.h>
#include <linux/rbtree.h>
#include <linux/overflow.h>
#include <asm/cpufeature.h>
#include <asm/pgtable_areas.h>
#include <linux/sched/signal.h>
#include <linux/rbtree_augmented.h>
#include <asm/tlbflush.h>

struct vm_area_struct;
struct notifier_block;

struct vm_struct;

struct vmap_area {
	unsigned long va_start;
	unsigned long va_end;

	struct rb_node rb_node;
	struct list_head list;

	union {
		unsigned long subtree_max_size;
		struct vm_struct *vm;
	};
};

extern void __init vmalloc_init(void);

#ifndef ARCH_PAGE_TABLE_SYNC_MASK
#define ARCH_PAGE_TABLE_SYNC_MASK 0
#endif

#include "internal.h"

LIST_HEAD(vmap_area_list);

static struct kmem_cache *vmap_area_cachep;

static LIST_HEAD(free_vmap_area_list);

static struct rb_root free_vmap_area_root = RB_ROOT;

static __always_inline unsigned long va_size(struct vmap_area *va)
{
	return (va->va_end - va->va_start);
}

RB_DECLARE_CALLBACKS_MAX(static, free_vmap_area_rb_augment_cb, struct vmap_area,
			 rb_node, unsigned long, subtree_max_size, va_size)

static __always_inline struct rb_node **find_va_links(struct vmap_area *va,
						      struct rb_root *root,
						      struct rb_node *from,
						      struct rb_node **parent)
{
	struct vmap_area *tmp_va;
	struct rb_node **link;

	if (root) {
		link = &root->rb_node;
		if (unlikely(!*link)) {
			*parent = NULL;
			return link;
		}
	} else {
		link = &from;
	}

	do {
		tmp_va = rb_entry(*link, struct vmap_area, rb_node);

		if (va->va_start < tmp_va->va_end &&
		    va->va_end <= tmp_va->va_start)
			link = &(*link)->rb_left;
		else if (va->va_end > tmp_va->va_start &&
			 va->va_start >= tmp_va->va_end)
			link = &(*link)->rb_right;
		else {
			WARN(1,
			     "vmalloc bug: 0x%lx-0x%lx overlaps with 0x%lx-0x%lx\n",
			     va->va_start, va->va_end, tmp_va->va_start,
			     tmp_va->va_end);

			return NULL;
		}
	} while (*link);

	*parent = &tmp_va->rb_node;
	return link;
}

static __always_inline void link_va(struct vmap_area *va, struct rb_root *root,
				    struct rb_node *parent,
				    struct rb_node **link,
				    struct list_head *head)
{
	if (likely(parent)) {
		head = &rb_entry(parent, struct vmap_area, rb_node)->list;
		if (&parent->rb_right != link)
			head = head->prev;
	}

	rb_link_node(&va->rb_node, parent, link);
	if (root == &free_vmap_area_root) {
		rb_insert_augmented(&va->rb_node, root,
				    &free_vmap_area_rb_augment_cb);
		va->subtree_max_size = 0;
	} else {
		rb_insert_color(&va->rb_node, root);
	}

	list_add(&va->list, head);
}

static __always_inline void augment_tree_propagate_from(struct vmap_area *va)
{
	free_vmap_area_rb_augment_cb_propagate(&va->rb_node, NULL);
}

static void insert_vmap_area_augment(struct vmap_area *va, struct rb_node *from,
				     struct rb_root *root,
				     struct list_head *head)
{
	struct rb_node **link;
	struct rb_node *parent;

	if (from)
		link = find_va_links(va, NULL, from, &parent);
	else
		link = find_va_links(va, root, NULL, &parent);

	if (link) {
		link_va(va, root, parent, link, head);
		augment_tree_propagate_from(va);
	}
}

void __init vmalloc_init(void)
{
	vmap_area_cachep = KMEM_CACHE(vmap_area, SLAB_PANIC);

	{
		unsigned long vmap_start = 1;
		const unsigned long vmap_end = ULONG_MAX;
		struct vmap_area *busy, *free;

		list_for_each_entry(busy, &vmap_area_list, list) {
			if (busy->va_start - vmap_start > 0) {
				free = kmem_cache_zalloc(vmap_area_cachep,
							 GFP_NOWAIT);
				if (!WARN_ON_ONCE(!free)) {
					free->va_start = vmap_start;
					free->va_end = busy->va_start;
					insert_vmap_area_augment(
						free, NULL,
						&free_vmap_area_root,
						&free_vmap_area_list);
				}
			}
			vmap_start = busy->va_end;
		}

		if (vmap_end - vmap_start > 0) {
			free = kmem_cache_zalloc(vmap_area_cachep, GFP_NOWAIT);
			if (!WARN_ON_ONCE(!free)) {
				free->va_start = vmap_start;
				free->va_end = vmap_end;
				insert_vmap_area_augment(free, NULL,
							 &free_vmap_area_root,
							 &free_vmap_area_list);
			}
		}
	}
}
