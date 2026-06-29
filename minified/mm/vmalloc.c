
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/rbtree.h>
#include <linux/compiler.h>
#include <linux/rbtree_augmented.h>
#include <linux/pgtable.h>

#include "internal.h"

bool is_vmalloc_addr(const void *x)
{
	unsigned long addr = (unsigned long)x;

	return addr >= VMALLOC_START && addr < VMALLOC_END;
}

LIST_HEAD(vmap_area_list);
static bool vmap_initialized __read_mostly;


static struct kmem_cache *vmap_area_cachep;

static LIST_HEAD(free_vmap_area_list);

static struct rb_root free_vmap_area_root = RB_ROOT;

static DEFINE_PER_CPU(struct vmap_area *, ne_fit_preload_node);

static __always_inline unsigned long
va_size(struct vmap_area *va)
{
	return (va->va_end - va->va_start);
}

static __always_inline unsigned long
get_subtree_max_size(struct rb_node *node)
{
	struct vmap_area *va;

	va = rb_entry_safe(node, struct vmap_area, rb_node);
	return va ? va->subtree_max_size : 0;
}

RB_DECLARE_CALLBACKS_MAX(static, free_vmap_area_rb_augment_cb,
	struct vmap_area, rb_node, unsigned long, subtree_max_size, va_size)

static __always_inline struct rb_node **
find_va_links(struct vmap_area *va,
	struct rb_root *root, struct rb_node *from,
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
			WARN(1, "vmalloc bug: 0x%lx-0x%lx overlaps with 0x%lx-0x%lx\n",
				va->va_start, va->va_end, tmp_va->va_start, tmp_va->va_end);

			return NULL;
		}
	} while (*link);

	*parent = &tmp_va->rb_node;
	return link;
}

static __always_inline void
link_va(struct vmap_area *va, struct rb_root *root,
	struct rb_node *parent, struct rb_node **link, struct list_head *head)
{
	
	if (likely(parent)) {
		head = &rb_entry(parent, struct vmap_area, rb_node)->list;
		if (&parent->rb_right != link)
			head = head->prev;
	}

	
	rb_link_node(&va->rb_node, parent, link);
	if (root == &free_vmap_area_root) {
		
		rb_insert_augmented(&va->rb_node,
			root, &free_vmap_area_rb_augment_cb);
		va->subtree_max_size = 0;
	} else {
		rb_insert_color(&va->rb_node, root);
	}

	
	list_add(&va->list, head);
}

static __always_inline void
unlink_va(struct vmap_area *va, struct rb_root *root)
{
	if (WARN_ON(RB_EMPTY_NODE(&va->rb_node)))
		return;

	if (root == &free_vmap_area_root)
		rb_erase_augmented(&va->rb_node,
			root, &free_vmap_area_rb_augment_cb);
	else
		rb_erase(&va->rb_node, root);

	list_del(&va->list);
	RB_CLEAR_NODE(&va->rb_node);
}

static __always_inline void
augment_tree_propagate_from(struct vmap_area *va)
{
	free_vmap_area_rb_augment_cb_propagate(&va->rb_node, NULL);
}

static void
insert_vmap_area_augment(struct vmap_area *va,
	struct rb_node *from, struct rb_root *root,
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

static __always_inline bool
is_within_this_va(struct vmap_area *va, unsigned long size,
	unsigned long align, unsigned long vstart)
{
	unsigned long nva_start_addr;

	if (va->va_start > vstart)
		nva_start_addr = ALIGN(va->va_start, align);
	else
		nva_start_addr = ALIGN(vstart, align);

	
	if (nva_start_addr + size < nva_start_addr ||
			nva_start_addr < vstart)
		return false;

	return (nva_start_addr + size <= va->va_end);
}

static __always_inline struct vmap_area *
find_vmap_lowest_match(unsigned long size, unsigned long align,
	unsigned long vstart, bool adjust_search_size)
{
	struct vmap_area *va;
	struct rb_node *node;
	unsigned long length;

	
	node = free_vmap_area_root.rb_node;

	
	length = adjust_search_size ? size + align - 1 : size;

	while (node) {
		va = rb_entry(node, struct vmap_area, rb_node);

		if (get_subtree_max_size(node->rb_left) >= length &&
				vstart < va->va_start) {
			node = node->rb_left;
		} else {
			if (is_within_this_va(va, size, align, vstart))
				return va;

			
			if (get_subtree_max_size(node->rb_right) >= length) {
				node = node->rb_right;
				continue;
			}

			
			while ((node = rb_parent(node))) {
				va = rb_entry(node, struct vmap_area, rb_node);
				if (is_within_this_va(va, size, align, vstart))
					return va;

				if (get_subtree_max_size(node->rb_right) >= length &&
						vstart <= va->va_start) {
					
					vstart = va->va_start + 1;
					node = node->rb_right;
					break;
				}
			}
		}
	}

	return NULL;
}

enum fit_type {
	NOTHING_FIT = 0,
	FL_FIT_TYPE = 1,	
	LE_FIT_TYPE = 2,	
	RE_FIT_TYPE = 3,	
	NE_FIT_TYPE = 4		
};

static __always_inline enum fit_type
classify_va_fit_type(struct vmap_area *va,
	unsigned long nva_start_addr, unsigned long size)
{
	enum fit_type type;

	
	if (nva_start_addr < va->va_start ||
			nva_start_addr + size > va->va_end)
		return NOTHING_FIT;

	
	if (va->va_start == nva_start_addr) {
		if (va->va_end == nva_start_addr + size)
			type = FL_FIT_TYPE;
		else
			type = LE_FIT_TYPE;
	} else if (va->va_end == nva_start_addr + size) {
		type = RE_FIT_TYPE;
	} else {
		type = NE_FIT_TYPE;
	}

	return type;
}

static __always_inline int
adjust_va_to_fit_type(struct vmap_area *va,
	unsigned long nva_start_addr, unsigned long size,
	enum fit_type type)
{
	struct vmap_area *lva = NULL;

	if (type == FL_FIT_TYPE) {
		
		unlink_va(va, &free_vmap_area_root);
		kmem_cache_free(vmap_area_cachep, va);
	} else if (type == LE_FIT_TYPE) {
		
		va->va_start += size;
	} else if (type == RE_FIT_TYPE) {
		
		va->va_end = nva_start_addr;
	} else if (type == NE_FIT_TYPE) {
		
		lva = __this_cpu_xchg(ne_fit_preload_node, NULL);
		if (unlikely(!lva)) {
			
			lva = kmem_cache_alloc(vmap_area_cachep, GFP_NOWAIT);
			if (!lva)
				return -1;
		}

		
		lva->va_start = va->va_start;
		lva->va_end = nva_start_addr;

		
		va->va_start = nva_start_addr + size;
	} else {
		return -1;
	}

	if (type != FL_FIT_TYPE) {
		augment_tree_propagate_from(va);

		if (lva)	
			insert_vmap_area_augment(lva, &va->rb_node,
				&free_vmap_area_root, &free_vmap_area_list);
	}

	return 0;
}

static __always_inline unsigned long
__alloc_vmap_area(unsigned long size, unsigned long align,
	unsigned long vstart, unsigned long vend)
{
	bool adjust_search_size = true;
	unsigned long nva_start_addr;
	struct vmap_area *va;
	enum fit_type type;
	int ret;

	
	if (align <= PAGE_SIZE || (align > PAGE_SIZE && (vend - vstart) == size))
		adjust_search_size = false;

	va = find_vmap_lowest_match(size, align, vstart, adjust_search_size);
	if (unlikely(!va))
		return vend;

	if (va->va_start > vstart)
		nva_start_addr = ALIGN(va->va_start, align);
	else
		nva_start_addr = ALIGN(vstart, align);

	
	if (nva_start_addr + size > vend)
		return vend;

	
	type = classify_va_fit_type(va, nva_start_addr, size);
	if (WARN_ON_ONCE(type == NOTHING_FIT))
		return vend;

	ret = adjust_va_to_fit_type(va, nva_start_addr, size, type);
	if (ret)
		return vend;

	return nva_start_addr;
}

static inline void
preload_this_cpu_lock(spinlock_t *lock, gfp_t gfp_mask, int node)
{
	struct vmap_area *va = NULL;

	
	if (!this_cpu_read(ne_fit_preload_node))
		va = kmem_cache_alloc_node(vmap_area_cachep, gfp_mask, node);

	spin_lock(lock);

	if (va && __this_cpu_cmpxchg(ne_fit_preload_node, NULL, va))
		kmem_cache_free(vmap_area_cachep, va);
}

static void vmap_init_free_space(void)
{
	unsigned long vmap_start = 1;
	const unsigned long vmap_end = ULONG_MAX;
	struct vmap_area *busy, *free;

	
	list_for_each_entry(busy, &vmap_area_list, list) {
		if (busy->va_start - vmap_start > 0) {
			free = kmem_cache_zalloc(vmap_area_cachep, GFP_NOWAIT);
			if (!WARN_ON_ONCE(!free)) {
				free->va_start = vmap_start;
				free->va_end = busy->va_start;

				insert_vmap_area_augment(free, NULL,
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

void __init vmalloc_init(void)
{
	vmap_area_cachep = KMEM_CACHE(vmap_area, SLAB_PANIC);

	/* The early-boot vmlist (vm_area_add_early) is never populated in this
	   build, so the vmlist import loop was dead and has been removed. */

	vmap_init_free_space();
	vmap_initialized = true;
}

static inline void setup_vmalloc_vm_locked(struct vm_struct *vm,
	struct vmap_area *va, unsigned long flags, const void *caller)
{
	vm->flags = flags;
	vm->addr = (void *)va->va_start;
	vm->size = va->va_end - va->va_start;
	vm->caller = caller;
	va->vm = vm;
}

void vfree(const void *addr)
{
	/*
	 * Anchor stub: runtime coverage (qemu -d exec) shows the kernel never
	 * vfree()s on its only job (boot + print + stay alive) -- vmalloc()
	 * itself is already an anchor-stub returning NULL. Kept link-live
	 * (util.c kvfree, vmalloc.h extern) but body reduced; the whole
	 * __vunmap / remove_vm_area / free_unmap_vmap_area teardown subtree
	 * is orphaned and deleted.
	 */
}


void *__vmalloc_node_range(unsigned long size, unsigned long align,
			unsigned long start, unsigned long end, gfp_t gfp_mask,
			pgprot_t prot, unsigned long vm_flags, int node,
			const void *caller)
{
	/*
	 * Anchor stub: this kernel's only job is boot + print + stay alive,
	 * and runtime coverage (qemu -d exec) shows __vmalloc_node_range is
	 * never executed on that path. Kept link-live (called by
	 * __vmalloc_node) but body reduced to satisfy the linker only.
	 */
	return NULL;
}

void *__vmalloc_node(unsigned long size, unsigned long align,
			    gfp_t gfp_mask, int node, const void *caller)
{
	return __vmalloc_node_range(size, align, VMALLOC_START, VMALLOC_END,
				gfp_mask, PAGE_KERNEL, 0, node, caller);
}

void *__vmalloc(unsigned long size, gfp_t gfp_mask)
{
	return __vmalloc_node(size, 1, gfp_mask, NUMA_NO_NODE,
				__builtin_return_address(0));
}

