

#include <asm/tlbflush.h>
#include <asm/mmu_context.h>


# define STATIC_NOPV			static
# define __flush_tlb_local		native_flush_tlb_local
# define __flush_tlb_global		native_flush_tlb_global
# define __flush_tlb_one_user(addr)	native_flush_tlb_one_user(addr)


static inline unsigned long build_cr3(pgd_t *pgd, u16 asid)
{
	/* PCID is unconditionally cleared at boot (setup_clear_cpu_cap in
	 * arch/x86/kernel/cpu/common.c), so the kernel never runs with PCIDs
	 * and asid is always 0. */
	VM_WARN_ON_ONCE(asid != 0);
	return __sme_pa(pgd);
}

atomic64_t last_mm_ctx_id = ATOMIC64_INIT(1);


static void choose_new_asid(u16 *new_asid)
{
	/* No PCID (cleared at boot): always use ASID 0 and force a flush. */
	*new_asid = 0;
}

static void load_new_mm_cr3(pgd_t *pgdir, u16 new_asid)
{
	/* Without PCID every switch reloads CR3 (always a full flush). */
	write_cr3(build_cr3(pgdir, new_asid));
}

void switch_mm(struct mm_struct *prev, struct mm_struct *next, struct task_struct *tsk)
{
	unsigned long flags;

	local_irq_save(flags);
	switch_mm_irqs_off(prev, next, tsk);
	local_irq_restore(flags);
}

/* Speculation-mitigation switch_mm path removed: CONFIG_SPECULATION_MITIGATIONS
 * is unset, so switch_mm_cond_ibpb / switch_mm_always_ibpb /
 * switch_mm_cond_l1d_flush are DEFINE_STATIC_KEY_FALSE and never enabled. With
 * all three keys off, cond_mitigation's only observable effect was writing the
 * never-read last_user_mm_spec field, so the whole helper chain
 * (cond_mitigation / mm_mangle_tif_spec_bits / l1d_flush_evaluate /
 * l1d_flush_force_sigbus) was dead. */

static inline void cr4_update_pce_mm(struct mm_struct *mm)
{
	/* rdpmc_always_available_key is DEFINE_STATIC_KEY_FALSE and never
	 * enabled (constant false); rdpmc_never_available_key is
	 * DEFINE_STATIC_KEY_TRUE and never disabled (constant true), so the
	 * RDPMC-enable condition is always false and CR4.PCE stays cleared. */
	cr4_clear_bits_irqsoff(X86_CR4_PCE);
}

void switch_mm_irqs_off(struct mm_struct *prev, struct mm_struct *next, struct task_struct *tsk)
{
	struct mm_struct *real_prev = this_cpu_read(cpu_tlbstate.loaded_mm);
	u16 prev_asid = this_cpu_read(cpu_tlbstate.loaded_mm_asid);
	bool was_lazy = this_cpu_read(cpu_tlbstate_shared.is_lazy);
	unsigned cpu = smp_processor_id();
	u64 next_tlb_gen;
	u16 new_asid;

	 

	 
	if (was_lazy)
		this_cpu_write(cpu_tlbstate_shared.is_lazy, false);

	 
	if (real_prev == next) {
		VM_WARN_ON(this_cpu_read(cpu_tlbstate.ctxs[prev_asid].ctx_id) != next->context.ctx_id);

		 
		if (WARN_ON_ONCE(real_prev != &init_mm && !cpumask_test_cpu(cpu, mm_cpumask(next))))
			cpumask_set_cpu(cpu, mm_cpumask(next));

		 
		if (!was_lazy)
			return;

		 
		smp_mb();
		next_tlb_gen = atomic64_read(&next->context.tlb_gen);
		if (this_cpu_read(cpu_tlbstate.ctxs[prev_asid].tlb_gen) == next_tlb_gen)
			return;


		new_asid = prev_asid;
	} else {

		if (real_prev != &init_mm) {
			VM_WARN_ON_ONCE(!cpumask_test_cpu(cpu, mm_cpumask(real_prev)));
			cpumask_clear_cpu(cpu, mm_cpumask(real_prev));
		}

		 
		if (next != &init_mm)
			cpumask_set_cpu(cpu, mm_cpumask(next));
		next_tlb_gen = atomic64_read(&next->context.tlb_gen);

		choose_new_asid(&new_asid);

		 
		this_cpu_write(cpu_tlbstate.loaded_mm, LOADED_MM_SWITCHING);
		barrier();
	}

	/* need_flush is always true: no PCID, so every switch reloads CR3. */
	this_cpu_write(cpu_tlbstate.ctxs[new_asid].ctx_id, next->context.ctx_id);
	this_cpu_write(cpu_tlbstate.ctxs[new_asid].tlb_gen, next_tlb_gen);
	load_new_mm_cr3(next->pgd, new_asid);


	barrier();

	this_cpu_write(cpu_tlbstate.loaded_mm, next);
	this_cpu_write(cpu_tlbstate.loaded_mm_asid, new_asid);

	if (next != real_prev) {
		cr4_update_pce_mm(next);
		switch_ldt(real_prev, next);
	}
}

void enter_lazy_tlb(struct mm_struct *mm, struct task_struct *tsk)
{
	if (this_cpu_read(cpu_tlbstate.loaded_mm) == &init_mm)
		return;

	this_cpu_write(cpu_tlbstate_shared.is_lazy, true);
}

void initialize_tlbstate_and_flush(void)
{
	int i;
	struct mm_struct *mm = this_cpu_read(cpu_tlbstate.loaded_mm);
	u64 tlb_gen = atomic64_read(&init_mm.context.tlb_gen);
	unsigned long cr3 = __read_cr3();

	 
	WARN_ON((cr3 & CR3_ADDR_MASK) != __pa(mm->pgd));

	 
	WARN_ON(boot_cpu_has(X86_FEATURE_PCID) && !(cr4_read_shadow() & X86_CR4_PCIDE));

	 
	write_cr3(build_cr3(mm->pgd, 0));


	this_cpu_write(cpu_tlbstate.loaded_mm_asid, 0);
	this_cpu_write(cpu_tlbstate.ctxs[0].ctx_id, mm->context.ctx_id);
	this_cpu_write(cpu_tlbstate.ctxs[0].tlb_gen, tlb_gen);

	for (i = 1; i < TLB_NR_DYN_ASIDS; i++)
		this_cpu_write(cpu_tlbstate.ctxs[i].ctx_id, 0);
}

DEFINE_PER_CPU_SHARED_ALIGNED(struct tlb_state_shared, cpu_tlbstate_shared);

void flush_tlb_mm_range(struct mm_struct *mm, unsigned long start, unsigned long end, unsigned int stride_shift, bool freed_tables)
{
	/* mmu_gather/unmap teardown never runs on this single-shot boot, so this
	 * range-flush root is runtime-dead; bump the generation counter so any
	 * live reader of mm->context.tlb_gen stays consistent and return. */
	inc_mm_tlb_gen(mm);
}


void flush_tlb_one_kernel(unsigned long addr)
{
	/*
	 * PAGE_TABLE_ISOLATION is unset on this build, so X86_FEATURE_PTI is
	 * never set and the user-mapping invalidation below is dead.
	 */
	flush_tlb_one_user(addr);
}

STATIC_NOPV void native_flush_tlb_one_user(unsigned long addr)
{
	asm volatile("invlpg (%0)" ::"r" (addr) : "memory");

	/*
	 * PAGE_TABLE_ISOLATION is unset, so X86_FEATURE_PTI is never set and the
	 * user-PCID invalidation that would otherwise follow here is dead.
	 */
}

void flush_tlb_one_user(unsigned long addr)
{
	__flush_tlb_one_user(addr);
}

STATIC_NOPV void native_flush_tlb_global(void)
{
	unsigned long flags;

	if (static_cpu_has(X86_FEATURE_INVPCID)) {
		 
		invpcid_flush_all();
		return;
	}

	 
	raw_local_irq_save(flags);

	__native_tlb_flush_global(this_cpu_read(cpu_tlbstate.cr4));

	raw_local_irq_restore(flags);
}

STATIC_NOPV void native_flush_tlb_local(void)
{
	 
	WARN_ON_ONCE(preemptible());


	native_write_cr3(__native_read_cr3());
}

void flush_tlb_local(void)
{
	__flush_tlb_local();
}

void __flush_tlb_all(void)
{
	 
	VM_WARN_ON_ONCE(preemptible());

	if (boot_cpu_has(X86_FEATURE_PGE)) {
		__flush_tlb_global();
	} else {
		 
		flush_tlb_local();
	}
}

/* Stub: TLB flush debugfs tuning not needed for minimal kernel */
