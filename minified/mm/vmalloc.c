// SPDX-License-Identifier: GPL-2.0-only

#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/highmem.h>
#include <linux/sched/signal.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/set_memory.h>
#include <linux/kallsyms.h>
#include <linux/list.h>
#include <linux/notifier.h>
#include <linux/rbtree.h>
#include <linux/xarray.h>
#include <linux/io.h>
#include <linux/rcupdate.h>
#include <linux/pfn.h>
#include <linux/kmemleak.h>
#include <linux/atomic.h>
#include <linux/compiler.h>
#include <linux/memcontrol.h>
#include <linux/llist.h>
#include <linux/bitops.h>
#include <linux/rbtree_augmented.h>
#include <linux/overflow.h>
#include <linux/pgtable.h>
#include <linux/uaccess.h>
#include <linux/hugetlb.h>
#include <linux/sched/mm.h>
#include <asm/tlbflush.h>
#include <asm/shmparam.h>

#include "internal.h"
#include "pgalloc-track.h"

static const unsigned int ioremap_max_page_shift = PAGE_SHIFT;

static const bool vmap_allow_huge = false;

bool is_vmalloc_addr(const void *x)
{
	unsigned long addr = (unsigned long)kasan_reset_tag(x);

	return addr >= VMALLOC_START && addr < VMALLOC_END;
}

struct vfree_deferred {
	struct llist_head list;
	struct work_struct wq;
};
static DEFINE_PER_CPU(struct vfree_deferred, vfree_deferred);

static void __vunmap(const void *, int);

static void free_work(struct work_struct *w)
{
	struct vfree_deferred *p = container_of(w, struct vfree_deferred, wq);
	struct llist_node *t, *llnode;

	llist_for_each_safe(llnode, t, llist_del_all(&p->list))
		__vunmap((void *)llnode, 1);
}

static int vmap_pte_range(pmd_t *pmd, unsigned long addr, unsigned long end,
			phys_addr_t phys_addr, pgprot_t prot,
			unsigned int max_page_shift, pgtbl_mod_mask *mask)
{
	pte_t *pte;
	u64 pfn;
	unsigned long size = PAGE_SIZE;

	pfn = phys_addr >> PAGE_SHIFT;
	pte = pte_alloc_kernel_track(pmd, addr, mask);
	if (!pte)
		return -ENOMEM;
	do {
		BUG_ON(!pte_none(*pte));

		set_pte_at(&init_mm, addr, pte, pfn_pte(pfn, prot));
		pfn++;
	} while (pte += PFN_DOWN(size), addr += size, addr != end);
	*mask |= PGTBL_PTE_MODIFIED;
	return 0;
}

static int vmap_try_huge_pmd(pmd_t *pmd, unsigned long addr, unsigned long end,
			phys_addr_t phys_addr, pgprot_t prot,
			unsigned int max_page_shift)
{
	if (max_page_shift < PMD_SHIFT)
		return 0;

	if (!arch_vmap_pmd_supported(prot))
		return 0;

	if ((end - addr) != PMD_SIZE)
		return 0;

	if (!IS_ALIGNED(addr, PMD_SIZE))
		return 0;

	if (!IS_ALIGNED(phys_addr, PMD_SIZE))
		return 0;

	if (pmd_present(*pmd) && !pmd_free_pte_page(pmd, addr))
		return 0;

	return pmd_set_huge(pmd, phys_addr, prot);
}

static int vmap_pmd_range(pud_t *pud, unsigned long addr, unsigned long end,
			phys_addr_t phys_addr, pgprot_t prot,
			unsigned int max_page_shift, pgtbl_mod_mask *mask)
{
	pmd_t *pmd;
	unsigned long next;

	pmd = pmd_alloc_track(&init_mm, pud, addr, mask);
	if (!pmd)
		return -ENOMEM;
	do {
		next = pmd_addr_end(addr, end);

		if (vmap_try_huge_pmd(pmd, addr, next, phys_addr, prot,
					max_page_shift)) {
			*mask |= PGTBL_PMD_MODIFIED;
			continue;
		}

		if (vmap_pte_range(pmd, addr, next, phys_addr, prot, max_page_shift, mask))
			return -ENOMEM;
	} while (pmd++, phys_addr += (next - addr), addr = next, addr != end);
	return 0;
}

static int vmap_try_huge_pud(pud_t *pud, unsigned long addr, unsigned long end,
			phys_addr_t phys_addr, pgprot_t prot,
			unsigned int max_page_shift)
{
	if (max_page_shift < PUD_SHIFT)
		return 0;

	if (!arch_vmap_pud_supported(prot))
		return 0;

	if ((end - addr) != PUD_SIZE)
		return 0;

	if (!IS_ALIGNED(addr, PUD_SIZE))
		return 0;

	if (!IS_ALIGNED(phys_addr, PUD_SIZE))
		return 0;

	if (pud_present(*pud) && !pud_free_pmd_page(pud, addr))
		return 0;

	return pud_set_huge(pud, phys_addr, prot);
}

static int vmap_pud_range(p4d_t *p4d, unsigned long addr, unsigned long end,
			phys_addr_t phys_addr, pgprot_t prot,
			unsigned int max_page_shift, pgtbl_mod_mask *mask)
{
	pud_t *pud;
	unsigned long next;

	pud = pud_alloc_track(&init_mm, p4d, addr, mask);
	if (!pud)
		return -ENOMEM;
	do {
		next = pud_addr_end(addr, end);

		if (vmap_try_huge_pud(pud, addr, next, phys_addr, prot,
					max_page_shift)) {
			*mask |= PGTBL_PUD_MODIFIED;
			continue;
		}

		if (vmap_pmd_range(pud, addr, next, phys_addr, prot,
					max_page_shift, mask))
			return -ENOMEM;
	} while (pud++, phys_addr += (next - addr), addr = next, addr != end);
	return 0;
}

static int vmap_try_huge_p4d(p4d_t *p4d, unsigned long addr, unsigned long end,
			phys_addr_t phys_addr, pgprot_t prot,
			unsigned int max_page_shift)
{
	if (max_page_shift < P4D_SHIFT)
		return 0;

	if (!arch_vmap_p4d_supported(prot))
		return 0;

	if ((end - addr) != P4D_SIZE)
		return 0;

	if (!IS_ALIGNED(addr, P4D_SIZE))
		return 0;

	if (!IS_ALIGNED(phys_addr, P4D_SIZE))
		return 0;

	if (p4d_present(*p4d) && !p4d_free_pud_page(p4d, addr))
		return 0;

	return p4d_set_huge(p4d, phys_addr, prot);
}

static int vmap_p4d_range(pgd_t *pgd, unsigned long addr, unsigned long end,
			phys_addr_t phys_addr, pgprot_t prot,
			unsigned int max_page_shift, pgtbl_mod_mask *mask)
{
	p4d_t *p4d;
	unsigned long next;

	p4d = p4d_alloc_track(&init_mm, pgd, addr, mask);
	if (!p4d)
		return -ENOMEM;
	do {
		next = p4d_addr_end(addr, end);

		if (vmap_try_huge_p4d(p4d, addr, next, phys_addr, prot,
					max_page_shift)) {
			*mask |= PGTBL_P4D_MODIFIED;
			continue;
		}

		if (vmap_pud_range(p4d, addr, next, phys_addr, prot,
					max_page_shift, mask))
			return -ENOMEM;
	} while (p4d++, phys_addr += (next - addr), addr = next, addr != end);
	return 0;
}

static int vmap_range_noflush(unsigned long addr, unsigned long end,
			phys_addr_t phys_addr, pgprot_t prot,
			unsigned int max_page_shift)
{
	pgd_t *pgd;
	unsigned long start;
	unsigned long next;
	int err;
	pgtbl_mod_mask mask = 0;

	might_sleep();
	BUG_ON(addr >= end);

	start = addr;
	pgd = pgd_offset_k(addr);
	do {
		next = pgd_addr_end(addr, end);
		err = vmap_p4d_range(pgd, addr, next, phys_addr, prot,
					max_page_shift, &mask);
		if (err)
			break;
	} while (pgd++, phys_addr += (next - addr), addr = next, addr != end);

	if (mask & ARCH_PAGE_TABLE_SYNC_MASK)
		arch_sync_kernel_mappings(start, end);

	return err;
}

int ioremap_page_range(unsigned long addr, unsigned long end,
		phys_addr_t phys_addr, pgprot_t prot)
{
	int err;

	err = vmap_range_noflush(addr, end, phys_addr, pgprot_nx(prot),
				 ioremap_max_page_shift);
	flush_cache_vmap(addr, end);
	return err;
}

static void vunmap_pte_range(pmd_t *pmd, unsigned long addr, unsigned long end,
			     pgtbl_mod_mask *mask)
{
	pte_t *pte;

	pte = pte_offset_kernel(pmd, addr);
	do {
		pte_t ptent = ptep_get_and_clear(&init_mm, addr, pte);
		WARN_ON(!pte_none(ptent) && !pte_present(ptent));
	} while (pte++, addr += PAGE_SIZE, addr != end);
	*mask |= PGTBL_PTE_MODIFIED;
}

static void vunmap_pmd_range(pud_t *pud, unsigned long addr, unsigned long end,
			     pgtbl_mod_mask *mask)
{
	pmd_t *pmd;
	unsigned long next;
	int cleared;

	pmd = pmd_offset(pud, addr);
	do {
		next = pmd_addr_end(addr, end);

		cleared = pmd_clear_huge(pmd);
		if (cleared || pmd_bad(*pmd))
			*mask |= PGTBL_PMD_MODIFIED;

		if (cleared)
			continue;
		if (pmd_none_or_clear_bad(pmd))
			continue;
		vunmap_pte_range(pmd, addr, next, mask);

		cond_resched();
	} while (pmd++, addr = next, addr != end);
}

static void vunmap_pud_range(p4d_t *p4d, unsigned long addr, unsigned long end,
			     pgtbl_mod_mask *mask)
{
	pud_t *pud;
	unsigned long next;
	int cleared;

	pud = pud_offset(p4d, addr);
	do {
		next = pud_addr_end(addr, end);

		cleared = pud_clear_huge(pud);
		if (cleared || pud_bad(*pud))
			*mask |= PGTBL_PUD_MODIFIED;

		if (cleared)
			continue;
		if (pud_none_or_clear_bad(pud))
			continue;
		vunmap_pmd_range(pud, addr, next, mask);
	} while (pud++, addr = next, addr != end);
}

static void vunmap_p4d_range(pgd_t *pgd, unsigned long addr, unsigned long end,
			     pgtbl_mod_mask *mask)
{
	p4d_t *p4d;
	unsigned long next;

	p4d = p4d_offset(pgd, addr);
	do {
		next = p4d_addr_end(addr, end);

		p4d_clear_huge(p4d);
		if (p4d_bad(*p4d))
			*mask |= PGTBL_P4D_MODIFIED;

		if (p4d_none_or_clear_bad(p4d))
			continue;
		vunmap_pud_range(p4d, addr, next, mask);
	} while (p4d++, addr = next, addr != end);
}

void vunmap_range_noflush(unsigned long start, unsigned long end)
{
	unsigned long next;
	pgd_t *pgd;
	unsigned long addr = start;
	pgtbl_mod_mask mask = 0;

	BUG_ON(addr >= end);
	pgd = pgd_offset_k(addr);
	do {
		next = pgd_addr_end(addr, end);
		if (pgd_bad(*pgd))
			mask |= PGTBL_PGD_MODIFIED;
		if (pgd_none_or_clear_bad(pgd))
			continue;
		vunmap_p4d_range(pgd, addr, next, &mask);
	} while (pgd++, addr = next, addr != end);

	if (mask & ARCH_PAGE_TABLE_SYNC_MASK)
		arch_sync_kernel_mappings(start, end);
}

void vunmap_range(unsigned long addr, unsigned long end)
{
	flush_cache_vunmap(addr, end);
	vunmap_range_noflush(addr, end);
	flush_tlb_kernel_range(addr, end);
}

static int vmap_pages_pte_range(pmd_t *pmd, unsigned long addr,
		unsigned long end, pgprot_t prot, struct page **pages, int *nr,
		pgtbl_mod_mask *mask)
{
	pte_t *pte;

	

	pte = pte_alloc_kernel_track(pmd, addr, mask);
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

		set_pte_at(&init_mm, addr, pte, mk_pte(page, prot));
		(*nr)++;
	} while (pte++, addr += PAGE_SIZE, addr != end);
	*mask |= PGTBL_PTE_MODIFIED;
	return 0;
}

static int vmap_pages_pmd_range(pud_t *pud, unsigned long addr,
		unsigned long end, pgprot_t prot, struct page **pages, int *nr,
		pgtbl_mod_mask *mask)
{
	pmd_t *pmd;
	unsigned long next;

	pmd = pmd_alloc_track(&init_mm, pud, addr, mask);
	if (!pmd)
		return -ENOMEM;
	do {
		next = pmd_addr_end(addr, end);
		if (vmap_pages_pte_range(pmd, addr, next, prot, pages, nr, mask))
			return -ENOMEM;
	} while (pmd++, addr = next, addr != end);
	return 0;
}

static int vmap_pages_pud_range(p4d_t *p4d, unsigned long addr,
		unsigned long end, pgprot_t prot, struct page **pages, int *nr,
		pgtbl_mod_mask *mask)
{
	pud_t *pud;
	unsigned long next;

	pud = pud_alloc_track(&init_mm, p4d, addr, mask);
	if (!pud)
		return -ENOMEM;
	do {
		next = pud_addr_end(addr, end);
		if (vmap_pages_pmd_range(pud, addr, next, prot, pages, nr, mask))
			return -ENOMEM;
	} while (pud++, addr = next, addr != end);
	return 0;
}

static int vmap_pages_p4d_range(pgd_t *pgd, unsigned long addr,
		unsigned long end, pgprot_t prot, struct page **pages, int *nr,
		pgtbl_mod_mask *mask)
{
	p4d_t *p4d;
	unsigned long next;

	p4d = p4d_alloc_track(&init_mm, pgd, addr, mask);
	if (!p4d)
		return -ENOMEM;
	do {
		next = p4d_addr_end(addr, end);
		if (vmap_pages_pud_range(p4d, addr, next, prot, pages, nr, mask))
			return -ENOMEM;
	} while (p4d++, addr = next, addr != end);
	return 0;
}

static int vmap_small_pages_range_noflush(unsigned long addr, unsigned long end,
		pgprot_t prot, struct page **pages)
{
	unsigned long start = addr;
	pgd_t *pgd;
	unsigned long next;
	int err = 0;
	int nr = 0;
	pgtbl_mod_mask mask = 0;

	BUG_ON(addr >= end);
	pgd = pgd_offset_k(addr);
	do {
		next = pgd_addr_end(addr, end);
		if (pgd_bad(*pgd))
			mask |= PGTBL_PGD_MODIFIED;
		err = vmap_pages_p4d_range(pgd, addr, next, prot, pages, &nr, &mask);
		if (err)
			return err;
	} while (pgd++, addr = next, addr != end);

	if (mask & ARCH_PAGE_TABLE_SYNC_MASK)
		arch_sync_kernel_mappings(start, end);

	return 0;
}

int vmap_pages_range_noflush(unsigned long addr, unsigned long end,
		pgprot_t prot, struct page **pages, unsigned int page_shift)
{
	unsigned int i, nr = (end - addr) >> PAGE_SHIFT;

	WARN_ON(page_shift < PAGE_SHIFT);

	if (!IS_ENABLED(CONFIG_HAVE_ARCH_HUGE_VMALLOC) ||
			page_shift == PAGE_SHIFT)
		return vmap_small_pages_range_noflush(addr, end, prot, pages);

	for (i = 0; i < nr; i += 1U << (page_shift - PAGE_SHIFT)) {
		int err;

		err = vmap_range_noflush(addr, addr + (1UL << page_shift),
					__pa(page_address(pages[i])), prot,
					page_shift);
		if (err)
			return err;

		addr += 1UL << page_shift;
	}

	return 0;
}

static int vmap_pages_range(unsigned long addr, unsigned long end,
		pgprot_t prot, struct page **pages, unsigned int page_shift)
{
	int err;

	err = vmap_pages_range_noflush(addr, end, prot, pages, page_shift);
	flush_cache_vmap(addr, end);
	return err;
}

int is_vmalloc_or_module_addr(const void *x)
{
	
	return is_vmalloc_addr(x);
}

struct page *vmalloc_to_page(const void *vmalloc_addr)
{
	unsigned long addr = (unsigned long) vmalloc_addr;
	struct page *page = NULL;
	pgd_t *pgd = pgd_offset_k(addr);
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *ptep, pte;

	
	VIRTUAL_BUG_ON(!is_vmalloc_or_module_addr(vmalloc_addr));

	if (pgd_none(*pgd))
		return NULL;
	if (WARN_ON_ONCE(pgd_leaf(*pgd)))
		return NULL; 
	if (WARN_ON_ONCE(pgd_bad(*pgd)))
		return NULL;

	p4d = p4d_offset(pgd, addr);
	if (p4d_none(*p4d))
		return NULL;
	if (p4d_leaf(*p4d))
		return p4d_page(*p4d) + ((addr & ~P4D_MASK) >> PAGE_SHIFT);
	if (WARN_ON_ONCE(p4d_bad(*p4d)))
		return NULL;

	pud = pud_offset(p4d, addr);
	if (pud_none(*pud))
		return NULL;
	if (pud_leaf(*pud))
		return pud_page(*pud) + ((addr & ~PUD_MASK) >> PAGE_SHIFT);
	if (WARN_ON_ONCE(pud_bad(*pud)))
		return NULL;

	pmd = pmd_offset(pud, addr);
	if (pmd_none(*pmd))
		return NULL;
	if (pmd_leaf(*pmd))
		return pmd_page(*pmd) + ((addr & ~PMD_MASK) >> PAGE_SHIFT);
	if (WARN_ON_ONCE(pmd_bad(*pmd)))
		return NULL;

	ptep = pte_offset_map(pmd, addr);
	pte = *ptep;
	if (pte_present(pte))
		page = pte_page(pte);
	pte_unmap(ptep);

	return page;
}

unsigned long vmalloc_to_pfn(const void *vmalloc_addr)
{
	return page_to_pfn(vmalloc_to_page(vmalloc_addr));
}

#define DEBUG_AUGMENT_PROPAGATE_CHECK 0
#define DEBUG_AUGMENT_LOWEST_MATCH_CHECK 0

static DEFINE_SPINLOCK(vmap_area_lock);
static DEFINE_SPINLOCK(free_vmap_area_lock);

LIST_HEAD(vmap_area_list);
static struct rb_root vmap_area_root = RB_ROOT;
static bool vmap_initialized __read_mostly;

static struct rb_root purge_vmap_area_root = RB_ROOT;
static LIST_HEAD(purge_vmap_area_list);
static DEFINE_SPINLOCK(purge_vmap_area_lock);

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

static void purge_vmap_area_lazy(void);
static BLOCKING_NOTIFIER_HEAD(vmap_notify_list);
static void drain_vmap_area_work(struct work_struct *work);
static DECLARE_WORK(drain_vmap_work, drain_vmap_area_work);

static atomic_long_t nr_vmalloc_pages;

unsigned long vmalloc_nr_pages(void)
{
	return atomic_long_read(&nr_vmalloc_pages);
}

static struct vmap_area *find_vmap_area_exceed_addr(unsigned long addr)
{
	struct vmap_area *va = NULL;
	struct rb_node *n = vmap_area_root.rb_node;

	addr = (unsigned long)kasan_reset_tag((void *)addr);

	while (n) {
		struct vmap_area *tmp;

		tmp = rb_entry(n, struct vmap_area, rb_node);
		if (tmp->va_end > addr) {
			va = tmp;
			if (tmp->va_start <= addr)
				break;

			n = n->rb_left;
		} else
			n = n->rb_right;
	}

	return va;
}

static struct vmap_area *__find_vmap_area(unsigned long addr)
{
	struct rb_node *n = vmap_area_root.rb_node;

	addr = (unsigned long)kasan_reset_tag((void *)addr);

	while (n) {
		struct vmap_area *va;

		va = rb_entry(n, struct vmap_area, rb_node);
		if (addr < va->va_start)
			n = n->rb_left;
		else if (addr >= va->va_end)
			n = n->rb_right;
		else
			return va;
	}

	return NULL;
}

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

static __always_inline struct list_head *
get_va_next_sibling(struct rb_node *parent, struct rb_node **link)
{
	struct list_head *list;

	if (unlikely(!parent))
		
		return NULL;

	list = &rb_entry(parent, struct vmap_area, rb_node)->list;
	return (&parent->rb_right == link ? list->next : list);
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

#if DEBUG_AUGMENT_PROPAGATE_CHECK

static __always_inline unsigned long
compute_subtree_max_size(struct vmap_area *va)
{
	return max3(va_size(va),
		get_subtree_max_size(va->rb_node.rb_left),
		get_subtree_max_size(va->rb_node.rb_right));
}

static void
augment_tree_propagate_check(void)
{
	struct vmap_area *va;
	unsigned long computed_size;

	list_for_each_entry(va, &free_vmap_area_list, list) {
		computed_size = compute_subtree_max_size(va);
		if (computed_size != va->subtree_max_size)
			pr_emerg("tree is corrupted: %lu, %lu\n",
				va_size(va), va->subtree_max_size);
	}
}
#endif

static __always_inline void
augment_tree_propagate_from(struct vmap_area *va)
{
	
	free_vmap_area_rb_augment_cb_propagate(&va->rb_node, NULL);

#if DEBUG_AUGMENT_PROPAGATE_CHECK
	augment_tree_propagate_check();
#endif
}

static void
insert_vmap_area(struct vmap_area *va,
	struct rb_root *root, struct list_head *head)
{
	struct rb_node **link;
	struct rb_node *parent;

	link = find_va_links(va, root, NULL, &parent);
	if (link)
		link_va(va, root, parent, link, head);
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

static __always_inline struct vmap_area *
merge_or_add_vmap_area(struct vmap_area *va,
	struct rb_root *root, struct list_head *head)
{
	struct vmap_area *sibling;
	struct list_head *next;
	struct rb_node **link;
	struct rb_node *parent;
	bool merged = false;

	
	link = find_va_links(va, root, NULL, &parent);
	if (!link)
		return NULL;

	
	next = get_va_next_sibling(parent, link);
	if (unlikely(next == NULL))
		goto insert;

	
	if (next != head) {
		sibling = list_entry(next, struct vmap_area, list);
		if (sibling->va_start == va->va_end) {
			sibling->va_start = va->va_start;

			
			kmem_cache_free(vmap_area_cachep, va);

			
			va = sibling;
			merged = true;
		}
	}

	
	if (next->prev != head) {
		sibling = list_entry(next->prev, struct vmap_area, list);
		if (sibling->va_end == va->va_start) {
			
			if (merged)
				unlink_va(va, root);

			sibling->va_end = va->va_end;

			
			kmem_cache_free(vmap_area_cachep, va);

			
			va = sibling;
			merged = true;
		}
	}

insert:
	if (!merged)
		link_va(va, root, parent, link, head);

	return va;
}

static __always_inline struct vmap_area *
merge_or_add_vmap_area_augment(struct vmap_area *va,
	struct rb_root *root, struct list_head *head)
{
	va = merge_or_add_vmap_area(va, root, head);
	if (va)
		augment_tree_propagate_from(va);

	return va;
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

#if DEBUG_AUGMENT_LOWEST_MATCH_CHECK
#include <linux/random.h>

static struct vmap_area *
find_vmap_lowest_linear_match(unsigned long size,
	unsigned long align, unsigned long vstart)
{
	struct vmap_area *va;

	list_for_each_entry(va, &free_vmap_area_list, list) {
		if (!is_within_this_va(va, size, align, vstart))
			continue;

		return va;
	}

	return NULL;
}

static void
find_vmap_lowest_match_check(unsigned long size, unsigned long align)
{
	struct vmap_area *va_1, *va_2;
	unsigned long vstart;
	unsigned int rnd;

	get_random_bytes(&rnd, sizeof(rnd));
	vstart = VMALLOC_START + rnd;

	va_1 = find_vmap_lowest_match(size, align, vstart, false);
	va_2 = find_vmap_lowest_linear_match(size, align, vstart);

	if (va_1 != va_2)
		pr_emerg("not lowest: t: 0x%p, l: 0x%p, v: 0x%lx\n",
			va_1, va_2, vstart);
}
#endif

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

#if DEBUG_AUGMENT_LOWEST_MATCH_CHECK
	find_vmap_lowest_match_check(size, align);
#endif

	return nva_start_addr;
}

static void free_vmap_area(struct vmap_area *va)
{
	
	spin_lock(&vmap_area_lock);
	unlink_va(va, &vmap_area_root);
	spin_unlock(&vmap_area_lock);

	
	spin_lock(&free_vmap_area_lock);
	merge_or_add_vmap_area_augment(va, &free_vmap_area_root, &free_vmap_area_list);
	spin_unlock(&free_vmap_area_lock);
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

static struct vmap_area *alloc_vmap_area(unsigned long size,
				unsigned long align,
				unsigned long vstart, unsigned long vend,
				int node, gfp_t gfp_mask)
{
	struct vmap_area *va;
	unsigned long freed;
	unsigned long addr;
	int purged = 0;
	int ret;

	BUG_ON(!size);
	BUG_ON(offset_in_page(size));
	BUG_ON(!is_power_of_2(align));

	if (unlikely(!vmap_initialized))
		return ERR_PTR(-EBUSY);

	might_sleep();
	gfp_mask = gfp_mask & GFP_RECLAIM_MASK;

	va = kmem_cache_alloc_node(vmap_area_cachep, gfp_mask, node);
	if (unlikely(!va))
		return ERR_PTR(-ENOMEM);

	
	kmemleak_scan_area(&va->rb_node, SIZE_MAX, gfp_mask);

retry:
	preload_this_cpu_lock(&free_vmap_area_lock, gfp_mask, node);
	addr = __alloc_vmap_area(size, align, vstart, vend);
	spin_unlock(&free_vmap_area_lock);

	
	if (unlikely(addr == vend))
		goto overflow;

	va->va_start = addr;
	va->va_end = addr + size;
	va->vm = NULL;

	spin_lock(&vmap_area_lock);
	insert_vmap_area(va, &vmap_area_root, &vmap_area_list);
	spin_unlock(&vmap_area_lock);

	BUG_ON(!IS_ALIGNED(va->va_start, align));
	BUG_ON(va->va_start < vstart);
	BUG_ON(va->va_end > vend);

	ret = kasan_populate_vmalloc(addr, size);
	if (ret) {
		free_vmap_area(va);
		return ERR_PTR(ret);
	}

	return va;

overflow:
	if (!purged) {
		purge_vmap_area_lazy();
		purged = 1;
		goto retry;
	}

	freed = 0;
	blocking_notifier_call_chain(&vmap_notify_list, 0, &freed);

	if (freed > 0) {
		purged = 0;
		goto retry;
	}

	if (!(gfp_mask & __GFP_NOWARN) && printk_ratelimit())
		pr_warn("vmap allocation for size %lu failed: use vmalloc=<size> to increase size\n",
			size);

	kmem_cache_free(vmap_area_cachep, va);
	return ERR_PTR(-EBUSY);
}

int register_vmap_purge_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&vmap_notify_list, nb);
}

int unregister_vmap_purge_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&vmap_notify_list, nb);
}

static unsigned long lazy_max_pages(void)
{
	unsigned int log;

	log = fls(num_online_cpus());

	return log * (32UL * 1024 * 1024 / PAGE_SIZE);
}

static atomic_long_t vmap_lazy_nr = ATOMIC_LONG_INIT(0);

static DEFINE_MUTEX(vmap_purge_lock);

static void purge_fragmented_blocks_allcpus(void);

static bool __purge_vmap_area_lazy(unsigned long start, unsigned long end)
{
	unsigned long resched_threshold;
	struct list_head local_pure_list;
	struct vmap_area *va, *n_va;

	lockdep_assert_held(&vmap_purge_lock);

	spin_lock(&purge_vmap_area_lock);
	purge_vmap_area_root = RB_ROOT;
	list_replace_init(&purge_vmap_area_list, &local_pure_list);
	spin_unlock(&purge_vmap_area_lock);

	if (unlikely(list_empty(&local_pure_list)))
		return false;

	start = min(start,
		list_first_entry(&local_pure_list,
			struct vmap_area, list)->va_start);

	end = max(end,
		list_last_entry(&local_pure_list,
			struct vmap_area, list)->va_end);

	flush_tlb_kernel_range(start, end);
	resched_threshold = lazy_max_pages() << 1;

	spin_lock(&free_vmap_area_lock);
	list_for_each_entry_safe(va, n_va, &local_pure_list, list) {
		unsigned long nr = (va->va_end - va->va_start) >> PAGE_SHIFT;
		unsigned long orig_start = va->va_start;
		unsigned long orig_end = va->va_end;

		
		va = merge_or_add_vmap_area_augment(va, &free_vmap_area_root,
				&free_vmap_area_list);

		if (!va)
			continue;

		if (is_vmalloc_or_module_addr((void *)orig_start))
			kasan_release_vmalloc(orig_start, orig_end,
					      va->va_start, va->va_end);

		atomic_long_sub(nr, &vmap_lazy_nr);

		if (atomic_long_read(&vmap_lazy_nr) < resched_threshold)
			cond_resched_lock(&free_vmap_area_lock);
	}
	spin_unlock(&free_vmap_area_lock);
	return true;
}

static void purge_vmap_area_lazy(void)
{
	mutex_lock(&vmap_purge_lock);
	purge_fragmented_blocks_allcpus();
	__purge_vmap_area_lazy(ULONG_MAX, 0);
	mutex_unlock(&vmap_purge_lock);
}

static void drain_vmap_area_work(struct work_struct *work)
{
	unsigned long nr_lazy;

	do {
		mutex_lock(&vmap_purge_lock);
		__purge_vmap_area_lazy(ULONG_MAX, 0);
		mutex_unlock(&vmap_purge_lock);

		
		nr_lazy = atomic_long_read(&vmap_lazy_nr);
	} while (nr_lazy > lazy_max_pages());
}

static void free_vmap_area_noflush(struct vmap_area *va)
{
	unsigned long nr_lazy;

	spin_lock(&vmap_area_lock);
	unlink_va(va, &vmap_area_root);
	spin_unlock(&vmap_area_lock);

	nr_lazy = atomic_long_add_return((va->va_end - va->va_start) >>
				PAGE_SHIFT, &vmap_lazy_nr);

	
	spin_lock(&purge_vmap_area_lock);
	merge_or_add_vmap_area(va,
		&purge_vmap_area_root, &purge_vmap_area_list);
	spin_unlock(&purge_vmap_area_lock);

	
	if (unlikely(nr_lazy > lazy_max_pages()))
		schedule_work(&drain_vmap_work);
}

static void free_unmap_vmap_area(struct vmap_area *va)
{
	flush_cache_vunmap(va->va_start, va->va_end);
	vunmap_range_noflush(va->va_start, va->va_end);
	if (debug_pagealloc_enabled_static())
		flush_tlb_kernel_range(va->va_start, va->va_end);

	free_vmap_area_noflush(va);
}

struct vmap_area *find_vmap_area(unsigned long addr)
{
	struct vmap_area *va;

	spin_lock(&vmap_area_lock);
	va = __find_vmap_area(addr);
	spin_unlock(&vmap_area_lock);

	return va;
}

#if BITS_PER_LONG == 32
#define VMALLOC_SPACE		(128UL*1024*1024)
#else
#define VMALLOC_SPACE		(128UL*1024*1024*1024)
#endif

#define VMALLOC_PAGES		(VMALLOC_SPACE / PAGE_SIZE)
#define VMAP_MAX_ALLOC		BITS_PER_LONG	
#define VMAP_BBMAP_BITS_MAX	1024	
#define VMAP_BBMAP_BITS_MIN	(VMAP_MAX_ALLOC*2)
#define VMAP_MIN(x, y)		((x) < (y) ? (x) : (y)) 
#define VMAP_MAX(x, y)		((x) > (y) ? (x) : (y)) 
#define VMAP_BBMAP_BITS		\
		VMAP_MIN(VMAP_BBMAP_BITS_MAX,	\
		VMAP_MAX(VMAP_BBMAP_BITS_MIN,	\
			VMALLOC_PAGES / roundup_pow_of_two(NR_CPUS) / 16))

#define VMAP_BLOCK_SIZE		(VMAP_BBMAP_BITS * PAGE_SIZE)

struct vmap_block_queue {
	spinlock_t lock;
	struct list_head free;
};

struct vmap_block {
	spinlock_t lock;
	struct vmap_area *va;
	unsigned long free, dirty;
	unsigned long dirty_min, dirty_max; 
	struct list_head free_list;
	struct rcu_head rcu_head;
	struct list_head purge;
};

static DEFINE_PER_CPU(struct vmap_block_queue, vmap_block_queue);

static DEFINE_XARRAY(vmap_blocks);

static unsigned long addr_to_vb_idx(unsigned long addr)
{
	addr -= VMALLOC_START & ~(VMAP_BLOCK_SIZE-1);
	addr /= VMAP_BLOCK_SIZE;
	return addr;
}

static void *vmap_block_vaddr(unsigned long va_start, unsigned long pages_off)
{
	unsigned long addr;

	addr = va_start + (pages_off << PAGE_SHIFT);
	BUG_ON(addr_to_vb_idx(addr) != addr_to_vb_idx(va_start));
	return (void *)addr;
}

static void *new_vmap_block(unsigned int order, gfp_t gfp_mask)
{
	struct vmap_block_queue *vbq;
	struct vmap_block *vb;
	struct vmap_area *va;
	unsigned long vb_idx;
	int node, err;
	void *vaddr;

	node = numa_node_id();

	vb = kmalloc_node(sizeof(struct vmap_block),
			gfp_mask & GFP_RECLAIM_MASK, node);
	if (unlikely(!vb))
		return ERR_PTR(-ENOMEM);

	va = alloc_vmap_area(VMAP_BLOCK_SIZE, VMAP_BLOCK_SIZE,
					VMALLOC_START, VMALLOC_END,
					node, gfp_mask);
	if (IS_ERR(va)) {
		kfree(vb);
		return ERR_CAST(va);
	}

	vaddr = vmap_block_vaddr(va->va_start, 0);
	spin_lock_init(&vb->lock);
	vb->va = va;
	
	BUG_ON(VMAP_BBMAP_BITS <= (1UL << order));
	vb->free = VMAP_BBMAP_BITS - (1UL << order);
	vb->dirty = 0;
	vb->dirty_min = VMAP_BBMAP_BITS;
	vb->dirty_max = 0;
	INIT_LIST_HEAD(&vb->free_list);

	vb_idx = addr_to_vb_idx(va->va_start);
	err = xa_insert(&vmap_blocks, vb_idx, vb, gfp_mask);
	if (err) {
		kfree(vb);
		free_vmap_area(va);
		return ERR_PTR(err);
	}

	vbq = raw_cpu_ptr(&vmap_block_queue);
	spin_lock(&vbq->lock);
	list_add_tail_rcu(&vb->free_list, &vbq->free);
	spin_unlock(&vbq->lock);

	return vaddr;
}

static void free_vmap_block(struct vmap_block *vb)
{
	struct vmap_block *tmp;

	tmp = xa_erase(&vmap_blocks, addr_to_vb_idx(vb->va->va_start));
	BUG_ON(tmp != vb);

	free_vmap_area_noflush(vb->va);
	kfree_rcu(vb, rcu_head);
}

static void purge_fragmented_blocks(int cpu)
{
	LIST_HEAD(purge);
	struct vmap_block *vb;
	struct vmap_block *n_vb;
	struct vmap_block_queue *vbq = &per_cpu(vmap_block_queue, cpu);

	rcu_read_lock();
	list_for_each_entry_rcu(vb, &vbq->free, free_list) {

		if (!(vb->free + vb->dirty == VMAP_BBMAP_BITS && vb->dirty != VMAP_BBMAP_BITS))
			continue;

		spin_lock(&vb->lock);
		if (vb->free + vb->dirty == VMAP_BBMAP_BITS && vb->dirty != VMAP_BBMAP_BITS) {
			vb->free = 0; 
			vb->dirty = VMAP_BBMAP_BITS; 
			vb->dirty_min = 0;
			vb->dirty_max = VMAP_BBMAP_BITS;
			spin_lock(&vbq->lock);
			list_del_rcu(&vb->free_list);
			spin_unlock(&vbq->lock);
			spin_unlock(&vb->lock);
			list_add_tail(&vb->purge, &purge);
		} else
			spin_unlock(&vb->lock);
	}
	rcu_read_unlock();

	list_for_each_entry_safe(vb, n_vb, &purge, purge) {
		list_del(&vb->purge);
		free_vmap_block(vb);
	}
}

static void purge_fragmented_blocks_allcpus(void)
{
	int cpu;

	for_each_possible_cpu(cpu)
		purge_fragmented_blocks(cpu);
}

static void *vb_alloc(unsigned long size, gfp_t gfp_mask)
{
	struct vmap_block_queue *vbq;
	struct vmap_block *vb;
	void *vaddr = NULL;
	unsigned int order;

	BUG_ON(offset_in_page(size));
	BUG_ON(size > PAGE_SIZE*VMAP_MAX_ALLOC);
	if (WARN_ON(size == 0)) {
		
		return NULL;
	}
	order = get_order(size);

	rcu_read_lock();
	vbq = raw_cpu_ptr(&vmap_block_queue);
	list_for_each_entry_rcu(vb, &vbq->free, free_list) {
		unsigned long pages_off;

		spin_lock(&vb->lock);
		if (vb->free < (1UL << order)) {
			spin_unlock(&vb->lock);
			continue;
		}

		pages_off = VMAP_BBMAP_BITS - vb->free;
		vaddr = vmap_block_vaddr(vb->va->va_start, pages_off);
		vb->free -= 1UL << order;
		if (vb->free == 0) {
			spin_lock(&vbq->lock);
			list_del_rcu(&vb->free_list);
			spin_unlock(&vbq->lock);
		}

		spin_unlock(&vb->lock);
		break;
	}

	rcu_read_unlock();

	
	if (!vaddr)
		vaddr = new_vmap_block(order, gfp_mask);

	return vaddr;
}

static void vb_free(unsigned long addr, unsigned long size)
{
	unsigned long offset;
	unsigned int order;
	struct vmap_block *vb;

	BUG_ON(offset_in_page(size));
	BUG_ON(size > PAGE_SIZE*VMAP_MAX_ALLOC);

	flush_cache_vunmap(addr, addr + size);

	order = get_order(size);
	offset = (addr & (VMAP_BLOCK_SIZE - 1)) >> PAGE_SHIFT;
	vb = xa_load(&vmap_blocks, addr_to_vb_idx(addr));

	vunmap_range_noflush(addr, addr + size);

	if (debug_pagealloc_enabled_static())
		flush_tlb_kernel_range(addr, addr + size);

	spin_lock(&vb->lock);

	
	vb->dirty_min = min(vb->dirty_min, offset);
	vb->dirty_max = max(vb->dirty_max, offset + (1UL << order));

	vb->dirty += 1UL << order;
	if (vb->dirty == VMAP_BBMAP_BITS) {
		BUG_ON(vb->free);
		spin_unlock(&vb->lock);
		free_vmap_block(vb);
	} else
		spin_unlock(&vb->lock);
}

static void _vm_unmap_aliases(unsigned long start, unsigned long end, int flush)
{
	int cpu;

	if (unlikely(!vmap_initialized))
		return;

	might_sleep();

	for_each_possible_cpu(cpu) {
		struct vmap_block_queue *vbq = &per_cpu(vmap_block_queue, cpu);
		struct vmap_block *vb;

		rcu_read_lock();
		list_for_each_entry_rcu(vb, &vbq->free, free_list) {
			spin_lock(&vb->lock);
			if (vb->dirty && vb->dirty != VMAP_BBMAP_BITS) {
				unsigned long va_start = vb->va->va_start;
				unsigned long s, e;

				s = va_start + (vb->dirty_min << PAGE_SHIFT);
				e = va_start + (vb->dirty_max << PAGE_SHIFT);

				start = min(s, start);
				end   = max(e, end);

				flush = 1;
			}
			spin_unlock(&vb->lock);
		}
		rcu_read_unlock();
	}

	mutex_lock(&vmap_purge_lock);
	purge_fragmented_blocks_allcpus();
	if (!__purge_vmap_area_lazy(start, end) && flush)
		flush_tlb_kernel_range(start, end);
	mutex_unlock(&vmap_purge_lock);
}

void vm_unmap_aliases(void)
{
	unsigned long start = ULONG_MAX, end = 0;
	int flush = 0;

	_vm_unmap_aliases(start, end, flush);
}

void vm_unmap_ram(const void *mem, unsigned int count)
{
	unsigned long size = (unsigned long)count << PAGE_SHIFT;
	unsigned long addr = (unsigned long)kasan_reset_tag(mem);
	struct vmap_area *va;

	might_sleep();
	BUG_ON(!addr);
	BUG_ON(addr < VMALLOC_START);
	BUG_ON(addr > VMALLOC_END);
	BUG_ON(!PAGE_ALIGNED(addr));

	kasan_poison_vmalloc(mem, size);

	if (likely(count <= VMAP_MAX_ALLOC)) {
		debug_check_no_locks_freed(mem, size);
		vb_free(addr, size);
		return;
	}

	va = find_vmap_area(addr);
	BUG_ON(!va);
	debug_check_no_locks_freed((void *)va->va_start,
				    (va->va_end - va->va_start));
	free_unmap_vmap_area(va);
}

void *vm_map_ram(struct page **pages, unsigned int count, int node)
{
	unsigned long size = (unsigned long)count << PAGE_SHIFT;
	unsigned long addr;
	void *mem;

	if (likely(count <= VMAP_MAX_ALLOC)) {
		mem = vb_alloc(size, GFP_KERNEL);
		if (IS_ERR(mem))
			return NULL;
		addr = (unsigned long)mem;
	} else {
		struct vmap_area *va;
		va = alloc_vmap_area(size, PAGE_SIZE,
				VMALLOC_START, VMALLOC_END, node, GFP_KERNEL);
		if (IS_ERR(va))
			return NULL;

		addr = va->va_start;
		mem = (void *)addr;
	}

	if (vmap_pages_range(addr, addr + size, PAGE_KERNEL,
				pages, PAGE_SHIFT) < 0) {
		vm_unmap_ram(mem, count);
		return NULL;
	}

	
	mem = kasan_unpoison_vmalloc(mem, size, KASAN_VMALLOC_PROT_NORMAL);

	return mem;
}

static struct vm_struct *vmlist __initdata;

static inline unsigned int vm_area_page_order(struct vm_struct *vm)
{
	return 0;
}

static inline void set_vm_area_page_order(struct vm_struct *vm, unsigned int order)
{
	BUG_ON(order != 0);
}

void __init vm_area_add_early(struct vm_struct *vm)
{
	struct vm_struct *tmp, **p;

	BUG_ON(vmap_initialized);
	for (p = &vmlist; (tmp = *p) != NULL; p = &tmp->next) {
		if (tmp->addr >= vm->addr) {
			BUG_ON(tmp->addr < vm->addr + vm->size);
			break;
		} else
			BUG_ON(tmp->addr + tmp->size > vm->addr);
	}
	vm->next = *p;
	*p = vm;
}

void __init vm_area_register_early(struct vm_struct *vm, size_t align)
{
	unsigned long addr = ALIGN(VMALLOC_START, align);
	struct vm_struct *cur, **p;

	BUG_ON(vmap_initialized);

	for (p = &vmlist; (cur = *p) != NULL; p = &cur->next) {
		if ((unsigned long)cur->addr - addr >= vm->size)
			break;
		addr = ALIGN((unsigned long)cur->addr + cur->size, align);
	}

	BUG_ON(addr > VMALLOC_END - vm->size);
	vm->addr = (void *)addr;
	vm->next = *p;
	*p = vm;
	kasan_populate_early_vm_area_shadow(vm->addr, vm->size);
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
	struct vmap_area *va;
	struct vm_struct *tmp;
	int i;

	
	vmap_area_cachep = KMEM_CACHE(vmap_area, SLAB_PANIC);

	for_each_possible_cpu(i) {
		struct vmap_block_queue *vbq;
		struct vfree_deferred *p;

		vbq = &per_cpu(vmap_block_queue, i);
		spin_lock_init(&vbq->lock);
		INIT_LIST_HEAD(&vbq->free);
		p = &per_cpu(vfree_deferred, i);
		init_llist_head(&p->list);
		INIT_WORK(&p->wq, free_work);
	}

	
	for (tmp = vmlist; tmp; tmp = tmp->next) {
		va = kmem_cache_zalloc(vmap_area_cachep, GFP_NOWAIT);
		if (WARN_ON_ONCE(!va))
			continue;

		va->va_start = (unsigned long)tmp->addr;
		va->va_end = va->va_start + tmp->size;
		va->vm = tmp;
		insert_vmap_area(va, &vmap_area_root, &vmap_area_list);
	}

	
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

static void setup_vmalloc_vm(struct vm_struct *vm, struct vmap_area *va,
			      unsigned long flags, const void *caller)
{
	spin_lock(&vmap_area_lock);
	setup_vmalloc_vm_locked(vm, va, flags, caller);
	spin_unlock(&vmap_area_lock);
}

static void clear_vm_uninitialized_flag(struct vm_struct *vm)
{
	
	smp_wmb();
	vm->flags &= ~VM_UNINITIALIZED;
}

static struct vm_struct *__get_vm_area_node(unsigned long size,
		unsigned long align, unsigned long shift, unsigned long flags,
		unsigned long start, unsigned long end, int node,
		gfp_t gfp_mask, const void *caller)
{
	struct vmap_area *va;
	struct vm_struct *area;
	unsigned long requested_size = size;

	BUG_ON(in_interrupt());
	size = ALIGN(size, 1ul << shift);
	if (unlikely(!size))
		return NULL;

	if (flags & VM_IOREMAP)
		align = 1ul << clamp_t(int, get_count_order_long(size),
				       PAGE_SHIFT, IOREMAP_MAX_ORDER);

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

	setup_vmalloc_vm(area, va, flags, caller);

	
	if (!(flags & VM_ALLOC))
		area->addr = kasan_unpoison_vmalloc(area->addr, requested_size,
						    KASAN_VMALLOC_PROT_NORMAL);

	return area;
}

struct vm_struct *__get_vm_area_caller(unsigned long size, unsigned long flags,
				       unsigned long start, unsigned long end,
				       const void *caller)
{
	return __get_vm_area_node(size, 1, PAGE_SHIFT, flags, start, end,
				  NUMA_NO_NODE, GFP_KERNEL, caller);
}

struct vm_struct *get_vm_area(unsigned long size, unsigned long flags)
{
	return __get_vm_area_node(size, 1, PAGE_SHIFT, flags,
				  VMALLOC_START, VMALLOC_END,
				  NUMA_NO_NODE, GFP_KERNEL,
				  __builtin_return_address(0));
}

struct vm_struct *get_vm_area_caller(unsigned long size, unsigned long flags,
				const void *caller)
{
	return __get_vm_area_node(size, 1, PAGE_SHIFT, flags,
				  VMALLOC_START, VMALLOC_END,
				  NUMA_NO_NODE, GFP_KERNEL, caller);
}

struct vm_struct *find_vm_area(const void *addr)
{
	struct vmap_area *va;

	va = find_vmap_area((unsigned long)addr);
	if (!va)
		return NULL;

	return va->vm;
}

struct vm_struct *remove_vm_area(const void *addr)
{
	struct vmap_area *va;

	might_sleep();

	spin_lock(&vmap_area_lock);
	va = __find_vmap_area((unsigned long)addr);
	if (va && va->vm) {
		struct vm_struct *vm = va->vm;

		va->vm = NULL;
		spin_unlock(&vmap_area_lock);

		kasan_free_module_shadow(vm);
		free_unmap_vmap_area(va);

		return vm;
	}

	spin_unlock(&vmap_area_lock);
	return NULL;
}

static inline void set_area_direct_map(const struct vm_struct *area,
				       int (*set_direct_map)(struct page *page))
{
	int i;

	
	for (i = 0; i < area->nr_pages; i++)
		if (page_address(area->pages[i]))
			set_direct_map(area->pages[i]);
}

static void vm_remove_mappings(struct vm_struct *area, int deallocate_pages)
{
	unsigned long start = ULONG_MAX, end = 0;
	unsigned int page_order = vm_area_page_order(area);
	int flush_reset = area->flags & VM_FLUSH_RESET_PERMS;
	int flush_dmap = 0;
	int i;

	remove_vm_area(area->addr);

	
	if (!flush_reset)
		return;

	
	if (!deallocate_pages) {
		vm_unmap_aliases();
		return;
	}

	
	for (i = 0; i < area->nr_pages; i += 1U << page_order) {
		unsigned long addr = (unsigned long)page_address(area->pages[i]);
		if (addr) {
			unsigned long page_size;

			page_size = PAGE_SIZE << page_order;
			start = min(addr, start);
			end = max(addr + page_size, end);
			flush_dmap = 1;
		}
	}

	
	set_area_direct_map(area, set_direct_map_invalid_noflush);
	_vm_unmap_aliases(start, end, flush_dmap);
	set_area_direct_map(area, set_direct_map_default_noflush);
}

static void __vunmap(const void *addr, int deallocate_pages)
{
	struct vm_struct *area;

	if (!addr)
		return;

	if (WARN(!PAGE_ALIGNED(addr), "Trying to vfree() bad address (%p)\n",
			addr))
		return;

	area = find_vm_area(addr);
	if (unlikely(!area)) {
		WARN(1, KERN_ERR "Trying to vfree() nonexistent vm area (%p)\n",
				addr);
		return;
	}

	debug_check_no_locks_freed(area->addr, get_vm_area_size(area));

	kasan_poison_vmalloc(area->addr, get_vm_area_size(area));

	vm_remove_mappings(area, deallocate_pages);

	if (deallocate_pages) {
		int i;

		for (i = 0; i < area->nr_pages; i++) {
			struct page *page = area->pages[i];

			BUG_ON(!page);
			mod_memcg_page_state(page, MEMCG_VMALLOC, -1);
			
			__free_pages(page, 0);
			cond_resched();
		}
		atomic_long_sub(area->nr_pages, &nr_vmalloc_pages);

		kvfree(area->pages);
	}

	kfree(area);
}

static inline void __vfree_deferred(const void *addr)
{
	
	struct vfree_deferred *p = raw_cpu_ptr(&vfree_deferred);

	if (llist_add((struct llist_node *)addr, &p->list))
		schedule_work(&p->wq);
}

void vfree_atomic(const void *addr)
{
	BUG_ON(in_nmi());

	kmemleak_free(addr);

	if (!addr)
		return;
	__vfree_deferred(addr);
}

static void __vfree(const void *addr)
{
	if (unlikely(in_interrupt()))
		__vfree_deferred(addr);
	else
		__vunmap(addr, 1);
}

void vfree(const void *addr)
{
	BUG_ON(in_nmi());

	kmemleak_free(addr);

	might_sleep_if(!in_interrupt());

	if (!addr)
		return;

	__vfree(addr);
}

void vunmap(const void *addr)
{
	BUG_ON(in_interrupt());
	might_sleep();
	if (addr)
		__vunmap(addr, 0);
}

void *vmap(struct page **pages, unsigned int count,
	   unsigned long flags, pgprot_t prot)
{
	struct vm_struct *area;
	unsigned long addr;
	unsigned long size;		

	might_sleep();

	
	if (WARN_ON_ONCE(flags & VM_NO_GUARD))
		flags &= ~VM_NO_GUARD;

	if (count > totalram_pages())
		return NULL;

	size = (unsigned long)count << PAGE_SHIFT;
	area = get_vm_area_caller(size, flags, __builtin_return_address(0));
	if (!area)
		return NULL;

	addr = (unsigned long)area->addr;
	if (vmap_pages_range(addr, addr + size, pgprot_nx(prot),
				pages, PAGE_SHIFT) < 0) {
		vunmap(area->addr);
		return NULL;
	}

	if (flags & VM_MAP_PUT_PAGES) {
		area->pages = pages;
		area->nr_pages = count;
	}
	return area->addr;
}

static inline unsigned int
vm_area_alloc_pages(gfp_t gfp, int nid,
		unsigned int order, unsigned int nr_pages, struct page **pages)
{
	unsigned int nr_allocated = 0;
	struct page *page;
	int i;

	
	if (!order) {
		gfp_t bulk_gfp = gfp & ~__GFP_NOFAIL;

		while (nr_allocated < nr_pages) {
			unsigned int nr, nr_pages_request;

			
			nr_pages_request = min(100U, nr_pages - nr_allocated);

			
			if (IS_ENABLED(CONFIG_NUMA) && nid == NUMA_NO_NODE)
				nr = alloc_pages_bulk_array_mempolicy(bulk_gfp,
							nr_pages_request,
							pages + nr_allocated);

			else
				nr = alloc_pages_bulk_array_node(bulk_gfp, nid,
							nr_pages_request,
							pages + nr_allocated);

			nr_allocated += nr;
			cond_resched();

			
			if (nr != nr_pages_request)
				break;
		}
	}

	

	while (nr_allocated < nr_pages) {
		if (fatal_signal_pending(current))
			break;

		if (nid == NUMA_NO_NODE)
			page = alloc_pages(gfp, order);
		else
			page = alloc_pages_node(nid, gfp, order);
		if (unlikely(!page))
			break;
		
		if (order)
			split_page(page, order);

		
		for (i = 0; i < (1U << order); i++)
			pages[nr_allocated + i] = page + i;

		cond_resched();
		nr_allocated += 1U << order;
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
	unsigned long size = get_vm_area_size(area);
	unsigned long array_size;
	unsigned int nr_small_pages = size >> PAGE_SHIFT;
	unsigned int page_order;
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
		warn_alloc(gfp_mask, NULL,
			"vmalloc error: size %lu, failed to allocated page array size %lu",
			nr_small_pages * PAGE_SIZE, array_size);
		free_vm_area(area);
		return NULL;
	}

	set_vm_area_page_order(area, page_shift - PAGE_SHIFT);
	page_order = vm_area_page_order(area);

	area->nr_pages = vm_area_alloc_pages(gfp_mask | __GFP_NOWARN,
		node, page_order, nr_small_pages, area->pages);

	atomic_long_add(area->nr_pages, &nr_vmalloc_pages);
	if (gfp_mask & __GFP_ACCOUNT) {
		int i;

		for (i = 0; i < area->nr_pages; i++)
			mod_memcg_page_state(area->pages[i], MEMCG_VMALLOC, 1);
	}

	
	if (area->nr_pages != nr_small_pages) {
		warn_alloc(gfp_mask, NULL,
			"vmalloc error: size %lu, page order %u, failed to allocate pages",
			area->nr_pages * PAGE_SIZE, page_order);
		goto fail;
	}

	
	if ((gfp_mask & (__GFP_FS | __GFP_IO)) == __GFP_IO)
		flags = memalloc_nofs_save();
	else if ((gfp_mask & (__GFP_FS | __GFP_IO)) == 0)
		flags = memalloc_noio_save();

	do {
		ret = vmap_pages_range(addr, addr + size, prot, area->pages,
			page_shift);
		if (nofail && (ret < 0))
			schedule_timeout_uninterruptible(1);
	} while (nofail && (ret < 0));

	if ((gfp_mask & (__GFP_FS | __GFP_IO)) == __GFP_IO)
		memalloc_nofs_restore(flags);
	else if ((gfp_mask & (__GFP_FS | __GFP_IO)) == 0)
		memalloc_noio_restore(flags);

	if (ret < 0) {
		warn_alloc(gfp_mask, NULL,
			"vmalloc error: size %lu, failed to map pages",
			area->nr_pages * PAGE_SIZE);
		goto fail;
	}

	return area->addr;

fail:
	__vfree(area->addr);
	return NULL;
}

void *__vmalloc_node_range(unsigned long size, unsigned long align,
			unsigned long start, unsigned long end, gfp_t gfp_mask,
			pgprot_t prot, unsigned long vm_flags, int node,
			const void *caller)
{
	struct vm_struct *area;
	void *ret;
	kasan_vmalloc_flags_t kasan_flags = KASAN_VMALLOC_NONE;
	unsigned long real_size = size;
	unsigned long real_align = align;
	unsigned int shift = PAGE_SHIFT;

	if (WARN_ON_ONCE(!size))
		return NULL;

	if ((size >> PAGE_SHIFT) > totalram_pages()) {
		warn_alloc(gfp_mask, NULL,
			"vmalloc error: size %lu, exceeds total pages",
			real_size);
		return NULL;
	}

	if (vmap_allow_huge && (vm_flags & VM_ALLOW_HUGE_VMAP)) {
		unsigned long size_per_node;

		

		size_per_node = size;
		if (node == NUMA_NO_NODE)
			size_per_node /= num_online_nodes();
		if (arch_vmap_pmd_supported(prot) && size_per_node >= PMD_SIZE)
			shift = PMD_SHIFT;
		else
			shift = arch_vmap_pte_supported_shift(size_per_node);

		align = max(real_align, 1UL << shift);
		size = ALIGN(real_size, 1UL << shift);
	}

again:
	area = __get_vm_area_node(real_size, align, shift, VM_ALLOC |
				  VM_UNINITIALIZED | vm_flags, start, end, node,
				  gfp_mask, caller);
	if (!area) {
		bool nofail = gfp_mask & __GFP_NOFAIL;
		warn_alloc(gfp_mask, NULL,
			"vmalloc error: size %lu, vm_struct allocation failed%s",
			real_size, (nofail) ? ". Retrying." : "");
		if (nofail) {
			schedule_timeout_uninterruptible(1);
			goto again;
		}
		goto fail;
	}

	
	if (pgprot_val(prot) == pgprot_val(PAGE_KERNEL)) {
		if (kasan_hw_tags_enabled()) {
			
			prot = arch_vmap_pgprot_tagged(prot);

			
			gfp_mask |= __GFP_SKIP_KASAN_UNPOISON | __GFP_SKIP_ZERO;
		}

		
		kasan_flags |= KASAN_VMALLOC_PROT_NORMAL;
	}

	
	ret = __vmalloc_area_node(area, gfp_mask, prot, shift, node);
	if (!ret)
		goto fail;

	
	kasan_flags |= KASAN_VMALLOC_VM_ALLOC;
	if (!want_init_on_free() && want_init_on_alloc(gfp_mask))
		kasan_flags |= KASAN_VMALLOC_INIT;
	
	area->addr = kasan_unpoison_vmalloc(area->addr, real_size, kasan_flags);

	
	clear_vm_uninitialized_flag(area);

	size = PAGE_ALIGN(size);
	if (!(vm_flags & VM_DEFER_KMEMLEAK))
		kmemleak_vmalloc(area, size, gfp_mask);

	return area->addr;

fail:
	if (shift > PAGE_SHIFT) {
		shift = PAGE_SHIFT;
		align = real_align;
		size = real_size;
		goto again;
	}

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

void *vmalloc(unsigned long size)
{
	return __vmalloc_node(size, 1, GFP_KERNEL, NUMA_NO_NODE,
				__builtin_return_address(0));
}

void *vmalloc_huge(unsigned long size, gfp_t gfp_mask)
{
	return __vmalloc_node_range(size, 1, VMALLOC_START, VMALLOC_END,
				    gfp_mask, PAGE_KERNEL, VM_ALLOW_HUGE_VMAP,
				    NUMA_NO_NODE, __builtin_return_address(0));
}

void *vzalloc(unsigned long size)
{
	return __vmalloc_node(size, 1, GFP_KERNEL | __GFP_ZERO, NUMA_NO_NODE,
				__builtin_return_address(0));
}

void *vmalloc_user(unsigned long size)
{
	return __vmalloc_node_range(size, SHMLBA,  VMALLOC_START, VMALLOC_END,
				    GFP_KERNEL | __GFP_ZERO, PAGE_KERNEL,
				    VM_USERMAP, NUMA_NO_NODE,
				    __builtin_return_address(0));
}

void *vmalloc_node(unsigned long size, int node)
{
	return __vmalloc_node(size, 1, GFP_KERNEL, node,
			__builtin_return_address(0));
}

void *vzalloc_node(unsigned long size, int node)
{
	return __vmalloc_node(size, 1, GFP_KERNEL | __GFP_ZERO, node,
				__builtin_return_address(0));
}

#define GFP_VMALLOC32 (GFP_DMA32 | GFP_KERNEL)

void *vmalloc_32(unsigned long size)
{
	return __vmalloc_node(size, 1, GFP_VMALLOC32, NUMA_NO_NODE,
			__builtin_return_address(0));
}

void *vmalloc_32_user(unsigned long size)
{
	return __vmalloc_node_range(size, SHMLBA,  VMALLOC_START, VMALLOC_END,
				    GFP_VMALLOC32 | __GFP_ZERO, PAGE_KERNEL,
				    VM_USERMAP, NUMA_NO_NODE,
				    __builtin_return_address(0));
}

static int aligned_vread(char *buf, char *addr, unsigned long count)
{
	struct page *p;
	int copied = 0;

	while (count) {
		unsigned long offset, length;

		offset = offset_in_page(addr);
		length = PAGE_SIZE - offset;
		if (length > count)
			length = count;
		p = vmalloc_to_page(addr);
		
		if (p) {
			
			void *map = kmap_atomic(p);
			memcpy(buf, map + offset, length);
			kunmap_atomic(map);
		} else
			memset(buf, 0, length);

		addr += length;
		buf += length;
		copied += length;
		count -= length;
	}
	return copied;
}

long vread(char *buf, char *addr, unsigned long count)
{
	struct vmap_area *va;
	struct vm_struct *vm;
	char *vaddr, *buf_start = buf;
	unsigned long buflen = count;
	unsigned long n;

	addr = kasan_reset_tag(addr);

	
	if ((unsigned long) addr + count < count)
		count = -(unsigned long) addr;

	spin_lock(&vmap_area_lock);
	va = find_vmap_area_exceed_addr((unsigned long)addr);
	if (!va)
		goto finished;

	
	if ((unsigned long)addr + count <= va->va_start)
		goto finished;

	list_for_each_entry_from(va, &vmap_area_list, list) {
		if (!count)
			break;

		if (!va->vm)
			continue;

		vm = va->vm;
		vaddr = (char *) vm->addr;
		if (addr >= vaddr + get_vm_area_size(vm))
			continue;
		while (addr < vaddr) {
			if (count == 0)
				goto finished;
			*buf = '\0';
			buf++;
			addr++;
			count--;
		}
		n = vaddr + get_vm_area_size(vm) - addr;
		if (n > count)
			n = count;
		if (!(vm->flags & VM_IOREMAP))
			aligned_vread(buf, addr, n);
		else 
			memset(buf, 0, n);
		buf += n;
		addr += n;
		count -= n;
	}
finished:
	spin_unlock(&vmap_area_lock);

	if (buf == buf_start)
		return 0;
	
	if (buf != buf_start + buflen)
		memset(buf, 0, buflen - (buf - buf_start));

	return buflen;
}

int remap_vmalloc_range_partial(struct vm_area_struct *vma, unsigned long uaddr,
				void *kaddr, unsigned long pgoff,
				unsigned long size)
{
	struct vm_struct *area;
	unsigned long off;
	unsigned long end_index;

	if (check_shl_overflow(pgoff, PAGE_SHIFT, &off))
		return -EINVAL;

	size = PAGE_ALIGN(size);

	if (!PAGE_ALIGNED(uaddr) || !PAGE_ALIGNED(kaddr))
		return -EINVAL;

	area = find_vm_area(kaddr);
	if (!area)
		return -EINVAL;

	if (!(area->flags & (VM_USERMAP | VM_DMA_COHERENT)))
		return -EINVAL;

	if (check_add_overflow(size, off, &end_index) ||
	    end_index > get_vm_area_size(area))
		return -EINVAL;
	kaddr += off;

	do {
		struct page *page = vmalloc_to_page(kaddr);
		int ret;

		ret = vm_insert_page(vma, uaddr, page);
		if (ret)
			return ret;

		uaddr += PAGE_SIZE;
		kaddr += PAGE_SIZE;
		size -= PAGE_SIZE;
	} while (size > 0);

	vma->vm_flags |= VM_DONTEXPAND | VM_DONTDUMP;

	return 0;
}

int remap_vmalloc_range(struct vm_area_struct *vma, void *addr,
						unsigned long pgoff)
{
	return remap_vmalloc_range_partial(vma, vma->vm_start,
					   addr, pgoff,
					   vma->vm_end - vma->vm_start);
}

void free_vm_area(struct vm_struct *area)
{
	struct vm_struct *ret;
	ret = remove_vm_area(area->addr);
	BUG_ON(ret != area);
	kfree(area);
}

