
#include <linux/vmalloc.h>
#include <linux/mm.h>
/* highmem.h removed - unused */
#include <linux/sched/signal.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
/* linux/interrupt.h removed - no interrupt features used */
/* proc_fs.h, seq_file.h, kallsyms.h, io.h, memcontrol.h, llist.h removed - unused */
/* asm/set_memory.h removed - set_memory functions not used */
#include <linux/list.h>
#include <linux/rbtree.h>
/* linux/rcupdate.h removed - no RCU used */
#include <linux/pfn.h>
/* linux/atomic.h removed - no atomic ops used directly */
#include <linux/rbtree_augmented.h>
/* linux/overflow.h removed - no overflow macros used */
#include <linux/pgtable.h>
#include <linux/uaccess.h>
/* hugetlb.h removed - unused */
#include <linux/sched/mm.h>
#include <asm/tlbflush.h>

#include "internal.h"
/* --- Inlined pgalloc-track.h --- */

/* pgd_none/p4d_none/pud_none always return 0 - folded paging */
static inline p4d_t *p4d_alloc_track(struct mm_struct *mm, pgd_t *pgd,
				     unsigned long address,
				     pgtbl_mod_mask *mod_mask)
{
	return p4d_offset(pgd, address);
}

static inline pud_t *pud_alloc_track(struct mm_struct *mm, p4d_t *p4d,
				     unsigned long address,
				     pgtbl_mod_mask *mod_mask)
{
	return pud_offset(p4d, address);
}

static inline pmd_t *pmd_alloc_track(struct mm_struct *mm, pud_t *pud,
				     unsigned long address,
				     pgtbl_mod_mask *mod_mask)
{
	return pmd_offset(pud, address);
}

#define pte_alloc_kernel_track(pmd, address, mask)                           \
	((unlikely(pmd_none(*(pmd))) && (__pte_alloc_kernel(pmd) || ({       \
						 *(mask) |=                  \
							 PGTBL_PMD_MODIFIED; \
						 0;                          \
					 }))) ?                              \
		 NULL :                                                      \
		 pte_offset_kernel(pmd, address))

/* ioremap_page_range, vmap_pmd_range, vmap_p4d_range removed - never called */
/* vunmap_pmd_range removed - only caller was vunmap_range_noflush which is now dead */

/* vunmap_range_noflush removed - only caller was remove_vm_area which is now dead */

static int vmap_pages_pmd_range(pud_t *pud, unsigned long addr,
				unsigned long end, pgprot_t prot,
				struct page **pages, int *nr,
				pgtbl_mod_mask *mask)
{
	pmd_t *pmd;
	unsigned long next;

	pmd = pmd_alloc_track(&init_mm, pud, addr, mask);
	if (!pmd)
		return -ENOMEM;
	do {
		next = pmd_addr_end(addr, end);
		/* Inlined vmap_pages_pte_range */
		{
			pte_t *pte;
			unsigned long pte_addr = addr;
			pte = pte_alloc_kernel_track(pmd, pte_addr, mask);
			if (!pte)
				return -ENOMEM;
			do {
				struct page *page = pages[*nr];
				if (WARN_ON(!pte_none(*pte)))
					return -EBUSY;
				if (WARN_ON(!page))
					return -ENOMEM;
				if (WARN_ON(!pfn_valid(page_to_pfn(page))))
					return -EINVAL;
				set_pte_at(&init_mm, pte_addr, pte,
					   mk_pte(page, prot));
				(*nr)++;
			} while (pte++, pte_addr += PAGE_SIZE,
				 pte_addr != next);
			*mask |= PGTBL_PTE_MODIFIED;
		}
	} while (pmd++, addr = next, addr != end);
	return 0;
}

/* vmap_pages_p4d_range inlined into vmap_pages_range_noflush */

static int vmap_pages_range_noflush(unsigned long addr, unsigned long end,
				    pgprot_t prot, struct page **pages,
				    unsigned int page_shift)
{
	unsigned long start = addr;
	pgd_t *pgd;
	unsigned long next;
	int nr = 0;
	pgtbl_mod_mask mask = 0;

	WARN_ON(page_shift < PAGE_SHIFT);

	BUG_ON(addr >= end);
	pgd = pgd_offset_k(addr);
	do {
		next = pgd_addr_end(addr, end);
		/* Inlined vmap_pages_p4d_range */
		{
			p4d_t *p4d;
			unsigned long p4d_next;
			unsigned long p4d_addr = addr;

			p4d = p4d_alloc_track(&init_mm, pgd, p4d_addr, &mask);
			if (!p4d)
				return -ENOMEM;
			do {
				p4d_next = p4d_addr_end(p4d_addr, next);
				{
					pud_t *pud;
					unsigned long pud_next;
					unsigned long pud_addr = p4d_addr;

					pud = pud_alloc_track(&init_mm, p4d,
							      pud_addr, &mask);
					if (!pud)
						return -ENOMEM;
					do {
						pud_next = pud_addr_end(
							pud_addr, p4d_next);
						if (vmap_pages_pmd_range(
							    pud, pud_addr,
							    pud_next, prot,
							    pages, &nr, &mask))
							return -ENOMEM;
					} while (pud++, pud_addr = pud_next,
						 pud_addr != p4d_next);
				}
			} while (p4d++, p4d_addr = p4d_next, p4d_addr != next);
		}
	} while (pgd++, addr = next, addr != end);

	if (mask & ARCH_PAGE_TABLE_SYNC_MASK)
		arch_sync_kernel_mappings(start, end);

	return 0;
}

/* vmap_pages_range inlined into __vmalloc_area_node - single caller */
/* vmalloc_to_page removed - no callers */

static DEFINE_SPINLOCK(vmap_area_lock);
static DEFINE_SPINLOCK(free_vmap_area_lock);

LIST_HEAD(vmap_area_list);
static struct rb_root vmap_area_root = RB_ROOT;
static bool vmap_initialized __read_mostly;

/* purge_vmap_area_root, purge_vmap_area_list, purge_vmap_area_lock removed - lazy purge dead (~3 LOC) */

static struct kmem_cache *vmap_area_cachep;

static LIST_HEAD(free_vmap_area_list);

static struct rb_root free_vmap_area_root = RB_ROOT;

static DEFINE_PER_CPU(struct vmap_area *, ne_fit_preload_node);

static __always_inline unsigned long va_size(struct vmap_area *va)
{
	return (va->va_end - va->va_start);
}

static __always_inline unsigned long get_subtree_max_size(struct rb_node *node)
{
	struct vmap_area *va;

	va = rb_entry_safe(node, struct vmap_area, rb_node);
	return va ? va->subtree_max_size : 0;
}

RB_DECLARE_CALLBACKS_MAX(static, free_vmap_area_rb_augment_cb, struct vmap_area,
			 rb_node, unsigned long, subtree_max_size, va_size)

/* purge_vmap_area_lazy removed - lazy purge dead, retry now pointless (~5 LOC) */
/* nr_vmalloc_pages removed - only added to, never read */

/* __find_vmap_area removed - only caller was remove_vm_area which is now dead */

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

static __always_inline void unlink_va(struct vmap_area *va,
				      struct rb_root *root)
{
	if (WARN_ON(RB_EMPTY_NODE(&va->rb_node)))
		return;

	if (root == &free_vmap_area_root)
		rb_erase_augmented(&va->rb_node, root,
				   &free_vmap_area_rb_augment_cb);
	else
		rb_erase(&va->rb_node, root);

	list_del(&va->list);
	RB_CLEAR_NODE(&va->rb_node);
}

static __always_inline void augment_tree_propagate_from(struct vmap_area *va)
{
	free_vmap_area_rb_augment_cb_propagate(&va->rb_node, NULL);
}

/* insert_vmap_area inlined */

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

/* merge_or_add_vmap_area removed - unused */

static __always_inline bool is_within_this_va(struct vmap_area *va,
					      unsigned long size,
					      unsigned long align,
					      unsigned long vstart)
{
	unsigned long nva_start_addr;

	if (va->va_start > vstart)
		nva_start_addr = ALIGN(va->va_start, align);
	else
		nva_start_addr = ALIGN(vstart, align);

	if (nva_start_addr + size < nva_start_addr || nva_start_addr < vstart)
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

				if (get_subtree_max_size(node->rb_right) >=
					    length &&
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

/* classify_va_fit_type inlined into alloc_vmap_area */

static __always_inline int adjust_va_to_fit_type(struct vmap_area *va,
						 unsigned long nva_start_addr,
						 unsigned long size,
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
						 &free_vmap_area_root,
						 &free_vmap_area_list);
	}

	return 0;
}

/* __alloc_vmap_area inlined into alloc_vmap_area */

/* preload_this_cpu_lock inlined into alloc_vmap_area */

static struct vmap_area *
alloc_vmap_area(unsigned long size, unsigned long align, unsigned long vstart,
		unsigned long vend, int node, gfp_t gfp_mask)
{
	struct vmap_area *va;
	/* freed, purged removed - vmap_notify_list call removed, purge_vmap_area_lazy dead */
	unsigned long addr;

	BUG_ON(!size);
	BUG_ON(offset_in_page(size));
	BUG_ON(!is_power_of_2(align));

	if (unlikely(!vmap_initialized))
		return ERR_PTR(-EBUSY);

	gfp_mask = gfp_mask & GFP_RECLAIM_MASK;

	va = kmem_cache_alloc_node(vmap_area_cachep, gfp_mask, node);
	if (unlikely(!va))
		return ERR_PTR(-ENOMEM);

	/* retry label removed - purge_vmap_area_lazy was empty, no point retrying */
	/* preload_this_cpu_lock inlined */
	{
		struct vmap_area *pva = NULL;
		if (!this_cpu_read(ne_fit_preload_node))
			pva = kmem_cache_alloc_node(vmap_area_cachep, gfp_mask,
						    node);
		spin_lock(&free_vmap_area_lock);
		if (pva && __this_cpu_cmpxchg(ne_fit_preload_node, NULL, pva))
			kmem_cache_free(vmap_area_cachep, pva);
	}
	/* Inlined __alloc_vmap_area */
	{
		bool adjust_search_size = true;
		unsigned long nva_start_addr;
		struct vmap_area *found_va;
		enum fit_type type;
		int fit_ret;

		if (align <= PAGE_SIZE ||
		    (align > PAGE_SIZE && (vend - vstart) == size))
			adjust_search_size = false;

		found_va = find_vmap_lowest_match(size, align, vstart,
						  adjust_search_size);
		if (unlikely(!found_va)) {
			addr = vend;
			goto alloc_done;
		}

		if (found_va->va_start > vstart)
			nva_start_addr = ALIGN(found_va->va_start, align);
		else
			nva_start_addr = ALIGN(vstart, align);

		if (nva_start_addr + size > vend) {
			addr = vend;
			goto alloc_done;
		}

		/* classify_va_fit_type inlined */
		if (nva_start_addr < found_va->va_start ||
		    nva_start_addr + size > found_va->va_end)
			type = NOTHING_FIT;
		else if (found_va->va_start == nva_start_addr) {
			if (found_va->va_end == nva_start_addr + size)
				type = FL_FIT_TYPE;
			else
				type = LE_FIT_TYPE;
		} else if (found_va->va_end == nva_start_addr + size)
			type = RE_FIT_TYPE;
		else
			type = NE_FIT_TYPE;
		if (WARN_ON_ONCE(type == NOTHING_FIT)) {
			addr = vend;
			goto alloc_done;
		}

		fit_ret = adjust_va_to_fit_type(found_va, nva_start_addr, size,
						type);
		if (fit_ret) {
			addr = vend;
			goto alloc_done;
		}

		addr = nva_start_addr;
	}
alloc_done:
	spin_unlock(&free_vmap_area_lock);

	if (unlikely(addr == vend)) {
		/* retry label removed - purge_vmap_area_lazy was empty, no point retrying */
		kmem_cache_free(vmap_area_cachep, va);
		return ERR_PTR(-EBUSY);
	}

	va->va_start = addr;
	va->va_end = addr + size;
	va->vm = NULL;

	spin_lock(&vmap_area_lock);
	{
		struct rb_node **link;
		struct rb_node *parent;
		link = find_va_links(va, &vmap_area_root, NULL, &parent);
		if (link)
			link_va(va, &vmap_area_root, parent, link,
				&vmap_area_list);
	}
	spin_unlock(&vmap_area_lock);

	BUG_ON(!IS_ALIGNED(va->va_start, align));
	BUG_ON(va->va_start < vstart);
	BUG_ON(va->va_end > vend);

	return va;
}
/* overflow label, retry logic, purge_vmap_area_lazy call removed (~13 LOC) */

/* lazy_max_pages, vmap_lazy_nr, purge_vmap_area_lazy, drain_vmap_area_work
   and lazy purge infrastructure removed - all stubs that do nothing (~35 LOC) */

/* free_vmap_area_noflush removed - inlined into single caller (~7 LOC) */
/* find_vmap_area inlined - single caller */

/* VMALLOC_SPACE, VMALLOC_PAGES, VMAP_MAX_ALLOC, VMAP_BBMAP_BITS_MAX, VMAP_BBMAP_BITS_MIN, VMAP_MIN, VMAP_MAX removed - never used */
/* VMAP_BBMAP_BITS, vmap_block_queue struct and per-CPU var removed - only initialized, never used */

/* purge_fragmented_blocks_allcpus definition removed - already inlined in stubs above */

/* vmlist, vm_area_page_order, set_vm_area_page_order removed - page order always 0 */

/* vmap_init_free_space inlined into vmalloc_init */

void __init vmalloc_init(void)
{
	/* va, tmp removed - vmlist loop removed (vmlist always NULL) */

	vmap_area_cachep = KMEM_CACHE(vmap_area, SLAB_PANIC);

	/* vmlist loop removed - vmlist was never assigned, always NULL */

	/* Inlined vmap_init_free_space */
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
	vmap_initialized = true;
}

/* setup_vmalloc_vm inlined into __get_vm_area_node */

static struct vm_struct *
__get_vm_area_node(unsigned long size, unsigned long align, unsigned long shift,
		   unsigned long flags, unsigned long start, unsigned long end,
		   int node, gfp_t gfp_mask, const void *caller)
{
	struct vmap_area *va;
	struct vm_struct *area;

	BUG_ON(in_interrupt());
	size = ALIGN(size, 1ul << shift);
	if (unlikely(!size))
		return NULL;

	/* VM_IOREMAP branch removed - never passed by only caller */

	area = kzalloc_node(sizeof(*area), gfp_mask & GFP_RECLAIM_MASK, node);
	if (unlikely(!area))
		return NULL;

	if (!(flags & VM_NO_GUARD))
		size += PAGE_SIZE;

	va = alloc_vmap_area(size, align, start, end, node, gfp_mask);
	if (IS_ERR(va)) {
		kfree(area);
		return NULL;
	}

	/* setup_vmalloc_vm inlined */
	spin_lock(&vmap_area_lock);
	area->flags = flags;
	area->addr = (void *)va->va_start;
	area->size = va->va_end - va->va_start;
	area->caller = caller;
	va->vm = area;
	spin_unlock(&vmap_area_lock);

	return area;
}

/* get_vm_area_caller, find_vm_area removed - never called */

/* remove_vm_area removed - only caller was free_vm_area which is now dead */

/* vfree, vunmap moved to vmalloc.h as static inline */
/* vmap removed - never called */

static inline unsigned int vm_area_alloc_pages(gfp_t gfp, int nid,
					       unsigned int order,
					       unsigned int nr_pages,
					       struct page **pages)
{
	unsigned int nr_allocated = 0;
	/* order is always 0 - use bulk allocation */
	gfp_t bulk_gfp = gfp & ~__GFP_NOFAIL;

	while (nr_allocated < nr_pages) {
		unsigned int nr, nr_pages_request;

		nr_pages_request = min(100U, nr_pages - nr_allocated);

		nr = alloc_pages_bulk_array_node(
			bulk_gfp, nid, nr_pages_request, pages + nr_allocated);

		nr_allocated += nr;
		cond_resched();

		if (nr != nr_pages_request)
			break;
	}

	/* Fallback: allocate remaining pages one at a time */
	while (nr_allocated < nr_pages) {
		struct page *page;

		if (fatal_signal_pending(current))
			break;

		if (nid == NUMA_NO_NODE)
			page = alloc_pages(gfp, 0);
		else
			page = alloc_pages_node(nid, gfp, 0);
		if (unlikely(!page))
			break;

		pages[nr_allocated] = page;
		cond_resched();
		nr_allocated++;
	}

	return nr_allocated;
}

static void *__vmalloc_area_node(struct vm_struct *area, gfp_t gfp_mask,
				 pgprot_t prot, unsigned int page_shift,
				 int node)
{
	const gfp_t nested_gfp = (gfp_mask & GFP_RECLAIM_MASK) | __GFP_ZERO;
	bool nofail = gfp_mask & __GFP_NOFAIL;
	unsigned long addr = (unsigned long)area->addr;
	/* get_vm_area_size inlined */
	unsigned long size = (area->flags & VM_NO_GUARD) ?
				     area->size :
				     area->size - PAGE_SIZE;
	unsigned long array_size;
	unsigned int nr_small_pages = size >> PAGE_SHIFT;
	unsigned int flags;
	int ret;

	array_size = (unsigned long)nr_small_pages * sizeof(struct page *);
	gfp_mask |= __GFP_NOWARN;
	if (!(gfp_mask & (GFP_DMA | GFP_DMA32)))
		gfp_mask |= __GFP_HIGHMEM;

	if (array_size > PAGE_SIZE) {
		area->pages = __vmalloc_node(array_size, 1, nested_gfp, node,
					     area->caller);
	} else {
		area->pages = kmalloc_node(array_size, nested_gfp, node);
	}

	if (!area->pages) {
		/* free_vm_area chain removed - OOM at boot is fatal anyway */
		BUG();
	}

	area->nr_pages = vm_area_alloc_pages(gfp_mask | __GFP_NOWARN, node, 0,
					     nr_small_pages, area->pages);

	/* nr_vmalloc_pages addition removed - counter never read */
	/* mod_memcg_page_state is empty stub */
	if (area->nr_pages != nr_small_pages) {
		goto fail;
	}

	if ((gfp_mask & (__GFP_FS | __GFP_IO)) == __GFP_IO)
		flags = memalloc_nofs_save();
	else if ((gfp_mask & (__GFP_FS | __GFP_IO)) == 0)
		flags = memalloc_noio_save();

	/* vmap_pages_range inlined - flush_cache_vmap is empty stub on x86 */
	do {
		ret = vmap_pages_range_noflush(addr, addr + size, prot,
					       area->pages, page_shift);
		if (nofail && (ret < 0))
			schedule_timeout_uninterruptible(1);
	} while (nofail && (ret < 0));

	if ((gfp_mask & (__GFP_FS | __GFP_IO)) == __GFP_IO)
		memalloc_nofs_restore(flags);
	else if ((gfp_mask & (__GFP_FS | __GFP_IO)) == 0)
		memalloc_noio_restore(flags);

	if (ret < 0) {
		goto fail;
	}

	return area->addr;

fail:
	/* __vfree was no-op */
	return NULL;
}

void *__vmalloc_node_range(unsigned long size, unsigned long align,
			   unsigned long start, unsigned long end,
			   gfp_t gfp_mask, pgprot_t prot,
			   unsigned long vm_flags, int node, const void *caller)
{
	struct vm_struct *area;

	if (WARN_ON_ONCE(!size))
		return NULL;

	if ((size >> PAGE_SHIFT) > totalram_pages()) {
		return NULL;
	}

again:
	area = __get_vm_area_node(size, align, PAGE_SHIFT,
				  VM_ALLOC | VM_UNINITIALIZED | vm_flags, start,
				  end, node, gfp_mask, caller);
	if (!area) {
		if (gfp_mask & __GFP_NOFAIL) {
			schedule_timeout_uninterruptible(1);
			goto again;
		}
		return NULL;
	}

	if (!__vmalloc_area_node(area, gfp_mask, prot, PAGE_SHIFT, node))
		return NULL;

	smp_wmb();
	area->flags &= ~VM_UNINITIALIZED;

	return area->addr;
}

void *__vmalloc_node(unsigned long size, unsigned long align, gfp_t gfp_mask,
		     int node, const void *caller)
{
	return __vmalloc_node_range(size, align, VMALLOC_START, VMALLOC_END,
				    gfp_mask, PAGE_KERNEL, 0, node, caller);
}

/* __vmalloc, vmalloc removed - never called */

/* free_vm_area removed - only called from error path, replaced with BUG() */
