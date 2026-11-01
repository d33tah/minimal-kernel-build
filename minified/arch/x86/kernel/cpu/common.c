#include <linux/sched/mm.h>
#include <linux/sched/task.h>

#include <asm/mmu_context.h>

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

/* Standard macro to see if a specific flag is changeable */
static inline int flag_is_changeable_p(u32 flag)
{
	u32 f1, f2;

	/*
	 * Cyrix and IDT cpus allow disabling of CPUID
	 * so the code below may return different results
	 * when it is executed before and after enabling
	 * the CPUID. Add "volatile" to not allow gcc to
	 * optimize the subsequent calls to this function.
	 */
	asm volatile("pushfl		\n\t"
		     "pushfl		\n\t"
		     "popl %0		\n\t"
		     "movl %0, %1	\n\t"
		     "xorl %2, %0	\n\t"
		     "pushl %0		\n\t"
		     "popfl		\n\t"
		     "pushfl		\n\t"
		     "popl %0		\n\t"
		     "popfl		\n\t"

		     : "=&r"(f1), "=&r"(f2)
		     : "ir"(flag));

	return ((f1 ^ f2) & flag) != 0;
}

/* Probe for the CPUID instruction */
static int have_cpuid_p(void)
{
	return flag_is_changeable_p(X86_EFLAGS_ID);
}

static void cpu_detect(struct cpuinfo_x86 *c)
{
	/* Get vendor name */
	cpuid(0x00000000, (unsigned int *)&c->cpuid_level,
	      (unsigned int *)&c->x86_vendor_id[0],
	      (unsigned int *)&c->x86_vendor_id[8],
	      (unsigned int *)&c->x86_vendor_id[4]);

	c->x86 = 4;
	/* Intel-defined flags: level 0x00000001 */
	if (c->cpuid_level >= 0x00000001) {
		u32 junk, tfms, cap0, misc;

		cpuid(0x00000001, &tfms, &misc, &junk, &cap0);
		c->x86 = x86_family(tfms);

		if (cap0 & (1 << 19)) {
			c->x86_clflush_size = ((misc >> 8) & 0xff) * 8;
			c->x86_cache_alignment = c->x86_clflush_size;
		}
	}
}

static void get_cpu_cap(struct cpuinfo_x86 *c)
{
	u32 eax, ebx, ecx, edx;

	/* Intel-defined flags: level 0x00000001 */
	if (c->cpuid_level >= 0x00000001) {
		cpuid(0x00000001, &eax, &ebx, &ecx, &edx);

		c->x86_capability[CPUID_1_ECX] = ecx;
		c->x86_capability[CPUID_1_EDX] = edx;
	}

	/* Additional Intel-defined flags: level 0x00000007 */
	if (c->cpuid_level >= 0x00000007) {
		cpuid_count(0x00000007, 0, &eax, &ebx, &ecx, &edx);
		c->x86_capability[CPUID_7_0_EBX] = ebx;
	}
}

static void identify_cpu_without_cpuid(struct cpuinfo_x86 *c)
{
	/*
	 * First of all, decide if this is a 486 or higher
	 * It's a 486 if we can modify the AC flag
	 */
	if (flag_is_changeable_p(X86_EFLAGS_AC))
		c->x86 = 4;
	else
		c->x86 = 3;
}

static void __init early_identify_cpu(struct cpuinfo_x86 *c)
{
	c->x86_clflush_size = 32;
	c->x86_cache_alignment = c->x86_clflush_size;

	memset(&c->x86_capability, 0, sizeof(c->x86_capability));

	if (!have_cpuid_p())
		identify_cpu_without_cpuid(c);

	if (have_cpuid_p()) {
		cpu_detect(c);
		get_cpu_cap(c);
	}
}

void __init early_cpu_init(void)
{
	early_identify_cpu(&boot_cpu_data);
}

static void identify_cpu(struct cpuinfo_x86 *c)
{
	/* Set up SMEP/SMAP/UMIP */
	setup_smep(c);
	setup_umip(c);

	/* Enable FSGSBASE instructions if available. */
	if (cpu_has(c, X86_FEATURE_FSGSBASE)) {
		cr4_set_bits(X86_CR4_FSGSBASE);
		elf_hwcap2 |= HWCAP2_FSGSBASE;
	}
}

void __init identify_boot_cpu(void)
{
	identify_cpu(&boot_cpu_data);
}

DEFINE_PER_CPU(struct task_struct *, current_task) = &init_task;
DEFINE_PER_CPU(int, __preempt_count) = INIT_PREEMPT_COUNT;

DEFINE_PER_CPU(unsigned long,
	       cpu_current_top_of_stack) = (unsigned long)&init_thread_union
					   + THREAD_SIZE;

static void load_percpu_segment(int cpu)
{
	loadsegment(fs, __KERNEL_PERCPU);
}

static void load_direct_gdt(int cpu)
{
	struct desc_ptr gdt_descr;

	gdt_descr.address = (long)get_cpu_gdt_rw(cpu);
	gdt_descr.size = GDT_SIZE - 1;
	load_gdt(&gdt_descr);
}

static void switch_to_new_gdt(int cpu)
{
	/* Load the original GDT */
	load_direct_gdt(cpu);
	/* Reload the per-cpu base */
	load_percpu_segment(cpu);
}

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

	if (boot_cpu_has(X86_FEATURE_VME) || boot_cpu_has(X86_FEATURE_TSC) ||
	    boot_cpu_has(X86_FEATURE_DE))
		cr4_clear_bits(X86_CR4_VME | X86_CR4_PVI | X86_CR4_TSD |
			       X86_CR4_DE);

	/*
	 * Initialize the per-CPU GDT with the boot GDT,
	 * and set up the GDT descriptor:
	 */
	switch_to_new_gdt(cpu);

	mmgrab(&init_mm);
	cur->active_mm = &init_mm;
	BUG_ON(cur->mm);
	initialize_tlbstate_and_flush();

	load_sp0(cur->thread.sp0);

	clear_all_debug_regs();

	doublefault_init_cpu_tss();
}
