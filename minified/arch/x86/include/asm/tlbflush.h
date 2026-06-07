 
#ifndef _ASM_X86_TLBFLUSH_H
#define _ASM_X86_TLBFLUSH_H

#include <linux/mm.h>
#include <linux/sched.h>

#include <asm/processor.h>
#include <asm/cpufeature.h>
#include <asm/special_insns.h>
#include <asm/processor-flags.h>

void __flush_tlb_all(void);

#define TLB_FLUSH_ALL	-1UL

void cr4_update_irqsoff(unsigned long set, unsigned long clear);

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

static inline void cr4_init_shadow(void)
{
	this_cpu_write(cpu_tlbstate.cr4, __read_cr4());
}

extern unsigned long mmu_cr4_features;

extern void initialize_tlbstate_and_flush(void);

void flush_tlb_local(void);
void flush_tlb_one_user(unsigned long addr);
void flush_tlb_one_kernel(unsigned long addr);
void flush_tlb_mm_range(struct mm_struct *mm, unsigned long start,
			unsigned long end, unsigned int stride_shift,
			bool freed_tables);
static inline u64 inc_mm_tlb_gen(struct mm_struct *mm)
{
	 
	return atomic64_inc_return(&mm->context.tlb_gen);
}

#endif  

#endif
