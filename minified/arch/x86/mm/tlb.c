/* Simplified TLB management for single-CPU hello-world kernel */
#include <asm/mmu_context.h>
#include <asm/cache.h>
#include <asm/apic.h>

DEFINE_STATIC_KEY_TRUE(rdpmc_never_available_key);
DEFINE_STATIC_KEY_FALSE(rdpmc_always_available_key);

#define STATIC_NOPV static
#define __flush_tlb_local native_flush_tlb_local
#define __flush_tlb_global native_flush_tlb_global
#define __flush_tlb_one_user(addr) native_flush_tlb_one_user(addr)

atomic64_t last_mm_ctx_id = ATOMIC64_INIT(1);

void switch_mm(struct mm_struct *prev, struct mm_struct *next,
	       struct task_struct *tsk)
{
	unsigned long flags;

	local_irq_save(flags);
	switch_mm_irqs_off(prev, next, tsk);
	local_irq_restore(flags);
}

void switch_mm_irqs_off(struct mm_struct *prev, struct mm_struct *next,
			struct task_struct *tsk)
{
	struct mm_struct *real_prev = this_cpu_read(cpu_tlbstate.loaded_mm);
	unsigned cpu = smp_processor_id();

	if (real_prev == next)
		return;

	if (real_prev != &init_mm)
		cpumask_clear_cpu(cpu, mm_cpumask(real_prev));
	if (next != &init_mm)
		cpumask_set_cpu(cpu, mm_cpumask(next));

	this_cpu_write(cpu_tlbstate.ctxs[0].ctx_id, next->context.ctx_id);
	this_cpu_write(cpu_tlbstate.ctxs[0].tlb_gen,
		       atomic64_read(&next->context.tlb_gen));
	write_cr3(__sme_pa(next->pgd));

	barrier();
	this_cpu_write(cpu_tlbstate.loaded_mm, next);
	this_cpu_write(cpu_tlbstate.loaded_mm_asid, 0);

	if (next != real_prev) {
		cr4_clear_bits_irqsoff(X86_CR4_PCE);
		switch_ldt(real_prev, next);
	}
}

void enter_lazy_tlb(struct mm_struct *mm, struct task_struct *tsk)
{
}

void initialize_tlbstate_and_flush(void)
{
	int i;
	struct mm_struct *mm = this_cpu_read(cpu_tlbstate.loaded_mm);

	write_cr3(__sme_pa(mm->pgd));

	this_cpu_write(cpu_tlbstate.loaded_mm_asid, 0);
	this_cpu_write(cpu_tlbstate.next_asid, 1);
	this_cpu_write(cpu_tlbstate.ctxs[0].ctx_id, mm->context.ctx_id);
	this_cpu_write(cpu_tlbstate.ctxs[0].tlb_gen,
		       atomic64_read(&init_mm.context.tlb_gen));

	for (i = 1; i < TLB_NR_DYN_ASIDS; i++)
		this_cpu_write(cpu_tlbstate.ctxs[i].ctx_id, 0);
}

DEFINE_PER_CPU_SHARED_ALIGNED(struct tlb_state_shared, cpu_tlbstate_shared);

unsigned long tlb_single_page_flush_ceiling __read_mostly = 33;

void flush_tlb_mm_range(struct mm_struct *mm, unsigned long start,
			unsigned long end, unsigned int stride_shift,
			bool freed_tables)
{
	inc_mm_tlb_gen(mm);
	flush_tlb_local();
}

void flush_tlb_one_kernel(unsigned long addr)
{
	flush_tlb_one_user(addr);
}

STATIC_NOPV void native_flush_tlb_one_user(unsigned long addr)
{
	asm volatile("invlpg (%0)" ::"r"(addr) : "memory");
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
	native_write_cr3(__native_read_cr3());
}

void flush_tlb_local(void)
{
	__flush_tlb_local();
}

void __flush_tlb_all(void)
{
	if (boot_cpu_has(X86_FEATURE_PGE))
		__flush_tlb_global();
	else
		flush_tlb_local();
}
