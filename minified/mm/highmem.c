
#include <linux/mm.h>
/* linux/swap.h removed - no swap features used */
#include <linux/pagemap.h>
#include <linux/init.h>
#include <linux/hash.h>
#include <linux/highmem.h>
#include <asm/tlbflush.h>
#include <linux/vmalloc.h>

#include <asm/kmap_size.h>

/* KM_INCR removed - never used */

#ifndef arch_kmap_local_post_map
#define arch_kmap_local_post_map(vaddr, pteval) \
	do {                                    \
	} while (0)
#endif

#ifndef arch_kmap_local_pre_unmap
#define arch_kmap_local_pre_unmap(vaddr) \
	do {                             \
	} while (0)
#endif

#ifndef arch_kmap_local_post_unmap
#define arch_kmap_local_post_unmap(vaddr) \
	do {                              \
	} while (0)
#endif

#ifndef arch_kmap_local_map_idx
#define arch_kmap_local_map_idx(idx, pfn) kmap_local_calc_idx(idx)
#endif

#ifndef arch_kmap_local_unmap_idx
#define arch_kmap_local_unmap_idx(idx, vaddr) kmap_local_calc_idx(idx)
#endif

/* arch_kmap_local_high_get stub removed - never called */

#ifndef arch_kmap_local_set_pte
#define arch_kmap_local_set_pte(mm, vaddr, ptep, ptev) \
	set_pte_at(mm, vaddr, ptep, ptev)
#endif

static inline int kmap_local_calc_idx(int idx)
{
	return idx + KM_MAX_IDX * smp_processor_id();
}

static pte_t *__kmap_pte;

static pte_t *kmap_get_pte(unsigned long vaddr, int idx)
{
	if (!__kmap_pte)
		__kmap_pte = virt_to_kpte(__fix_to_virt(FIX_KMAP_BEGIN));
	return &__kmap_pte[-idx];
}

/* __kmap_local_pfn_prot, __kmap_local_page_prot removed - never called */

void __kmap_local_sched_out(void)
{
	struct task_struct *tsk = current;
	pte_t *kmap_pte;
	int i;

	for (i = 0; i < tsk->kmap_ctrl.idx; i++) {
		pte_t pteval = tsk->kmap_ctrl.pteval[i];
		unsigned long addr;
		int idx;

		if (WARN_ON_ONCE(pte_none(pteval)))
			continue;

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

	for (i = 0; i < tsk->kmap_ctrl.idx; i++) {
		pte_t pteval = tsk->kmap_ctrl.pteval[i];
		unsigned long addr;
		int idx;

		if (WARN_ON_ONCE(pte_none(pteval)))
			continue;

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

/* HASHED_PAGE_VIRTUAL not defined - page address hash table removed (~88 LOC) */
