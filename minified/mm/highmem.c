// SPDX-License-Identifier: GPL-2.0
/*
 * High memory handling common code and variables.
 *
 * (C) 1999 Andrea Arcangeli, SuSE GmbH, andrea@suse.de
 *          Gerhard Wichert, Siemens AG, Gerhard.Wichert@pdb.siemens.de
 *
 *
 * Redesigned the x86 32-bit VM architecture to deal with
 * 64-bit physical space. With current x86 CPUs this
 * means up to 64 Gigabytes physical RAM.
 *
 * Rewrote high memory support to move the page cache into
 * high memory. Implemented permanent (schedulable) kmaps
 * based on Linus' idea.
 *
 * Copyright (C) 1999 Ingo Molnar <mingo@redhat.com>
 */

#include <linux/mm.h>
#include <linux/export.h>
#include <linux/swap.h>
#include <linux/bio.h>
#include <linux/pagemap.h>
#include <linux/mempool.h>
#include <linux/init.h>
#include <linux/hash.h>
#include <linux/highmem.h>
#include <linux/kgdb.h>
#include <asm/tlbflush.h>
#include <linux/vmalloc.h>

/*
 * Virtual_count is not a pure "count".
 *  0 means that it is not mapped, and has not been mapped
 *    since a TLB flush - it is usable.
 *  1 means that there are no users, but it has been mapped
 *    since the last TLB flush - so we can't use it.
 *  n means that there are (n-1) current users of it.
 */


#include <asm/kmap_size.h>

/*
 * With DEBUG_KMAP_LOCAL the stack depth is doubled and every second
 * slot is unused which acts as a guard page
 */
# define KM_INCR	1

static inline int kmap_local_idx_push(void)
{
	WARN_ON_ONCE(in_hardirq() && !irqs_disabled());
	current->kmap_ctrl.idx += KM_INCR;
	BUG_ON(current->kmap_ctrl.idx >= KM_MAX_IDX);
	return current->kmap_ctrl.idx - 1;
}

static inline int kmap_local_idx(void)
{
	return current->kmap_ctrl.idx - 1;
}

static inline void kmap_local_idx_pop(void)
{
	current->kmap_ctrl.idx -= KM_INCR;
	BUG_ON(current->kmap_ctrl.idx < 0);
}

#ifndef arch_kmap_local_post_map
# define arch_kmap_local_post_map(vaddr, pteval)	do { } while (0)
#endif

#ifndef arch_kmap_local_pre_unmap
# define arch_kmap_local_pre_unmap(vaddr)		do { } while (0)
#endif

#ifndef arch_kmap_local_post_unmap
# define arch_kmap_local_post_unmap(vaddr)		do { } while (0)
#endif

#ifndef arch_kmap_local_map_idx
#define arch_kmap_local_map_idx(idx, pfn)	kmap_local_calc_idx(idx)
#endif

#ifndef arch_kmap_local_unmap_idx
#define arch_kmap_local_unmap_idx(idx, vaddr)	kmap_local_calc_idx(idx)
#endif

#ifndef arch_kmap_local_high_get
static inline void *arch_kmap_local_high_get(struct page *page)
{
	return NULL;
}
#endif

#ifndef arch_kmap_local_set_pte
#define arch_kmap_local_set_pte(mm, vaddr, ptep, ptev)	\
	set_pte_at(mm, vaddr, ptep, ptev)
#endif

/* Unmap a local mapping which was obtained by kmap_high_get() */
static inline bool kmap_high_unmap_local(unsigned long vaddr)
{
#ifdef ARCH_NEEDS_KMAP_HIGH_GET
	if (vaddr >= PKMAP_ADDR(0) && vaddr < PKMAP_ADDR(LAST_PKMAP)) {
		kunmap_high(pte_page(pkmap_page_table[PKMAP_NR(vaddr)]));
		return true;
	}
#endif
	return false;
}

static inline int kmap_local_calc_idx(int idx)
{
	return idx + KM_MAX_IDX * smp_processor_id();
}

static pte_t *__kmap_pte;

static pte_t *kmap_get_pte(unsigned long vaddr, int idx)
{
	if (IS_ENABLED(CONFIG_KMAP_LOCAL_NON_LINEAR_PTE_ARRAY))
		/*
		 * Set by the arch if __kmap_pte[-idx] does not produce
		 * the correct entry.
		 */
		return virt_to_kpte(vaddr);
	if (!__kmap_pte)
		__kmap_pte = virt_to_kpte(__fix_to_virt(FIX_KMAP_BEGIN));
	return &__kmap_pte[-idx];
}

void *__kmap_local_pfn_prot(unsigned long pfn, pgprot_t prot)
{
	pte_t pteval, *kmap_pte;
	unsigned long vaddr;
	int idx;

	/*
	 * Disable migration so resulting virtual address is stable
	 * across preemption.
	 */
	migrate_disable();
	preempt_disable();
	idx = arch_kmap_local_map_idx(kmap_local_idx_push(), pfn);
	vaddr = __fix_to_virt(FIX_KMAP_BEGIN + idx);
	kmap_pte = kmap_get_pte(vaddr, idx);
	BUG_ON(!pte_none(*kmap_pte));
	pteval = pfn_pte(pfn, prot);
	arch_kmap_local_set_pte(&init_mm, vaddr, kmap_pte, pteval);
	arch_kmap_local_post_map(vaddr, pteval);
	current->kmap_ctrl.pteval[kmap_local_idx()] = pteval;
	preempt_enable();

	return (void *)vaddr;
}
EXPORT_SYMBOL_GPL(__kmap_local_pfn_prot);

void *__kmap_local_page_prot(struct page *page, pgprot_t prot)
{
	void *kmap;

	/*
	 * To broaden the usage of the actual kmap_local() machinery always map
	 * pages when debugging is enabled and the architecture has no problems
	 * with alias mappings.
	 */
	if (!IS_ENABLED(CONFIG_DEBUG_KMAP_LOCAL_FORCE_MAP) && !PageHighMem(page))
		return page_address(page);

	/* Try kmap_high_get() if architecture has it enabled */
	kmap = arch_kmap_local_high_get(page);
	if (kmap)
		return kmap;

	return __kmap_local_pfn_prot(page_to_pfn(page), prot);
}
EXPORT_SYMBOL(__kmap_local_page_prot);

void kunmap_local_indexed(void *vaddr)
{
	unsigned long addr = (unsigned long) vaddr & PAGE_MASK;
	pte_t *kmap_pte;
	int idx;

	if (addr < __fix_to_virt(FIX_KMAP_END) ||
	    addr > __fix_to_virt(FIX_KMAP_BEGIN)) {
		if (IS_ENABLED(CONFIG_DEBUG_KMAP_LOCAL_FORCE_MAP)) {
			/* This _should_ never happen! See above. */
			WARN_ON_ONCE(1);
			return;
		}
		/*
		 * Handle mappings which were obtained by kmap_high_get()
		 * first as the virtual address of such mappings is below
		 * PAGE_OFFSET. Warn for all other addresses which are in
		 * the user space part of the virtual address space.
		 */
		if (!kmap_high_unmap_local(addr))
			WARN_ON_ONCE(addr < PAGE_OFFSET);
		return;
	}

	preempt_disable();
	idx = arch_kmap_local_unmap_idx(kmap_local_idx(), addr);
	WARN_ON_ONCE(addr != __fix_to_virt(FIX_KMAP_BEGIN + idx));

	kmap_pte = kmap_get_pte(addr, idx);
	arch_kmap_local_pre_unmap(addr);
	pte_clear(&init_mm, addr, kmap_pte);
	arch_kmap_local_post_unmap(addr);
	current->kmap_ctrl.pteval[kmap_local_idx()] = __pte(0);
	kmap_local_idx_pop();
	preempt_enable();
	migrate_enable();
}
EXPORT_SYMBOL(kunmap_local_indexed);

/*
 * Invoked before switch_to(). This is safe even when during or after
 * clearing the maps an interrupt which needs a kmap_local happens because
 * the task::kmap_ctrl.idx is not modified by the unmapping code so a
 * nested kmap_local will use the next unused index and restore the index
 * on unmap. The already cleared kmaps of the outgoing task are irrelevant
 * because the interrupt context does not know about them. The same applies
 * when scheduling back in for an interrupt which happens before the
 * restore is complete.
 */
void __kmap_local_sched_out(void)
{
	struct task_struct *tsk = current;
	pte_t *kmap_pte;
	int i;

	/* Clear kmaps */
	for (i = 0; i < tsk->kmap_ctrl.idx; i++) {
		pte_t pteval = tsk->kmap_ctrl.pteval[i];
		unsigned long addr;
		int idx;

		/* With debug all even slots are unmapped and act as guard */
		if (IS_ENABLED(CONFIG_DEBUG_KMAP_LOCAL) && !(i & 0x01)) {
			WARN_ON_ONCE(pte_val(pteval) != 0);
			continue;
		}
		if (WARN_ON_ONCE(pte_none(pteval)))
			continue;

		/*
		 * This is a horrible hack for XTENSA to calculate the
		 * coloured PTE index. Uses the PFN encoded into the pteval
		 * and the map index calculation because the actual mapped
		 * virtual address is not stored in task::kmap_ctrl.
		 * For any sane architecture this is optimized out.
		 */
		idx = arch_kmap_local_map_idx(i, pte_pfn(pteval));

		addr = __fix_to_virt(FIX_KMAP_BEGIN + idx);
		kmap_pte = kmap_get_pte(addr, idx);
		arch_kmap_local_pre_unmap(addr);
		pte_clear(&init_mm, addr, kmap_pte);
		arch_kmap_local_post_unmap(addr);
	}
}

void __kmap_local_sched_in(void)
{
	struct task_struct *tsk = current;
	pte_t *kmap_pte;
	int i;

	/* Restore kmaps */
	for (i = 0; i < tsk->kmap_ctrl.idx; i++) {
		pte_t pteval = tsk->kmap_ctrl.pteval[i];
		unsigned long addr;
		int idx;

		/* With debug all even slots are unmapped and act as guard */
		if (IS_ENABLED(CONFIG_DEBUG_KMAP_LOCAL) && !(i & 0x01)) {
			WARN_ON_ONCE(pte_val(pteval) != 0);
			continue;
		}
		if (WARN_ON_ONCE(pte_none(pteval)))
			continue;

		/* See comment in __kmap_local_sched_out() */
		idx = arch_kmap_local_map_idx(i, pte_pfn(pteval));
		addr = __fix_to_virt(FIX_KMAP_BEGIN + idx);
		kmap_pte = kmap_get_pte(addr, idx);
		set_pte_at(&init_mm, addr, kmap_pte, pteval);
		arch_kmap_local_post_map(addr, pteval);
	}
}

void kmap_local_fork(struct task_struct *tsk)
{
	if (WARN_ON_ONCE(tsk->kmap_ctrl.idx))
		memset(&tsk->kmap_ctrl, 0, sizeof(tsk->kmap_ctrl));
}


#if defined(HASHED_PAGE_VIRTUAL)

#define PA_HASH_ORDER	7

/*
 * Describes one page->virtual association
 */
struct page_address_map {
	struct page *page;
	void *virtual;
	struct list_head list;
};

static struct page_address_map page_address_maps[LAST_PKMAP];

/*
 * Hash table bucket
 */
static struct page_address_slot {
	struct list_head lh;			/* List of page_address_maps */
	spinlock_t lock;			/* Protect this bucket's list */
} ____cacheline_aligned_in_smp page_address_htable[1<<PA_HASH_ORDER];

static struct page_address_slot *page_slot(const struct page *page)
{
	return &page_address_htable[hash_ptr(page, PA_HASH_ORDER)];
}

/**
 * page_address - get the mapped virtual address of a page
 * @page: &struct page to get the virtual address of
 *
 * Returns the page's virtual address.
 */
void *page_address(const struct page *page)
{
	unsigned long flags;
	void *ret;
	struct page_address_slot *pas;

	if (!PageHighMem(page))
		return lowmem_page_address(page);

	pas = page_slot(page);
	ret = NULL;
	spin_lock_irqsave(&pas->lock, flags);
	if (!list_empty(&pas->lh)) {
		struct page_address_map *pam;

		list_for_each_entry(pam, &pas->lh, list) {
			if (pam->page == page) {
				ret = pam->virtual;
				break;
			}
		}
	}

	spin_unlock_irqrestore(&pas->lock, flags);
	return ret;
}
EXPORT_SYMBOL(page_address);

/**
 * set_page_address - set a page's virtual address
 * @page: &struct page to set
 * @virtual: virtual address to use
 */
void set_page_address(struct page *page, void *virtual)
{
	unsigned long flags;
	struct page_address_slot *pas;
	struct page_address_map *pam;

	BUG_ON(!PageHighMem(page));

	pas = page_slot(page);
	if (virtual) {		/* Add */
		pam = &page_address_maps[PKMAP_NR((unsigned long)virtual)];
		pam->page = page;
		pam->virtual = virtual;

		spin_lock_irqsave(&pas->lock, flags);
		list_add_tail(&pam->list, &pas->lh);
		spin_unlock_irqrestore(&pas->lock, flags);
	} else {		/* Remove */
		spin_lock_irqsave(&pas->lock, flags);
		list_for_each_entry(pam, &pas->lh, list) {
			if (pam->page == page) {
				list_del(&pam->list);
				break;
			}
		}
		spin_unlock_irqrestore(&pas->lock, flags);
	}

	return;
}

void __init page_address_init(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(page_address_htable); i++) {
		INIT_LIST_HEAD(&page_address_htable[i].lh);
		spin_lock_init(&page_address_htable[i].lock);
	}
}

#endif	/* defined(HASHED_PAGE_VIRTUAL) */
