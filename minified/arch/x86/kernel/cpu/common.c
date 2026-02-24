#include <linux/percpu.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/sched/mm.h>
#include <linux/sched/task.h>
#include <linux/init.h>

#include <asm/mmu_context.h>

#include <linux/utsname.h>
#include <asm/processor.h>
#include <asm/tlbflush.h>
#include <linux/atomic.h>
#include <asm/desc.h>
#include <asm/fpu/api.h>
#include <asm/cpu.h>

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

			      [GDT_ENTRY_ESPFIX_SS] = GDT_ENTRY_INIT(0xc092, 0,
								     0xfffff),
			      [GDT_ENTRY_PERCPU] = GDT_ENTRY_INIT(0xc092, 0,
								  0xfffff),
		      } };

static const unsigned long cr4_pinned_mask = X86_CR4_SMEP | X86_CR4_SMAP |
					     X86_CR4_UMIP | X86_CR4_FSGSBASE |
					     X86_CR4_CET;
static DEFINE_STATIC_KEY_FALSE_RO(cr_pinning);
static unsigned long cr4_pinned_bits __ro_after_init;

void native_write_cr0(unsigned long val)
{
	unsigned long bits_missing = 0;

set_register:
	asm volatile("mov %0,%%cr0" : "+r"(val) : : "memory");

	if (static_branch_likely(&cr_pinning)) {
		if (unlikely((val & X86_CR0_WP) != X86_CR0_WP)) {
			bits_missing = X86_CR0_WP;
			val |= bits_missing;
			goto set_register;
		}

		WARN_ONCE(bits_missing, "CR0 WP bit went missing!?\n");
	}
}

void __no_profile native_write_cr4(unsigned long val)
{
	unsigned long bits_changed = 0;

set_register:
	asm volatile("mov %0,%%cr4" : "+r"(val) : : "memory");

	if (static_branch_likely(&cr_pinning)) {
		if (unlikely((val & cr4_pinned_mask) != cr4_pinned_bits)) {
			bits_changed = (val & cr4_pinned_mask) ^
				       cr4_pinned_bits;
			val = (val & ~cr4_pinned_mask) | cr4_pinned_bits;
			goto set_register;
		}

		WARN_ONCE(bits_changed, "pinned CR4 bits changed: 0x%lx!?\n",
			  bits_changed);
	}
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

__u32 cpu_caps_cleared[NCAPINTS + NBUGINTS] __aligned(sizeof(unsigned long));
__u32 cpu_caps_set[NCAPINTS + NBUGINTS] __aligned(sizeof(unsigned long));

static void load_percpu_segment(int cpu)
{
	loadsegment(fs, __KERNEL_PERCPU);
}

DEFINE_PER_CPU(struct cpu_entry_area *, cpu_entry_area);

static void load_direct_gdt(int cpu)
{
	struct desc_ptr gdt_descr;

	gdt_descr.address = (long)get_cpu_gdt_rw(cpu);
	gdt_descr.size = GDT_SIZE - 1;
	load_gdt(&gdt_descr);
}

static void load_fixmap_gdt(int cpu)
{
	struct desc_ptr gdt_descr;

	gdt_descr.address = (long)get_cpu_gdt_ro(cpu);
	gdt_descr.size = GDT_SIZE - 1;
	load_gdt(&gdt_descr);
}

static void switch_to_new_gdt(int cpu)
{
	load_direct_gdt(cpu);

	load_percpu_segment(cpu);
}

static void cpu_detect(struct cpuinfo_x86 *c)
{
	cpuid(0x00000000, (unsigned int *)&c->cpuid_level,
	      (unsigned int *)&c->x86_vendor_id[0],
	      (unsigned int *)&c->x86_vendor_id[8],
	      (unsigned int *)&c->x86_vendor_id[4]);

	c->x86 = 4;

	if (c->cpuid_level >= 0x00000001) {
		u32 junk, tfms, cap0, misc;

		cpuid(0x00000001, &tfms, &misc, &junk, &cap0);
		c->x86 = x86_family(tfms);
		c->x86_model = x86_model(tfms);
		c->x86_stepping = x86_stepping(tfms);

		if (cap0 & (1 << 19)) {
			c->x86_clflush_size = ((misc >> 8) & 0xff) * 8;
			c->x86_cache_alignment = c->x86_clflush_size;
		}
	}
}

static void apply_forced_caps(struct cpuinfo_x86 *c)
{
	int i;

	for (i = 0; i < NCAPINTS + NBUGINTS; i++) {
		c->x86_capability[i] &= ~cpu_caps_cleared[i];
		c->x86_capability[i] |= cpu_caps_set[i];
	}
}

static void get_cpu_cap(struct cpuinfo_x86 *c)
{
	u32 eax, ebx, ecx, edx;

	if (c->cpuid_level >= 0x00000001) {
		cpuid(0x00000001, &eax, &ebx, &ecx, &edx);

		c->x86_capability[CPUID_1_ECX] = ecx;
		c->x86_capability[CPUID_1_EDX] = edx;
	}

	c->extended_cpuid_level = cpuid_eax(0x80000000);
	apply_forced_caps(c);
}

void __init early_cpu_init(void)
{
	{
		struct cpuinfo_x86 *c = &boot_cpu_data;

		c->x86_clflush_size = 32;
		c->x86_phys_bits = 32;
		c->x86_virt_bits = 32;
		c->x86_cache_alignment = c->x86_clflush_size;

		memset(&c->x86_capability, 0, sizeof(c->x86_capability));
		c->extended_cpuid_level = 0;

		cpu_detect(c);
		get_cpu_cap(c);

		setup_force_cpu_cap(X86_FEATURE_ALWAYS);

		fpu__init_system(c);

		setup_clear_cpu_cap(X86_FEATURE_PCID);

		if (!pgtable_l5_enabled())
			setup_clear_cpu_cap(X86_FEATURE_LA57);

		setup_clear_cpu_cap(X86_FEATURE_NOPL);
	}
}

static void identify_cpu(struct cpuinfo_x86 *c)
{
	c->loops_per_jiffy = loops_per_jiffy;
	c->x86_vendor = X86_VENDOR_UNKNOWN;
	c->x86_model = c->x86_stepping = 0;
	c->x86_vendor_id[0] = '\0';
	c->cpuid_level = -1;
	c->x86_clflush_size = 32;
	c->x86_phys_bits = 32;
	c->x86_virt_bits = 32;
	c->x86_cache_alignment = c->x86_clflush_size;
	memset(&c->x86_capability, 0, sizeof(c->x86_capability));

	c->extended_cpuid_level = 0;
	cpu_detect(c);
	get_cpu_cap(c);
	set_cpu_bug(c, X86_BUG_ESPFIX);
	apply_forced_caps(c);
	cr4_clear_bits(X86_CR4_UMIP);
	apply_forced_caps(c);
	select_idle_routine(c);
}

static void __init identify_boot_cpu(void)
{
	identify_cpu(&boot_cpu_data);
	/* HAS_KERNEL_IBT is 0 */
	sysenter_setup();
	cr4_pinned_bits = this_cpu_read(cpu_tlbstate.cr4) & cr4_pinned_mask;
	static_key_enable(&cr_pinning.key);
}

/* inlined from bugs.c */
void __init check_bugs(void)
{
	identify_boot_cpu();

	if (boot_cpu_data.x86 < 4)
		panic("Kernel requires i486+ for 'invlpg' and other features");

	init_utsname()->machine[1] =
		'0' + (boot_cpu_data.x86 > 6 ? 6 : boot_cpu_data.x86);

	alternatives_patched = 1;
}

int __read_mostly alternatives_patched;

DEFINE_PER_CPU(struct task_struct *, current_task) = &init_task;
DEFINE_PER_CPU(int, __preempt_count) = INIT_PREEMPT_COUNT;

DEFINE_PER_CPU(unsigned long,
	       cpu_current_top_of_stack) = (unsigned long)&init_thread_union
					   + THREAD_SIZE;

void cpu_init_exception_handling(void)
{
	struct tss_struct *tss = this_cpu_ptr(&cpu_tss_rw);
	int cpu = raw_smp_processor_id();

	tss->x86_tss.io_bitmap_base = IO_BITMAP_OFFSET_INVALID;
	set_tss_desc(cpu, &get_cpu_entry_area(cpu)->tss.x86_tss);

	load_TR_desc();
	load_current_idt();
}

void cpu_init(void)
{
	struct task_struct *cur = current;
	int cpu = raw_smp_processor_id();

	/* X86_32: check VME/TSC/DE features */
	if (cpu_feature_enabled(X86_FEATURE_VME) ||
	    boot_cpu_has(X86_FEATURE_TSC) || boot_cpu_has(X86_FEATURE_DE))
		cr4_clear_bits(X86_CR4_VME | X86_CR4_PVI | X86_CR4_TSD |
			       X86_CR4_DE);

	switch_to_new_gdt(cpu);

	mmgrab(&init_mm);
	cur->active_mm = &init_mm;
	BUG_ON(cur->mm);
	initialize_tlbstate_and_flush();
	enter_lazy_tlb(&init_mm, cur);

	load_sp0((unsigned long)(cpu_entry_stack(cpu) + 1));

	load_mm_ldt(&init_mm);

	{
		int i;
		for (i = 0; i < 8; i++) {
			if ((i == 4) || (i == 5))
				continue;
			set_debugreg(0, i);
		}
	}

	fpu__init_cpu();

	load_fixmap_gdt(cpu);
}
