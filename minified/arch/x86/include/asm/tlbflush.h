 
#ifndef _ASM_X86_TLBFLUSH_H
#define _ASM_X86_TLBFLUSH_H

#include <linux/mm.h>
#include <linux/sched.h>

#include <asm/processor.h>
#include <asm/cpufeature.h>
#include <asm/special_insns.h>
#include <asm/processor-flags.h>

/* Inlined from asm/invpcid.h */
static inline void __invpcid(unsigned long pcid, unsigned long addr, unsigned long type)
{
	struct { u64 d[2]; } desc = { { pcid, addr } };
	asm volatile("invpcid %[desc], %[type]" :: [desc] "m" (desc), [type] "r" (type) : "memory");
}
#define INVPCID_TYPE_INDIV_ADDR		0
/* INVPCID_TYPE_SINGLE_CTXT removed - never used */
#define INVPCID_TYPE_ALL_INCL_GLOBAL	2
/* INVPCID_TYPE_ALL_NON_GLOBAL removed - never used */
static inline void invpcid_flush_one(unsigned long pcid, unsigned long addr) { __invpcid(pcid, addr, INVPCID_TYPE_INDIV_ADDR); }
static inline void invpcid_flush_all(void) { __invpcid(0, 0, INVPCID_TYPE_ALL_INCL_GLOBAL); }

void __flush_tlb_all(void);

#define TLB_FLUSH_ALL	-1UL

void cr4_update_irqsoff(unsigned long set, unsigned long clear);
unsigned long cr4_read_shadow(void);

 
static inline void cr4_set_bits_irqsoff(unsigned long mask)
{
	cr4_update_irqsoff(mask, 0);
}

 
static inline void cr4_clear_bits_irqsoff(unsigned long mask)
{
	cr4_update_irqsoff(0, mask);
}

 
static inline void cr4_set_bits(unsigned long mask)
{
	unsigned long flags;

	local_irq_save(flags);
	cr4_set_bits_irqsoff(mask);
	local_irq_restore(flags);
}

 
static inline void cr4_clear_bits(unsigned long mask)
{
	unsigned long flags;

	local_irq_save(flags);
	cr4_clear_bits_irqsoff(mask);
	local_irq_restore(flags);
}

#ifndef MODULE
 
#define TLB_NR_DYN_ASIDS	6

struct tlb_context {
	u64 ctx_id;
	u64 tlb_gen;
};

struct tlb_state {
	 
	struct mm_struct *loaded_mm;

#define LOADED_MM_SWITCHING ((struct mm_struct *)1UL)

	 
	union {
		struct mm_struct	*last_user_mm;
		unsigned long		last_user_mm_spec;
	};

	u16 loaded_mm_asid;
	u16 next_asid;

	 
	bool invalidate_other;

	 
	unsigned short user_pcid_flush_mask;

	 
	unsigned long cr4;

	 
	struct tlb_context ctxs[TLB_NR_DYN_ASIDS];
};
DECLARE_PER_CPU_ALIGNED(struct tlb_state, cpu_tlbstate);

struct tlb_state_shared {
	 
	bool is_lazy;
};
DECLARE_PER_CPU_SHARED_ALIGNED(struct tlb_state_shared, cpu_tlbstate_shared);

/* nmi_uaccess_okay removed - only caller was copy_from_user_nmi */

 
static inline void cr4_init_shadow(void)
{
	this_cpu_write(cpu_tlbstate.cr4, __read_cr4());
}

extern unsigned long mmu_cr4_features;
extern u32 *trampoline_cr4_features;

extern void initialize_tlbstate_and_flush(void);

 
struct flush_tlb_info {
	 
	struct mm_struct	*mm;
	unsigned long		start;
	unsigned long		end;
	u64			new_tlb_gen;
	unsigned int		initiating_cpu;
	u8			stride_shift;
	u8			freed_tables;
};

void flush_tlb_local(void);
void flush_tlb_one_user(unsigned long addr);
void flush_tlb_one_kernel(unsigned long addr);
void flush_tlb_multi(const struct cpumask *cpumask,
		      const struct flush_tlb_info *info);


#define flush_tlb_mm(mm)						\
		flush_tlb_mm_range(mm, 0UL, TLB_FLUSH_ALL, 0UL, true)

/* huge_page_shift(hstate_vma(vma)) always returns PAGE_SHIFT since
 * hstate_vma returns NULL and huge_page_shift(NULL) = PAGE_SHIFT */
#define flush_tlb_range(vma, start, end)				\
	flush_tlb_mm_range((vma)->vm_mm, start, end, PAGE_SHIFT, false)

/* flush_tlb_all removed - never called */
extern void flush_tlb_mm_range(struct mm_struct *mm, unsigned long start,
				unsigned long end, unsigned int stride_shift,
				bool freed_tables);
static inline u64 inc_mm_tlb_gen(struct mm_struct *mm)
{
	 
	return atomic64_inc_return(&mm->context.tlb_gen);
}

/* pte_flags_need_flush, pte_needs_flush, huge_pmd_needs_flush removed - never called */

#endif  

static inline void __native_tlb_flush_global(unsigned long cr4)
{
	native_write_cr4(cr4 ^ X86_CR4_PGE);
	native_write_cr4(cr4);
}
#endif  
