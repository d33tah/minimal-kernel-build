
#include <linux/mm.h>
#include <linux/export.h>
#include <linux/swap.h>
#include <linux/pagemap.h>
#include <linux/init.h>
#include <linux/highmem.h>
#include <asm/tlbflush.h>
#include <linux/vmalloc.h>



#include <asm/kmap_size.h>

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



static inline int kmap_local_calc_idx(int idx)
{
	return idx + KM_MAX_IDX * smp_processor_id();
}

static pte_t *__kmap_pte;

static pte_t *kmap_get_pte(unsigned long vaddr, int idx)
{
	if (IS_ENABLED(CONFIG_KMAP_LOCAL_NON_LINEAR_PTE_ARRAY))
		 
		return virt_to_kpte(vaddr);
	if (!__kmap_pte)
		__kmap_pte = virt_to_kpte(__fix_to_virt(FIX_KMAP_BEGIN));
	return &__kmap_pte[-idx];
}

void __kmap_local_sched_out(void)
{
	struct task_struct *tsk = current;
	pte_t *kmap_pte;
	int i;

	 
	for (i = 0; i < tsk->kmap_ctrl.idx; i++) {
		pte_t pteval = tsk->kmap_ctrl.pteval[i];
		unsigned long addr;
		int idx;

		 
		if (IS_ENABLED(CONFIG_DEBUG_KMAP_LOCAL) && !(i & 0x01)) {
			WARN_ON_ONCE(pte_val(pteval) != 0);
			continue;
		}
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

		 
		if (IS_ENABLED(CONFIG_DEBUG_KMAP_LOCAL) && !(i & 0x01)) {
			WARN_ON_ONCE(pte_val(pteval) != 0);
			continue;
		}
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
