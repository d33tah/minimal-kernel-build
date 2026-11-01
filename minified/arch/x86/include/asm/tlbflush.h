/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_TLBFLUSH_H
#define _ASM_X86_TLBFLUSH_H

#include <linux/mm.h>

void __flush_tlb_all(void);

void cr4_update_irqsoff(unsigned long set, unsigned long clear);

/* Set in this cpu's CR4. */
static inline void cr4_set_bits_irqsoff(unsigned long mask)
{
	cr4_update_irqsoff(mask, 0);
}

/* Clear in this cpu's CR4. */
static inline void cr4_clear_bits_irqsoff(unsigned long mask)
{
	cr4_update_irqsoff(0, mask);
}

/* Set in this cpu's CR4. */
static inline void cr4_set_bits(unsigned long mask)
{
	unsigned long flags;

	local_irq_save(flags);
	cr4_set_bits_irqsoff(mask);
	local_irq_restore(flags);
}

/* Clear in this cpu's CR4. */
static inline void cr4_clear_bits(unsigned long mask)
{
	unsigned long flags;

	local_irq_save(flags);
	cr4_clear_bits_irqsoff(mask);
	local_irq_restore(flags);
}

struct tlb_state {
	/*
	 * cpu_tlbstate.loaded_mm should match CR3 whenever interrupts
	 * are on.  This means that it may not match current->active_mm,
	 * which will contain the previous user mm when we're in lazy TLB
	 * mode even if we've already switched back to swapper_pg_dir.
	 *
	 * During switch_mm_irqs_off(), loaded_mm will be set to
	 * LOADED_MM_SWITCHING during the brief interrupts-off window
	 * when CR3 and loaded_mm would otherwise be inconsistent.  This
	 * is for nmi_uaccess_okay()'s benefit.
	 */
	struct mm_struct *loaded_mm;

	/*
	 * Access to this CR4 shadow and to H/W CR4 is protected by
	 * disabling interrupts when modifying either one.
	 */
	unsigned long cr4;
};
DECLARE_PER_CPU_ALIGNED(struct tlb_state, cpu_tlbstate);

/* Initialize cr4 shadow for this CPU. */
static inline void cr4_init_shadow(void)
{
	this_cpu_write(cpu_tlbstate.cr4, __read_cr4());
}

extern unsigned long mmu_cr4_features;

extern void initialize_tlbstate_and_flush(void);
void flush_tlb_one_user(unsigned long addr);
void flush_tlb_one_kernel(unsigned long addr);

extern void flush_tlb_mm_range(struct mm_struct *mm, unsigned long start,
			       unsigned long end, unsigned int stride_shift,
			       bool freed_tables);

static inline void __native_tlb_flush_global(unsigned long cr4)
{
	native_write_cr4(cr4 ^ X86_CR4_PGE);
	native_write_cr4(cr4);
}
#endif /* _ASM_X86_TLBFLUSH_H */
