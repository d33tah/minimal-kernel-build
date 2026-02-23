 
#ifndef _ASM_X86_TLBFLUSH_H
#define _ASM_X86_TLBFLUSH_H

#include <linux/mm.h>
#include <linux/sched.h>

#include <asm/processor.h>
#include <asm/cpufeature.h>
#include <asm/special_insns.h>
#include <asm/processor-flags.h>

static inline void __invpcid(unsigned long pcid, unsigned long addr, unsigned long type)
{
	struct { u64 d[2]; } desc = { { pcid, addr } };
	asm volatile("invpcid %[desc], %[type]" :: [desc] "m" (desc), [type] "r" (type) : "memory");
}
#define INVPCID_TYPE_ALL_INCL_GLOBAL	2
static inline void invpcid_flush_all(void) { __invpcid(0, 0, INVPCID_TYPE_ALL_INCL_GLOBAL); }

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

static inline void cr4_init_shadow(void)
{
	this_cpu_write(cpu_tlbstate.cr4, __read_cr4());
}

extern unsigned long mmu_cr4_features;

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

extern void flush_tlb_mm_range(struct mm_struct *mm, unsigned long start,
				unsigned long end, unsigned int stride_shift,
				bool freed_tables);
static inline u64 inc_mm_tlb_gen(struct mm_struct *mm)
{
	 
	return atomic64_inc_return(&mm->context.tlb_gen);
}

#endif  

static inline void __native_tlb_flush_global(unsigned long cr4)
{
	native_write_cr4(cr4 ^ X86_CR4_PGE);
	native_write_cr4(cr4);
}
#endif  
