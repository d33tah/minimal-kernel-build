
#include <linux/kernel.h>
#include <linux/percpu.h>
#include <linux/delay.h>
#include <linux/sched/mm.h>
#include <linux/sched/task.h>

#include <asm/mmu_context.h>

/* Inlined from asm/doublefault.h */
extern void doublefault_init_cpu_tss(void);
#include <asm/proto.h>
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
			      [GDT_ENTRY_PNPBIOS_CS32] = GDT_ENTRY_INIT(0x409a,
									0,
									0xffff),
			      [GDT_ENTRY_PNPBIOS_CS16] = GDT_ENTRY_INIT(0x009a,
									0,
									0xffff),
			      [GDT_ENTRY_PNPBIOS_DS] = GDT_ENTRY_INIT(0x0092, 0,
								      0xffff),
			      [GDT_ENTRY_PNPBIOS_TS1] = GDT_ENTRY_INIT(0x0092,
								       0, 0),
			      [GDT_ENTRY_PNPBIOS_TS2] = GDT_ENTRY_INIT(0x0092,
								       0, 0),
			      [GDT_ENTRY_APMBIOS_BASE] = GDT_ENTRY_INIT(0x409a,
									0,
									0xffff),
			      [GDT_ENTRY_APMBIOS_BASE +
				      1] = GDT_ENTRY_INIT(0x009a, 0, 0xffff),
			      [GDT_ENTRY_APMBIOS_BASE +
				      2] = GDT_ENTRY_INIT(0x4092, 0, 0xffff),
			      [GDT_ENTRY_ESPFIX_SS] = GDT_ENTRY_INIT(0xc092, 0,
								     0xfffff),
			      [GDT_ENTRY_PERCPU] = GDT_ENTRY_INIT(0xc092, 0,
								  0xfffff),
		      } };

static __always_inline void setup_smep(struct cpuinfo_x86 *c)
{
	if (cpu_has(c, X86_FEATURE_SMEP))
		cr4_set_bits(X86_CR4_SMEP);
}

static __always_inline void setup_umip(struct cpuinfo_x86 *c)
{
	/*
	 * UMIP is in DISABLED_MASK16, so cpu_feature_enabled(X86_FEATURE_UMIP)
	 * is constant 0 -- the activate path was dead; only the clear remains.
	 */
	cr4_clear_bits(X86_CR4_UMIP);
}

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

	lockdep_assert_irqs_disabled();

	newval = (cr4 & ~clear) | set;
	if (newval != cr4) {
		this_cpu_write(cpu_tlbstate.cr4, newval);
		__write_cr4(newval);
	}
}

unsigned long cr4_read_shadow(void)
{
	return this_cpu_read(cpu_tlbstate.cr4);
}

static void __init setup_cr_pinning(void)
{
	cr4_pinned_bits = this_cpu_read(cpu_tlbstate.cr4) & cr4_pinned_mask;
	static_key_enable(&cr_pinning.key);
}

DEFINE_PER_CPU(struct cpu_entry_area *, cpu_entry_area);

/*
 * Only CPUID leaves 1 and 7 feed bits that something reads: the
 * boot_cpu_has() users (TSC, PSE, PGE, SEP, SMEP, FSGSBASE, INVPCID,
 * HYPERVISOR, WAITPKG, CX8) and ELF_HWCAP (the leaf 1 EDX word). The family
 * is checked by check_bugs(); the CLFLUSH line size is cache_line_size().
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
		c->x86_capability[CPUID_7_ECX] = ecx;
	}

	/* Process-context identifiers are never used by this kernel. */
	clear_bit(X86_FEATURE_PCID, (unsigned long *)c->x86_capability);
	/*
	 * Alternatives are never applied, so the STAC/CLAC around user
	 * accesses are never patched in: CR4.SMAP must stay off and the
	 * feature must read as absent (the page-fault path would otherwise
	 * treat every kernel fault on a user address as an SMAP violation).
	 */
	clear_bit(X86_FEATURE_SMAP, (unsigned long *)c->x86_capability);
}

void __init early_cpu_init(void)
{
	identify_cpu(&boot_cpu_data);
}

void enable_sep_cpu(void)
{
	struct tss_struct *tss;
	int cpu;

	if (!boot_cpu_has(X86_FEATURE_SEP))
		return;

	cpu = get_cpu();
	tss = &per_cpu(cpu_tss_rw, cpu);

	tss->x86_tss.ss1 = __KERNEL_CS;
	wrmsr(MSR_IA32_SYSENTER_CS, tss->x86_tss.ss1, 0);
	wrmsr(MSR_IA32_SYSENTER_ESP, (unsigned long)(cpu_entry_stack(cpu) + 1),
	      0);
	wrmsr(MSR_IA32_SYSENTER_EIP, (unsigned long)entry_SYSENTER_32, 0);

	put_cpu();
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

	enable_sep_cpu();
	setup_cr_pinning();
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

#define dbg_restore_debug_regs()

static inline void tss_setup_io_bitmap(struct tss_struct *tss)
{
	tss->x86_tss.io_bitmap_base = IO_BITMAP_OFFSET_INVALID;
}

void cpu_init_exception_handling(void)
{
	struct tss_struct *tss = this_cpu_ptr(&cpu_tss_rw);
	int cpu = raw_smp_processor_id();

	tss_setup_io_bitmap(tss);
	set_tss_desc(cpu, &get_cpu_entry_area(cpu)->tss.x86_tss);

	load_TR_desc();

	load_current_idt();
}

void cpu_init(void)
{
	struct task_struct *cur = current;
	int cpu = raw_smp_processor_id();
	struct desc_ptr gdt_descr;

	if (cpu_feature_enabled(X86_FEATURE_VME) ||
	    boot_cpu_has(X86_FEATURE_TSC) || boot_cpu_has(X86_FEATURE_DE))
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
	enter_lazy_tlb(&init_mm, cur);

	load_sp0((unsigned long)(cpu_entry_stack(cpu) + 1));

	load_mm_ldt(&init_mm);

	clear_all_debug_regs();
	dbg_restore_debug_regs();

	doublefault_init_cpu_tss();

	{
		struct desc_ptr gdt_descr;

		gdt_descr.address = (long)get_cpu_gdt_ro(cpu);
		gdt_descr.size = GDT_SIZE - 1;
		load_gdt(&gdt_descr);
	}
}
