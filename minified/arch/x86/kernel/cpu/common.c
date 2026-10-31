
#include <linux/sched/mm.h>
#include <linux/sched/task.h>

#include <asm/mmu_context.h>

/* Inlined from asm/doublefault.h */
extern void doublefault_init_cpu_tss(void);
#include <asm/hwcap2.h>
#include <asm/cpu.h>

u32 elf_hwcap2 __read_mostly;

DEFINE_PER_CPU_PAGE_ALIGNED(
	struct gdt_page,
	gdt_page) = { .gdt = {
			      [GDT_ENTRY_KERNEL_CS] = GDT_ENTRY_INIT(0xc09a, 0,
								     0xfffff),
			      [GDT_ENTRY_KERNEL_DS] = GDT_ENTRY_INIT(0xc092, 0,
								     0xfffff),
			      [GDT_ENTRY_DEFAULT_USER_CS] =
				      GDT_ENTRY_INIT(0xc0fa, 0, 0xfffff),
			      [GDT_ENTRY_DEFAULT_USER_DS] =
				      GDT_ENTRY_INIT(0xc0f2, 0, 0xfffff),
		      } };

static __always_inline void setup_smep(struct cpuinfo_x86 *c)
{
	if (cpu_has(c, X86_FEATURE_SMEP))
		cr4_set_bits(X86_CR4_SMEP);
}

static __always_inline void setup_umip(struct cpuinfo_x86 *c)
{
	/* UMIP is never activated (no #GP emulation); only the clear remains. */
	cr4_clear_bits(X86_CR4_UMIP);
}

void native_write_cr0(unsigned long val)
{
	asm volatile("mov %0,%%cr0" : "+r"(val) : : "memory");
}

void __no_profile native_write_cr4(unsigned long val)
{
	asm volatile("mov %0,%%cr4" : "+r"(val) : : "memory");
}

void cr4_update_irqsoff(unsigned long set, unsigned long clear)
{
	unsigned long newval, cr4 = this_cpu_read(cpu_tlbstate.cr4);

	newval = (cr4 & ~clear) | set;
	if (newval != cr4) {
		this_cpu_write(cpu_tlbstate.cr4, newval);
		__write_cr4(newval);
	}
}

/*
 * Only CPUID leaves 1 and 7 feed bits that something reads: the
 * boot_cpu_has() users (VME, DE, TSC, PSE, PGE, HYPERVISOR, SMEP, FSGSBASE)
 * and ELF_HWCAP (the leaf 1 EDX word). The family is checked by
 * check_bugs(); the CLFLUSH line size is cache_line_size().
 * CR4.SMAP is never set (alternatives are never applied, so STAC/CLAC
 * would not be patched in) and nothing tests the SMAP bit.
 */
static void __init identify_cpu(struct cpuinfo_x86 *c)
{
	u32 level, eax, ebx, ecx, edx;

	memset(c->x86_capability, 0, sizeof(c->x86_capability));
	c->x86 = 4;
	c->x86_cache_alignment = 32;

	cpuid(0x00000000, &level, &ebx, &ecx, &edx);

	if (level >= 0x00000001) {
		cpuid(0x00000001, &eax, &ebx, &ecx, &edx);
		c->x86 = x86_family(eax);
		if (edx & (1 << 19))
			c->x86_cache_alignment = ((ebx >> 8) & 0xff) * 8;
		c->x86_capability[CPUID_1_EDX] = edx;
		c->x86_capability[CPUID_1_ECX] = ecx;
	}

	if (level >= 0x00000007) {
		cpuid_count(0x00000007, 0, &eax, &ebx, &ecx, &edx);
		c->x86_capability[CPUID_7_0_EBX] = ebx;
	}
}

void __init early_cpu_init(void)
{
	identify_cpu(&boot_cpu_data);
}

void __init identify_boot_cpu(void)
{
	struct cpuinfo_x86 *c = &boot_cpu_data;

	setup_smep(c);
	setup_umip(c);

	if (cpu_has(c, X86_FEATURE_FSGSBASE)) {
		cr4_set_bits(X86_CR4_FSGSBASE);
		elf_hwcap2 |= HWCAP2_FSGSBASE;
	}
}

DEFINE_PER_CPU(struct task_struct *, current_task) = &init_task;
DEFINE_PER_CPU(int, __preempt_count) = INIT_PREEMPT_COUNT;

DEFINE_PER_CPU(unsigned long,
	       cpu_current_top_of_stack) = (unsigned long)&init_thread_union
					   + THREAD_SIZE;

static void clear_all_debug_regs(void)
{
	int i;

	for (i = 0; i < 8; i++) {
		if ((i == 4) || (i == 5))
			continue;

		set_debugreg(0, i);
	}
}

void cpu_init_exception_handling(void)
{
	struct tss_struct *tss = this_cpu_ptr(&cpu_tss_rw);
	int cpu = raw_smp_processor_id();

	set_tss_desc(cpu, &tss->x86_tss);

	load_TR_desc();

	load_current_idt();
}

void cpu_init(void)
{
	struct task_struct *cur = current;
	int cpu = raw_smp_processor_id();
	struct desc_ptr gdt_descr;

	if (boot_cpu_has(X86_FEATURE_VME) || boot_cpu_has(X86_FEATURE_TSC) ||
	    boot_cpu_has(X86_FEATURE_DE))
		cr4_clear_bits(X86_CR4_VME | X86_CR4_PVI | X86_CR4_TSD |
			       X86_CR4_DE);

	gdt_descr.address = (long)get_cpu_gdt_rw(cpu);
	gdt_descr.size = GDT_SIZE - 1;
	load_gdt(&gdt_descr);
	loadsegment(fs, __KERNEL_PERCPU);

	mmgrab(&init_mm);
	cur->active_mm = &init_mm;
	BUG_ON(cur->mm);
	initialize_tlbstate_and_flush();

	load_sp0(cur->thread.sp0);

	clear_all_debug_regs();

	doublefault_init_cpu_tss();
}
