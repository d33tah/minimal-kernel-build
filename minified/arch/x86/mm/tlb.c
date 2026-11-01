// SPDX-License-Identifier: GPL-2.0-only
#include <asm/mmu_context.h>

#define STATIC_NOPV static
#define __flush_tlb_local native_flush_tlb_local
#define __flush_tlb_global native_flush_tlb_global
#define __flush_tlb_one_user(addr) native_flush_tlb_one_user(addr)

static void flush_tlb_local(void);

static inline unsigned long build_cr3(pgd_t *pgd, u16 asid)
{
	/* PCID is unconditionally cleared at boot (setup_clear_cpu_cap in
	 * arch/x86/kernel/cpu/common.c), so the kernel never runs with PCIDs
	 * and asid is always 0. */
	return __sme_pa(pgd);
}

static void load_new_mm_cr3(pgd_t *pgdir, u16 new_asid)
{
	/* Without PCID every switch reloads CR3 (always a full flush). */
	write_cr3(build_cr3(pgdir, new_asid));
}

void switch_mm(struct mm_struct *prev, struct mm_struct *next,
	       struct task_struct *tsk)
{
	unsigned long flags;

	local_irq_save(flags);
	switch_mm_irqs_off(prev, next, tsk);
	local_irq_restore(flags);
}

static inline void cr4_update_pce_mm(struct mm_struct *mm)
{
	/* rdpmc_always_available_key is DEFINE_STATIC_KEY_FALSE and never
	 * enabled (constant false); rdpmc_never_available_key is
	 * DEFINE_STATIC_KEY_TRUE and never disabled (constant true), so the
	 * RDPMC-enable condition is always false and CR4.PCE stays cleared. */
	cr4_clear_bits_irqsoff(X86_CR4_PCE);
}

void switch_mm_irqs_off(struct mm_struct *prev, struct mm_struct *next,
			struct task_struct *tsk)
{
	struct mm_struct *real_prev = this_cpu_read(cpu_tlbstate.loaded_mm);

	/*
	 * UP: a lazy mm stays loaded and flush_tlb_mm_range() flushes it in
	 * place, so switching back to it needs no CR3 reload.
	 */
	if (real_prev == next)
		return;

	/* need_flush is always true: no PCID, so every switch reloads CR3. */
	load_new_mm_cr3(next->pgd, 0);

	/* Make sure we write CR3 before loaded_mm. */
	barrier();

	this_cpu_write(cpu_tlbstate.loaded_mm, next);

	cr4_update_pce_mm(next);
}

/*
 * Call this when reinitializing a CPU.  It fixes the following potential
 * problems:
 *
 * - The ASID changed from what cpu_tlbstate thinks it is (most likely
 *   because the CPU was taken down and came back up with CR3's PCID
 *   bits clear.  CPU hotplug can do this.
 *
 * - The TLB contains junk in slots corresponding to inactive ASIDs.
 *
 * - The CPU went so far out to lunch that it may have missed a TLB
 *   flush.
 */
void initialize_tlbstate_and_flush(void)
{
	struct mm_struct *mm = this_cpu_read(cpu_tlbstate.loaded_mm);

	/* Force ASID 0 and force a TLB flush. */
	write_cr3(build_cr3(mm->pgd, 0));
}

void flush_tlb_mm_range(struct mm_struct *mm, unsigned long start,
			unsigned long end, unsigned int stride_shift,
			bool freed_tables)
{
	unsigned long addr;

	preempt_disable();
	/*
	 * UP: only the loaded mm can have TLB entries; any other mm gets a CR3
	 * reload in switch_mm_irqs_off. Small ranges use invlpg (which also drops
	 * paging-structure caches), larger ones and TLB_FLUSH_ALL reload CR3.
	 */
	if (this_cpu_read(cpu_tlbstate.loaded_mm) == mm) {
		if ((end - start) >> stride_shift > 33)
			flush_tlb_local();
		else
			for (addr = start; addr < end;
			     addr += 1UL << stride_shift)
				flush_tlb_one_user(addr);
	}
	preempt_enable();
}

/*
 * Flush one page in the kernel mapping
 */
void flush_tlb_one_kernel(unsigned long addr)
{
	flush_tlb_one_user(addr);
}

/*
 * Flush one page in the user mapping
 */
STATIC_NOPV void native_flush_tlb_one_user(unsigned long addr)
{
	asm volatile("invlpg (%0)" ::"r"(addr) : "memory");
}

void flush_tlb_one_user(unsigned long addr)
{
	__flush_tlb_one_user(addr);
}

/*
 * Flush everything
 */
STATIC_NOPV void native_flush_tlb_global(void)
{
	unsigned long flags;

	/*
	 * Read-modify-write to CR4 - protect it from preemption and
	 * from interrupts. (Use the raw variant because this code can
	 * be called from deep inside debugging code.)
	 */
	raw_local_irq_save(flags);

	__native_tlb_flush_global(this_cpu_read(cpu_tlbstate.cr4));

	raw_local_irq_restore(flags);
}

/*
 * Flush the entire current user mapping
 */
STATIC_NOPV void native_flush_tlb_local(void)
{
	/* If current->mm == NULL then the read_cr3() "borrows" an mm */
	native_write_cr3(__native_read_cr3());
}

static void flush_tlb_local(void)
{
	__flush_tlb_local();
}

/*
 * Flush everything
 */
void __flush_tlb_all(void)
{
	if (boot_cpu_has(X86_FEATURE_PGE)) {
		__flush_tlb_global();
	} else {
		/*
		 * !PGE -> !PCID (setup_pcid()), thus every flush is total.
		 */
		flush_tlb_local();
	}
}
