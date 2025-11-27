
#define USE_EARLY_PGTABLE_L5

#include <linux/memblock.h>
#include <linux/linkage.h>
#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/percpu.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/sched/mm.h>
#include <linux/sched/clock.h>
#include <linux/sched/task.h>
#include <linux/sched/smt.h>
#include <linux/init.h>
#include <linux/kprobes.h>
#include <linux/kgdb.h>
#include <linux/smp.h>
#include <linux/io.h>
#include <linux/syscore_ops.h>
#include <linux/pgtable.h>

#include <asm/cmdline.h>
#include <asm/stackprotector.h>
#include <asm/perf_event.h>
#include <asm/mmu_context.h>
#include <asm/doublefault.h>
#include <asm/archrandom.h>
#include <asm/hypervisor.h>
#include <asm/processor.h>
#include <asm/tlbflush.h>
#include <asm/debugreg.h>
#include <asm/sections.h>
#include <asm/vsyscall.h>
#include <linux/topology.h>
#include <linux/cpumask.h>
#include <linux/atomic.h>
#include <asm/proto.h>
#include <asm/setup.h>
#include <asm/apic.h>
#include <asm/desc.h>
#include <asm/fpu/api.h>
#include <asm/mtrr.h>
#include <asm/hwcap2.h>
#include <linux/numa.h>
#include <asm/numa.h>
#include <asm/asm.h>
#include <asm/bugs.h>
#include <asm/cpu.h>
#include <asm/mce.h>
#include <asm/msr.h>
#include <asm/memtype.h>
#include <asm/microcode.h>
#include <asm/microcode_intel.h>
#include <asm/intel-family.h>
#include <asm/cpu_device_id.h>
#include <asm/uv/uv.h>
#include <asm/sigframe.h>
#include <asm/traps.h>
#include <asm/sev.h>

#include "cpu.h"

u32 elf_hwcap2 __read_mostly;

cpumask_var_t cpu_initialized_mask;
cpumask_var_t cpu_callout_mask;
cpumask_var_t cpu_callin_mask;

cpumask_var_t cpu_sibling_setup_mask;

int smp_num_siblings = 1;

DEFINE_PER_CPU_READ_MOSTLY(u16, cpu_llc_id) = BAD_APICID;

/* Stub: get_llc_id not used in minimal kernel */
u16 get_llc_id(unsigned int cpu)
{
	return 0;
}

DEFINE_PER_CPU_READ_MOSTLY(u16, cpu_l2c_id) = BAD_APICID;

/* Stub: PPIN (Protected Processor Inventory Number) not needed for minimal kernel */
static void ppin_init(struct cpuinfo_x86 *c)
{
}

void __init setup_cpu_local_masks(void)
{
	alloc_bootmem_cpumask_var(&cpu_initialized_mask);
	alloc_bootmem_cpumask_var(&cpu_callin_mask);
	alloc_bootmem_cpumask_var(&cpu_callout_mask);
	alloc_bootmem_cpumask_var(&cpu_sibling_setup_mask);
}

static void default_init(struct cpuinfo_x86 *c)
{
	
	
	if (c->cpuid_level == -1) {
		
		if (c->x86 == 4)
			strcpy(c->x86_model_id, "486");
		else if (c->x86 == 3)
			strcpy(c->x86_model_id, "386");
	}
}

static const struct cpu_dev default_cpu = {
	.c_init		= default_init,
	.c_vendor	= "Unknown",
	.c_x86_vendor	= X86_VENDOR_UNKNOWN,
};

static const struct cpu_dev *this_cpu = &default_cpu;

DEFINE_PER_CPU_PAGE_ALIGNED(struct gdt_page, gdt_page) = { .gdt = {
	[GDT_ENTRY_KERNEL_CS]		= GDT_ENTRY_INIT(0xc09a, 0, 0xfffff),
	[GDT_ENTRY_KERNEL_DS]		= GDT_ENTRY_INIT(0xc092, 0, 0xfffff),
	[GDT_ENTRY_DEFAULT_USER_CS]	= GDT_ENTRY_INIT(0xc0fa, 0, 0xfffff),
	[GDT_ENTRY_DEFAULT_USER_DS]	= GDT_ENTRY_INIT(0xc0f2, 0, 0xfffff),
	
	
	[GDT_ENTRY_PNPBIOS_CS32]	= GDT_ENTRY_INIT(0x409a, 0, 0xffff),
	
	[GDT_ENTRY_PNPBIOS_CS16]	= GDT_ENTRY_INIT(0x009a, 0, 0xffff),
	
	[GDT_ENTRY_PNPBIOS_DS]		= GDT_ENTRY_INIT(0x0092, 0, 0xffff),
	
	[GDT_ENTRY_PNPBIOS_TS1]		= GDT_ENTRY_INIT(0x0092, 0, 0),
	
	[GDT_ENTRY_PNPBIOS_TS2]		= GDT_ENTRY_INIT(0x0092, 0, 0),
	
	
	[GDT_ENTRY_APMBIOS_BASE]	= GDT_ENTRY_INIT(0x409a, 0, 0xffff),
	
	[GDT_ENTRY_APMBIOS_BASE+1]	= GDT_ENTRY_INIT(0x009a, 0, 0xffff),
	
	[GDT_ENTRY_APMBIOS_BASE+2]	= GDT_ENTRY_INIT(0x4092, 0, 0xffff),

	[GDT_ENTRY_ESPFIX_SS]		= GDT_ENTRY_INIT(0xc092, 0, 0xfffff),
	[GDT_ENTRY_PERCPU]		= GDT_ENTRY_INIT(0xc092, 0, 0xfffff),
} };

static int __init x86_noinvpcid_setup(char *s)
{
	
	if (s)
		return -EINVAL;

	if (!boot_cpu_has(X86_FEATURE_INVPCID))
		return 0;

	setup_clear_cpu_cap(X86_FEATURE_INVPCID);
	pr_info("noinvpcid: INVPCID feature disabled\n");
	return 0;
}
early_param("noinvpcid", x86_noinvpcid_setup);

static int cachesize_override = -1;

static int __init cachesize_setup(char *str)
{
	get_option(&str, &cachesize_override);
	return 1;
}
__setup("cachesize=", cachesize_setup);

static inline int flag_is_changeable_p(u32 flag)
{
	u32 f1, f2;

	asm volatile ("pushfl		\n\t"
		      "pushfl		\n\t"
		      "popl %0		\n\t"
		      "movl %0, %1	\n\t"
		      "xorl %2, %0	\n\t"
		      "pushl %0		\n\t"
		      "popfl		\n\t"
		      "pushfl		\n\t"
		      "popl %0		\n\t"
		      "popfl		\n\t"

		      : "=&r" (f1), "=&r" (f2)
		      : "ir" (flag));

	return ((f1^f2) & flag) != 0;
}

int have_cpuid_p(void)
{
	return flag_is_changeable_p(X86_EFLAGS_ID);
}

static void squash_the_stupid_serial_number(struct cpuinfo_x86 *c)
{
	/* Stub: CPU serial number feature not relevant for minimal kernel */
}

static __always_inline void setup_smep(struct cpuinfo_x86 *c)
{
	if (cpu_has(c, X86_FEATURE_SMEP))
		cr4_set_bits(X86_CR4_SMEP);
}

static __always_inline void setup_smap(struct cpuinfo_x86 *c)
{
	unsigned long eflags = native_save_fl();

	BUG_ON(eflags & X86_EFLAGS_AC);

	if (cpu_has(c, X86_FEATURE_SMAP))
		cr4_set_bits(X86_CR4_SMAP);
}

static __always_inline void setup_umip(struct cpuinfo_x86 *c)
{
	
	if (!cpu_feature_enabled(X86_FEATURE_UMIP))
		goto out;

	if (!cpu_has(c, X86_FEATURE_UMIP))
		goto out;

	cr4_set_bits(X86_CR4_UMIP);

	pr_info_once("x86/cpu: User Mode Instruction Prevention (UMIP) activated\n");

	return;

out:
	
	cr4_clear_bits(X86_CR4_UMIP);
}

static const unsigned long cr4_pinned_mask =
	X86_CR4_SMEP | X86_CR4_SMAP | X86_CR4_UMIP |
	X86_CR4_FSGSBASE | X86_CR4_CET;
static DEFINE_STATIC_KEY_FALSE_RO(cr_pinning);
static unsigned long cr4_pinned_bits __ro_after_init;

void native_write_cr0(unsigned long val)
{
	unsigned long bits_missing = 0;

set_register:
	asm volatile("mov %0,%%cr0": "+r" (val) : : "memory");

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
	asm volatile("mov %0,%%cr4": "+r" (val) : : "memory");

	if (static_branch_likely(&cr_pinning)) {
		if (unlikely((val & cr4_pinned_mask) != cr4_pinned_bits)) {
			bits_changed = (val & cr4_pinned_mask) ^ cr4_pinned_bits;
			val = (val & ~cr4_pinned_mask) | cr4_pinned_bits;
			goto set_register;
		}
		
		WARN_ONCE(bits_changed, "pinned CR4 bits changed: 0x%lx!?\n",
			  bits_changed);
	}
}
#if IS_MODULE(CONFIG_LKDTM)
#endif

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

void cr4_init(void)
{
	unsigned long cr4 = __read_cr4();

	if (boot_cpu_has(X86_FEATURE_PCID))
		cr4 |= X86_CR4_PCIDE;
	if (static_branch_likely(&cr_pinning))
		cr4 = (cr4 & ~cr4_pinned_mask) | cr4_pinned_bits;

	__write_cr4(cr4);

	this_cpu_write(cpu_tlbstate.cr4, cr4);
}

static void __init setup_cr_pinning(void)
{
	cr4_pinned_bits = this_cpu_read(cpu_tlbstate.cr4) & cr4_pinned_mask;
	static_key_enable(&cr_pinning.key);
}

static __init int x86_nofsgsbase_setup(char *arg)
{
	
	if (strlen(arg))
		return 0;

	if (!boot_cpu_has(X86_FEATURE_FSGSBASE))
		return 1;

	setup_clear_cpu_cap(X86_FEATURE_FSGSBASE);
	pr_info("FSGSBASE disabled via kernel command line\n");
	return 1;
}
__setup("nofsgsbase", x86_nofsgsbase_setup);

static bool pku_disabled;

static __always_inline void setup_pku(struct cpuinfo_x86 *c)
{
	if (c == &boot_cpu_data) {
		if (pku_disabled || !cpu_feature_enabled(X86_FEATURE_PKU))
			return;
		
		setup_force_cpu_cap(X86_FEATURE_OSPKE);

	} else if (!cpu_feature_enabled(X86_FEATURE_OSPKE)) {
		return;
	}

	cr4_set_bits(X86_CR4_PKE);
	
	pkru_write_default();
}

static __always_inline void setup_cet(struct cpuinfo_x86 *c)
{
	u64 msr = CET_ENDBR_EN;

	if (!HAS_KERNEL_IBT ||
	    !cpu_feature_enabled(X86_FEATURE_IBT))
		return;

	wrmsrl(MSR_IA32_S_CET, msr);
	cr4_set_bits(X86_CR4_CET);

	if (!ibt_selftest()) {
		pr_err("IBT selftest: Failed!\n");
		setup_clear_cpu_cap(X86_FEATURE_IBT);
		return;
	}
}

__noendbr void cet_disable(void)
{
	if (cpu_feature_enabled(X86_FEATURE_IBT))
		wrmsrl(MSR_IA32_S_CET, 0);
}

struct cpuid_dependent_feature {
	u32 feature;
	u32 level;
};

static const struct cpuid_dependent_feature
cpuid_dependent_features[] = {
	{ X86_FEATURE_MWAIT,		0x00000005 },
	{ X86_FEATURE_DCA,		0x00000009 },
	{ X86_FEATURE_XSAVE,		0x0000000d },
	{ 0, 0 }
};

static void filter_cpuid_features(struct cpuinfo_x86 *c, bool warn)
{
	const struct cpuid_dependent_feature *df;

	for (df = cpuid_dependent_features; df->feature; df++) {

		if (!cpu_has(c, df->feature))
			continue;
		
		if (!((s32)df->level < 0 ?
		     (u32)df->level > (u32)c->extended_cpuid_level :
		     (s32)df->level > (s32)c->cpuid_level))
			continue;

		clear_cpu_cap(c, df->feature);
		if (!warn)
			continue;

		pr_warn("CPU: CPU feature " X86_CAP_FMT " disabled, no CPUID level 0x%x\n",
			x86_cap_flag(df->feature), df->level);
	}
}

static const char *table_lookup_model(struct cpuinfo_x86 *c)
{
	const struct legacy_cpu_model_info *info;

	if (c->x86_model >= 16)
		return NULL;	

	if (!this_cpu)
		return NULL;

	info = this_cpu->legacy_models;

	while (info->family) {
		if (info->family == c->x86)
			return info->model_names[c->x86_model];
		info++;
	}
	return NULL;		
}

__u32 cpu_caps_cleared[NCAPINTS + NBUGINTS] __aligned(sizeof(unsigned long));
__u32 cpu_caps_set[NCAPINTS + NBUGINTS] __aligned(sizeof(unsigned long));

void load_percpu_segment(int cpu)
{
	loadsegment(fs, __KERNEL_PERCPU);
}

DEFINE_PER_CPU(struct cpu_entry_area *, cpu_entry_area);

void load_direct_gdt(int cpu)
{
	struct desc_ptr gdt_descr;

	gdt_descr.address = (long)get_cpu_gdt_rw(cpu);
	gdt_descr.size = GDT_SIZE - 1;
	load_gdt(&gdt_descr);
}

void load_fixmap_gdt(int cpu)
{
	struct desc_ptr gdt_descr;

	gdt_descr.address = (long)get_cpu_gdt_ro(cpu);
	gdt_descr.size = GDT_SIZE - 1;
	load_gdt(&gdt_descr);
}

void switch_to_new_gdt(int cpu)
{
	
	load_direct_gdt(cpu);
	
	load_percpu_segment(cpu);
}

static const struct cpu_dev *cpu_devs[X86_VENDOR_NUM] = {};

static void get_model_name(struct cpuinfo_x86 *c)
{
	unsigned int *v;
	char *p, *q, *s;

	if (c->extended_cpuid_level < 0x80000004)
		return;

	v = (unsigned int *)c->x86_model_id;
	cpuid(0x80000002, &v[0], &v[1], &v[2], &v[3]);
	cpuid(0x80000003, &v[4], &v[5], &v[6], &v[7]);
	cpuid(0x80000004, &v[8], &v[9], &v[10], &v[11]);
	c->x86_model_id[48] = 0;

	p = q = s = &c->x86_model_id[0];

	while (*p == ' ')
		p++;

	while (*p) {
		
		if (!isspace(*p))
			s = q;

		*q++ = *p++;
	}

	*(s + 1) = '\0';
}

void detect_num_cpu_cores(struct cpuinfo_x86 *c)
{
	unsigned int eax, ebx, ecx, edx;

	c->x86_max_cores = 1;
	if (!IS_ENABLED(CONFIG_SMP) || c->cpuid_level < 4)
		return;

	cpuid_count(4, 0, &eax, &ebx, &ecx, &edx);
	if (eax & 0x1f)
		c->x86_max_cores = (eax >> 26) + 1;
}

void cpu_detect_cache_sizes(struct cpuinfo_x86 *c)
{
	unsigned int n, dummy, ebx, ecx, edx, l2size;

	n = c->extended_cpuid_level;

	if (n >= 0x80000005) {
		cpuid(0x80000005, &dummy, &ebx, &ecx, &edx);
		c->x86_cache_size = (ecx>>24) + (edx>>24);
	}

	if (n < 0x80000006)	
		return;

	cpuid(0x80000006, &dummy, &ebx, &ecx, &edx);
	l2size = ecx >> 16;

	if (this_cpu->legacy_cache_size)
		l2size = this_cpu->legacy_cache_size(c, l2size);

	if (cachesize_override != -1)
		l2size = cachesize_override;

	if (l2size == 0)
		return;		

	c->x86_cache_size = l2size;
}

u16 __read_mostly tlb_lli_4k[NR_INFO];
u16 __read_mostly tlb_lli_2m[NR_INFO];
u16 __read_mostly tlb_lli_4m[NR_INFO];
u16 __read_mostly tlb_lld_4k[NR_INFO];
u16 __read_mostly tlb_lld_2m[NR_INFO];
u16 __read_mostly tlb_lld_4m[NR_INFO];
u16 __read_mostly tlb_lld_1g[NR_INFO];

static void cpu_detect_tlb(struct cpuinfo_x86 *c)
{
	if (this_cpu->c_detect_tlb)
		this_cpu->c_detect_tlb(c);

	pr_info("Last level iTLB entries: 4KB %d, 2MB %d, 4MB %d\n",
		tlb_lli_4k[ENTRIES], tlb_lli_2m[ENTRIES],
		tlb_lli_4m[ENTRIES]);

	pr_info("Last level dTLB entries: 4KB %d, 2MB %d, 4MB %d, 1GB %d\n",
		tlb_lld_4k[ENTRIES], tlb_lld_2m[ENTRIES],
		tlb_lld_4m[ENTRIES], tlb_lld_1g[ENTRIES]);
}

int detect_ht_early(struct cpuinfo_x86 *c)
{
	return 0;
}

void detect_ht(struct cpuinfo_x86 *c)
{
}

static void get_cpu_vendor(struct cpuinfo_x86 *c)
{
	char *v = c->x86_vendor_id;
	int i;

	for (i = 0; i < X86_VENDOR_NUM; i++) {
		if (!cpu_devs[i])
			break;

		if (!strcmp(v, cpu_devs[i]->c_ident[0]) ||
		    (cpu_devs[i]->c_ident[1] &&
		     !strcmp(v, cpu_devs[i]->c_ident[1]))) {

			this_cpu = cpu_devs[i];
			c->x86_vendor = this_cpu->c_x86_vendor;
			return;
		}
	}

	pr_err_once("CPU: vendor_id '%s' unknown, using generic init.\n" \
		    "CPU: Your system may be unstable.\n", v);

	c->x86_vendor = X86_VENDOR_UNKNOWN;
	this_cpu = &default_cpu;
}

void cpu_detect(struct cpuinfo_x86 *c)
{
	
	cpuid(0x00000000, (unsigned int *)&c->cpuid_level,
	      (unsigned int *)&c->x86_vendor_id[0],
	      (unsigned int *)&c->x86_vendor_id[8],
	      (unsigned int *)&c->x86_vendor_id[4]);

	c->x86 = 4;
	
	if (c->cpuid_level >= 0x00000001) {
		u32 junk, tfms, cap0, misc;

		cpuid(0x00000001, &tfms, &misc, &junk, &cap0);
		c->x86		= x86_family(tfms);
		c->x86_model	= x86_model(tfms);
		c->x86_stepping	= x86_stepping(tfms);

		if (cap0 & (1<<19)) {
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

static void init_speculation_control(struct cpuinfo_x86 *c)
{
	
	if (cpu_has(c, X86_FEATURE_SPEC_CTRL)) {
		set_cpu_cap(c, X86_FEATURE_IBRS);
		set_cpu_cap(c, X86_FEATURE_IBPB);
		set_cpu_cap(c, X86_FEATURE_MSR_SPEC_CTRL);
	}

	if (cpu_has(c, X86_FEATURE_INTEL_STIBP))
		set_cpu_cap(c, X86_FEATURE_STIBP);

	if (cpu_has(c, X86_FEATURE_SPEC_CTRL_SSBD) ||
	    cpu_has(c, X86_FEATURE_VIRT_SSBD))
		set_cpu_cap(c, X86_FEATURE_SSBD);

	if (cpu_has(c, X86_FEATURE_AMD_IBRS)) {
		set_cpu_cap(c, X86_FEATURE_IBRS);
		set_cpu_cap(c, X86_FEATURE_MSR_SPEC_CTRL);
	}

	if (cpu_has(c, X86_FEATURE_AMD_IBPB))
		set_cpu_cap(c, X86_FEATURE_IBPB);

	if (cpu_has(c, X86_FEATURE_AMD_STIBP)) {
		set_cpu_cap(c, X86_FEATURE_STIBP);
		set_cpu_cap(c, X86_FEATURE_MSR_SPEC_CTRL);
	}

	if (cpu_has(c, X86_FEATURE_AMD_SSBD)) {
		set_cpu_cap(c, X86_FEATURE_SSBD);
		set_cpu_cap(c, X86_FEATURE_MSR_SPEC_CTRL);
		clear_cpu_cap(c, X86_FEATURE_VIRT_SSBD);
	}
}

void get_cpu_cap(struct cpuinfo_x86 *c)
{
	u32 eax, ebx, ecx, edx;

	if (c->cpuid_level >= 0x00000001) {
		cpuid(0x00000001, &eax, &ebx, &ecx, &edx);

		c->x86_capability[CPUID_1_ECX] = ecx;
		c->x86_capability[CPUID_1_EDX] = edx;
	}

	if (c->cpuid_level >= 0x00000006)
		c->x86_capability[CPUID_6_EAX] = cpuid_eax(0x00000006);

	if (c->cpuid_level >= 0x00000007) {
		cpuid_count(0x00000007, 0, &eax, &ebx, &ecx, &edx);
		c->x86_capability[CPUID_7_0_EBX] = ebx;
		c->x86_capability[CPUID_7_ECX] = ecx;
		c->x86_capability[CPUID_7_EDX] = edx;

		if (eax >= 1) {
			cpuid_count(0x00000007, 1, &eax, &ebx, &ecx, &edx);
			c->x86_capability[CPUID_7_1_EAX] = eax;
		}
	}

	if (c->cpuid_level >= 0x0000000d) {
		cpuid_count(0x0000000d, 1, &eax, &ebx, &ecx, &edx);

		c->x86_capability[CPUID_D_1_EAX] = eax;
	}

	eax = cpuid_eax(0x80000000);
	c->extended_cpuid_level = eax;

	if ((eax & 0xffff0000) == 0x80000000) {
		if (eax >= 0x80000001) {
			cpuid(0x80000001, &eax, &ebx, &ecx, &edx);

			c->x86_capability[CPUID_8000_0001_ECX] = ecx;
			c->x86_capability[CPUID_8000_0001_EDX] = edx;
		}
	}

	if (c->extended_cpuid_level >= 0x80000007) {
		cpuid(0x80000007, &eax, &ebx, &ecx, &edx);

		c->x86_capability[CPUID_8000_0007_EBX] = ebx;
		c->x86_power = edx;
	}

	if (c->extended_cpuid_level >= 0x80000008) {
		cpuid(0x80000008, &eax, &ebx, &ecx, &edx);
		c->x86_capability[CPUID_8000_0008_EBX] = ebx;
	}

	if (c->extended_cpuid_level >= 0x8000000a)
		c->x86_capability[CPUID_8000_000A_EDX] = cpuid_edx(0x8000000a);

	if (c->extended_cpuid_level >= 0x8000001f)
		c->x86_capability[CPUID_8000_001F_EAX] = cpuid_eax(0x8000001f);

	init_scattered_cpuid_features(c);
	init_speculation_control(c);

	apply_forced_caps(c);
}

void get_cpu_address_sizes(struct cpuinfo_x86 *c)
{
	u32 eax, ebx, ecx, edx;

	if (c->extended_cpuid_level >= 0x80000008) {
		cpuid(0x80000008, &eax, &ebx, &ecx, &edx);

		c->x86_virt_bits = (eax >> 8) & 0xff;
		c->x86_phys_bits = eax & 0xff;
	}
	else if (cpu_has(c, X86_FEATURE_PAE) || cpu_has(c, X86_FEATURE_PSE36))
		c->x86_phys_bits = 36;
	c->x86_cache_bits = c->x86_phys_bits;
}

static void identify_cpu_without_cpuid(struct cpuinfo_x86 *c)
{
	int i;

	if (flag_is_changeable_p(X86_EFLAGS_AC))
		c->x86 = 4;
	else
		c->x86 = 3;

	for (i = 0; i < X86_VENDOR_NUM; i++)
		if (cpu_devs[i] && cpu_devs[i]->c_identify) {
			c->x86_vendor_id[0] = 0;
			cpu_devs[i]->c_identify(c);
			if (c->x86_vendor_id[0]) {
				get_cpu_vendor(c);
				break;
			}
		}
}

#define NO_SPECULATION		BIT(0)
#define NO_MELTDOWN		BIT(1)
#define NO_SSB			BIT(2)
#define NO_L1TF			BIT(3)
#define NO_MDS			BIT(4)
#define MSBDS_ONLY		BIT(5)
#define NO_SWAPGS		BIT(6)
#define NO_ITLB_MULTIHIT	BIT(7)
#define NO_SPECTRE_V2		BIT(8)

#define VULNWL(vendor, family, model, whitelist)	\
	X86_MATCH_VENDOR_FAM_MODEL(vendor, family, model, whitelist)

#define VULNWL_INTEL(model, whitelist)		\
	VULNWL(INTEL, 6, INTEL_FAM6_##model, whitelist)

#define VULNWL_AMD(family, whitelist)		\
	VULNWL(AMD, family, X86_MODEL_ANY, whitelist)

#define VULNWL_HYGON(family, whitelist)		\
	VULNWL(HYGON, family, X86_MODEL_ANY, whitelist)

static const __initconst struct x86_cpu_id cpu_vuln_whitelist[] = {
	VULNWL(ANY,	4, X86_MODEL_ANY,	NO_SPECULATION),
	VULNWL(CENTAUR,	5, X86_MODEL_ANY,	NO_SPECULATION),
	VULNWL(INTEL,	5, X86_MODEL_ANY,	NO_SPECULATION),
	VULNWL(NSC,	5, X86_MODEL_ANY,	NO_SPECULATION),
	VULNWL(VORTEX,	5, X86_MODEL_ANY,	NO_SPECULATION),
	VULNWL(VORTEX,	6, X86_MODEL_ANY,	NO_SPECULATION),

	VULNWL_INTEL(ATOM_SALTWELL,		NO_SPECULATION | NO_ITLB_MULTIHIT),
	VULNWL_INTEL(ATOM_SALTWELL_TABLET,	NO_SPECULATION | NO_ITLB_MULTIHIT),
	VULNWL_INTEL(ATOM_SALTWELL_MID,		NO_SPECULATION | NO_ITLB_MULTIHIT),
	VULNWL_INTEL(ATOM_BONNELL,		NO_SPECULATION | NO_ITLB_MULTIHIT),
	VULNWL_INTEL(ATOM_BONNELL_MID,		NO_SPECULATION | NO_ITLB_MULTIHIT),

	VULNWL_INTEL(ATOM_SILVERMONT,		NO_SSB | NO_L1TF | MSBDS_ONLY | NO_SWAPGS | NO_ITLB_MULTIHIT),
	VULNWL_INTEL(ATOM_SILVERMONT_D,		NO_SSB | NO_L1TF | MSBDS_ONLY | NO_SWAPGS | NO_ITLB_MULTIHIT),
	VULNWL_INTEL(ATOM_SILVERMONT_MID,	NO_SSB | NO_L1TF | MSBDS_ONLY | NO_SWAPGS | NO_ITLB_MULTIHIT),
	VULNWL_INTEL(ATOM_AIRMONT,		NO_SSB | NO_L1TF | MSBDS_ONLY | NO_SWAPGS | NO_ITLB_MULTIHIT),
	VULNWL_INTEL(XEON_PHI_KNL,		NO_SSB | NO_L1TF | MSBDS_ONLY | NO_SWAPGS | NO_ITLB_MULTIHIT),
	VULNWL_INTEL(XEON_PHI_KNM,		NO_SSB | NO_L1TF | MSBDS_ONLY | NO_SWAPGS | NO_ITLB_MULTIHIT),

	VULNWL_INTEL(CORE_YONAH,		NO_SSB),

	VULNWL_INTEL(ATOM_AIRMONT_MID,		NO_L1TF | MSBDS_ONLY | NO_SWAPGS | NO_ITLB_MULTIHIT),
	VULNWL_INTEL(ATOM_AIRMONT_NP,		NO_L1TF | NO_SWAPGS | NO_ITLB_MULTIHIT),

	VULNWL_INTEL(ATOM_GOLDMONT,		NO_MDS | NO_L1TF | NO_SWAPGS | NO_ITLB_MULTIHIT),
	VULNWL_INTEL(ATOM_GOLDMONT_D,		NO_MDS | NO_L1TF | NO_SWAPGS | NO_ITLB_MULTIHIT),
	VULNWL_INTEL(ATOM_GOLDMONT_PLUS,	NO_MDS | NO_L1TF | NO_SWAPGS | NO_ITLB_MULTIHIT),

	VULNWL_INTEL(ATOM_TREMONT_D,		NO_ITLB_MULTIHIT),

	VULNWL_AMD(0x0f,	NO_MELTDOWN | NO_SSB | NO_L1TF | NO_MDS | NO_SWAPGS | NO_ITLB_MULTIHIT),
	VULNWL_AMD(0x10,	NO_MELTDOWN | NO_SSB | NO_L1TF | NO_MDS | NO_SWAPGS | NO_ITLB_MULTIHIT),
	VULNWL_AMD(0x11,	NO_MELTDOWN | NO_SSB | NO_L1TF | NO_MDS | NO_SWAPGS | NO_ITLB_MULTIHIT),
	VULNWL_AMD(0x12,	NO_MELTDOWN | NO_SSB | NO_L1TF | NO_MDS | NO_SWAPGS | NO_ITLB_MULTIHIT),

	VULNWL_AMD(X86_FAMILY_ANY,	NO_MELTDOWN | NO_L1TF | NO_MDS | NO_SWAPGS | NO_ITLB_MULTIHIT),
	VULNWL_HYGON(X86_FAMILY_ANY,	NO_MELTDOWN | NO_L1TF | NO_MDS | NO_SWAPGS | NO_ITLB_MULTIHIT),

	VULNWL(CENTAUR,	7, X86_MODEL_ANY,	NO_SPECTRE_V2 | NO_SWAPGS),
	VULNWL(ZHAOXIN,	7, X86_MODEL_ANY,	NO_SPECTRE_V2 | NO_SWAPGS),
	{}
};

#define VULNBL(vendor, family, model, blacklist)	\
	X86_MATCH_VENDOR_FAM_MODEL(vendor, family, model, blacklist)

#define VULNBL_INTEL_STEPPINGS(model, steppings, issues)		   \
	X86_MATCH_VENDOR_FAM_MODEL_STEPPINGS_FEATURE(INTEL, 6,		   \
					    INTEL_FAM6_##model, steppings, \
					    X86_FEATURE_ANY, issues)

#define VULNBL_AMD(family, blacklist)		\
	VULNBL(AMD, family, X86_MODEL_ANY, blacklist)

#define VULNBL_HYGON(family, blacklist)		\
	VULNBL(HYGON, family, X86_MODEL_ANY, blacklist)

#define SRBDS		BIT(0)

#define MMIO		BIT(1)

#define MMIO_SBDS	BIT(2)

#define RETBLEED	BIT(3)

static const struct x86_cpu_id cpu_vuln_blacklist[] __initconst = {
	VULNBL_INTEL_STEPPINGS(IVYBRIDGE,	X86_STEPPING_ANY,		SRBDS),
	VULNBL_INTEL_STEPPINGS(HASWELL,		X86_STEPPING_ANY,		SRBDS),
	VULNBL_INTEL_STEPPINGS(HASWELL_L,	X86_STEPPING_ANY,		SRBDS),
	VULNBL_INTEL_STEPPINGS(HASWELL_G,	X86_STEPPING_ANY,		SRBDS),
	VULNBL_INTEL_STEPPINGS(HASWELL_X,	X86_STEPPING_ANY,		MMIO),
	VULNBL_INTEL_STEPPINGS(BROADWELL_D,	X86_STEPPING_ANY,		MMIO),
	VULNBL_INTEL_STEPPINGS(BROADWELL_G,	X86_STEPPING_ANY,		SRBDS),
	VULNBL_INTEL_STEPPINGS(BROADWELL_X,	X86_STEPPING_ANY,		MMIO),
	VULNBL_INTEL_STEPPINGS(BROADWELL,	X86_STEPPING_ANY,		SRBDS),
	VULNBL_INTEL_STEPPINGS(SKYLAKE_L,	X86_STEPPING_ANY,		SRBDS | MMIO | RETBLEED),
	VULNBL_INTEL_STEPPINGS(SKYLAKE_X,	X86_STEPPING_ANY,		MMIO | RETBLEED),
	VULNBL_INTEL_STEPPINGS(SKYLAKE,		X86_STEPPING_ANY,		SRBDS | MMIO | RETBLEED),
	VULNBL_INTEL_STEPPINGS(KABYLAKE_L,	X86_STEPPING_ANY,		SRBDS | MMIO | RETBLEED),
	VULNBL_INTEL_STEPPINGS(KABYLAKE,	X86_STEPPING_ANY,		SRBDS | MMIO | RETBLEED),
	VULNBL_INTEL_STEPPINGS(CANNONLAKE_L,	X86_STEPPING_ANY,		RETBLEED),
	VULNBL_INTEL_STEPPINGS(ICELAKE_L,	X86_STEPPING_ANY,		MMIO | MMIO_SBDS | RETBLEED),
	VULNBL_INTEL_STEPPINGS(ICELAKE_D,	X86_STEPPING_ANY,		MMIO),
	VULNBL_INTEL_STEPPINGS(ICELAKE_X,	X86_STEPPING_ANY,		MMIO),
	VULNBL_INTEL_STEPPINGS(COMETLAKE,	X86_STEPPING_ANY,		MMIO | MMIO_SBDS | RETBLEED),
	VULNBL_INTEL_STEPPINGS(COMETLAKE_L,	X86_STEPPINGS(0x0, 0x0),	MMIO | RETBLEED),
	VULNBL_INTEL_STEPPINGS(COMETLAKE_L,	X86_STEPPING_ANY,		MMIO | MMIO_SBDS | RETBLEED),
	VULNBL_INTEL_STEPPINGS(LAKEFIELD,	X86_STEPPING_ANY,		MMIO | MMIO_SBDS | RETBLEED),
	VULNBL_INTEL_STEPPINGS(ROCKETLAKE,	X86_STEPPING_ANY,		MMIO | RETBLEED),
	VULNBL_INTEL_STEPPINGS(ATOM_TREMONT,	X86_STEPPING_ANY,		MMIO | MMIO_SBDS),
	VULNBL_INTEL_STEPPINGS(ATOM_TREMONT_D,	X86_STEPPING_ANY,		MMIO),
	VULNBL_INTEL_STEPPINGS(ATOM_TREMONT_L,	X86_STEPPING_ANY,		MMIO | MMIO_SBDS),

	VULNBL_AMD(0x15, RETBLEED),
	VULNBL_AMD(0x16, RETBLEED),
	VULNBL_AMD(0x17, RETBLEED),
	VULNBL_HYGON(0x18, RETBLEED),
	{}
};

static bool __init cpu_matches(const struct x86_cpu_id *table, unsigned long which)
{
	const struct x86_cpu_id *m = x86_match_cpu(table);

	return m && !!(m->driver_data & which);
}

u64 x86_read_arch_cap_msr(void)
{
	u64 ia32_cap = 0;

	if (boot_cpu_has(X86_FEATURE_ARCH_CAPABILITIES))
		rdmsrl(MSR_IA32_ARCH_CAPABILITIES, ia32_cap);

	return ia32_cap;
}

static bool arch_cap_mmio_immune(u64 ia32_cap)
{
	return (ia32_cap & ARCH_CAP_FBSDP_NO &&
		ia32_cap & ARCH_CAP_PSDP_NO &&
		ia32_cap & ARCH_CAP_SBDR_SSDP_NO);
}

static void __init cpu_set_bug_bits(struct cpuinfo_x86 *c)
{
	u64 ia32_cap = x86_read_arch_cap_msr();

	if (!cpu_matches(cpu_vuln_whitelist, NO_ITLB_MULTIHIT) &&
	    !(ia32_cap & ARCH_CAP_PSCHANGE_MC_NO))
		setup_force_cpu_bug(X86_BUG_ITLB_MULTIHIT);

	if (cpu_matches(cpu_vuln_whitelist, NO_SPECULATION))
		return;

	setup_force_cpu_bug(X86_BUG_SPECTRE_V1);

	if (!cpu_matches(cpu_vuln_whitelist, NO_SPECTRE_V2))
		setup_force_cpu_bug(X86_BUG_SPECTRE_V2);

	if (!cpu_matches(cpu_vuln_whitelist, NO_SSB) &&
	    !(ia32_cap & ARCH_CAP_SSB_NO) &&
	   !cpu_has(c, X86_FEATURE_AMD_SSB_NO))
		setup_force_cpu_bug(X86_BUG_SPEC_STORE_BYPASS);

	if (ia32_cap & ARCH_CAP_IBRS_ALL)
		setup_force_cpu_cap(X86_FEATURE_IBRS_ENHANCED);

	if (!cpu_matches(cpu_vuln_whitelist, NO_MDS) &&
	    !(ia32_cap & ARCH_CAP_MDS_NO)) {
		setup_force_cpu_bug(X86_BUG_MDS);
		if (cpu_matches(cpu_vuln_whitelist, MSBDS_ONLY))
			setup_force_cpu_bug(X86_BUG_MSBDS_ONLY);
	}

	if (!cpu_matches(cpu_vuln_whitelist, NO_SWAPGS))
		setup_force_cpu_bug(X86_BUG_SWAPGS);

	if (!(ia32_cap & ARCH_CAP_TAA_NO) &&
	    (cpu_has(c, X86_FEATURE_RTM) ||
	     (ia32_cap & ARCH_CAP_TSX_CTRL_MSR)))
		setup_force_cpu_bug(X86_BUG_TAA);

	if ((cpu_has(c, X86_FEATURE_RDRAND) ||
	     cpu_has(c, X86_FEATURE_RDSEED)) &&
	    cpu_matches(cpu_vuln_blacklist, SRBDS | MMIO_SBDS))
		    setup_force_cpu_bug(X86_BUG_SRBDS);

	if (cpu_matches(cpu_vuln_blacklist, MMIO) &&
	    !arch_cap_mmio_immune(ia32_cap))
		setup_force_cpu_bug(X86_BUG_MMIO_STALE_DATA);

	if (!cpu_has(c, X86_FEATURE_BTC_NO)) {
		if (cpu_matches(cpu_vuln_blacklist, RETBLEED) || (ia32_cap & ARCH_CAP_RSBA))
			setup_force_cpu_bug(X86_BUG_RETBLEED);
	}

	if (cpu_matches(cpu_vuln_whitelist, NO_MELTDOWN))
		return;

	if (ia32_cap & ARCH_CAP_RDCL_NO)
		return;

	setup_force_cpu_bug(X86_BUG_CPU_MELTDOWN);

	if (cpu_matches(cpu_vuln_whitelist, NO_L1TF))
		return;

	setup_force_cpu_bug(X86_BUG_L1TF);
}

static void detect_nopl(void)
{
	setup_clear_cpu_cap(X86_FEATURE_NOPL);
}

static void __init cpu_parse_early_param(void)
{
	char arg[128];
	char *argptr = arg, *opt;
	int arglen, taint = 0;

	if (cmdline_find_option_bool(boot_command_line, "no387"))
		pr_err("Option 'no387' required CONFIG_MATH_EMULATION enabled.\n");

	if (cmdline_find_option_bool(boot_command_line, "nofxsr"))
		setup_clear_cpu_cap(X86_FEATURE_FXSR);

	if (cmdline_find_option_bool(boot_command_line, "noxsave"))
		setup_clear_cpu_cap(X86_FEATURE_XSAVE);

	if (cmdline_find_option_bool(boot_command_line, "noxsaveopt"))
		setup_clear_cpu_cap(X86_FEATURE_XSAVEOPT);

	if (cmdline_find_option_bool(boot_command_line, "noxsaves"))
		setup_clear_cpu_cap(X86_FEATURE_XSAVES);

	arglen = cmdline_find_option(boot_command_line, "clearcpuid", arg, sizeof(arg));
	if (arglen <= 0)
		return;

	pr_info("Clearing CPUID bits:");

	while (argptr) {
		bool found __maybe_unused = false;
		unsigned int bit;

		opt = strsep(&argptr, ",");

		if (!kstrtouint(opt, 10, &bit)) {
			if (bit < NCAPINTS * 32) {

					pr_cont(" " X86_CAP_FMT, x86_cap_flag(bit));

				setup_clear_cpu_cap(bit);
				taint++;
			}
			
			continue;
		}

	}
	pr_cont("\n");

	if (taint)
		add_taint(TAINT_CPU_OUT_OF_SPEC, LOCKDEP_STILL_OK);
}

static void __init early_identify_cpu(struct cpuinfo_x86 *c)
{
	c->x86_clflush_size = 32;
	c->x86_phys_bits = 32;
	c->x86_virt_bits = 32;
	c->x86_cache_alignment = c->x86_clflush_size;

	memset(&c->x86_capability, 0, sizeof(c->x86_capability));
	c->extended_cpuid_level = 0;

	if (!have_cpuid_p())
		identify_cpu_without_cpuid(c);

	if (have_cpuid_p()) {
		cpu_detect(c);
		get_cpu_vendor(c);
		get_cpu_cap(c);
		get_cpu_address_sizes(c);
		setup_force_cpu_cap(X86_FEATURE_CPUID);
		cpu_parse_early_param();

		if (this_cpu->c_early_init)
			this_cpu->c_early_init(c);

		c->cpu_index = 0;
		filter_cpuid_features(c, false);

		if (this_cpu->c_bsp_init)
			this_cpu->c_bsp_init(c);
	} else {
		setup_clear_cpu_cap(X86_FEATURE_CPUID);
	}

	setup_force_cpu_cap(X86_FEATURE_ALWAYS);

	cpu_set_bug_bits(c);

	sld_setup(c);

	fpu__init_system(c);

	init_sigframe_size();

	setup_clear_cpu_cap(X86_FEATURE_PCID);

	if (!pgtable_l5_enabled())
		setup_clear_cpu_cap(X86_FEATURE_LA57);

	detect_nopl();
}

void __init early_cpu_init(void)
{
	const struct cpu_dev *const *cdev;
	int count = 0;

	for (cdev = __x86_cpu_dev_start; cdev < __x86_cpu_dev_end; cdev++) {
		const struct cpu_dev *cpudev = *cdev;

		if (count >= X86_VENDOR_NUM)
			break;
		cpu_devs[count] = cpudev;
		count++;

	}
	early_identify_cpu(&boot_cpu_data);
}

/* Stubbed - not used in minimal kernel */
void check_null_seg_clears_base(struct cpuinfo_x86 *c)
{
}

static void generic_identify(struct cpuinfo_x86 *c)
{
	c->extended_cpuid_level = 0;

	if (!have_cpuid_p())
		identify_cpu_without_cpuid(c);

	if (!have_cpuid_p())
		return;

	cpu_detect(c);

	get_cpu_vendor(c);

	get_cpu_cap(c);

	get_cpu_address_sizes(c);

	if (c->cpuid_level >= 0x00000001) {
		c->initial_apicid = (cpuid_ebx(1) >> 24) & 0xFF;
		c->apicid = c->initial_apicid;
		c->phys_proc_id = c->initial_apicid;
	}

	get_model_name(c); 

	set_cpu_bug(c, X86_BUG_ESPFIX);
}

static void validate_apic_and_package_id(struct cpuinfo_x86 *c)
{
	c->logical_proc_id = 0;
}

static void identify_cpu(struct cpuinfo_x86 *c)
{
	int i;

	c->loops_per_jiffy = loops_per_jiffy;
	c->x86_cache_size = 0;
	c->x86_vendor = X86_VENDOR_UNKNOWN;
	c->x86_model = c->x86_stepping = 0;	
	c->x86_vendor_id[0] = '\0'; 
	c->x86_model_id[0] = '\0';  
	c->x86_max_cores = 1;
	c->x86_coreid_bits = 0;
	c->cu_id = 0xff;
	c->cpuid_level = -1;	
	c->x86_clflush_size = 32;
	c->x86_phys_bits = 32;
	c->x86_virt_bits = 32;
	c->x86_cache_alignment = c->x86_clflush_size;
	memset(&c->x86_capability, 0, sizeof(c->x86_capability));

	generic_identify(c);

	if (this_cpu->c_identify)
		this_cpu->c_identify(c);

	apply_forced_caps(c);

	if (this_cpu->c_init)
		this_cpu->c_init(c);

	squash_the_stupid_serial_number(c);

	setup_smep(c);
	setup_smap(c);
	setup_umip(c);

	if (cpu_has(c, X86_FEATURE_FSGSBASE)) {
		cr4_set_bits(X86_CR4_FSGSBASE);
		elf_hwcap2 |= HWCAP2_FSGSBASE;
	}

	filter_cpuid_features(c, true);

	if (!c->x86_model_id[0]) {
		const char *p;
		p = table_lookup_model(c);
		if (p)
			strcpy(c->x86_model_id, p);
		else
			
			sprintf(c->x86_model_id, "%02x/%02x",
				c->x86, c->x86_model);
	}

	x86_init_rdrand(c);
	setup_pku(c);
	setup_cet(c);

	apply_forced_caps(c);

	if (c != &boot_cpu_data) {
		
		for (i = 0; i < NCAPINTS; i++)
			boot_cpu_data.x86_capability[i] &= c->x86_capability[i];

		for (i = NCAPINTS; i < NCAPINTS + NBUGINTS; i++)
			c->x86_capability[i] |= boot_cpu_data.x86_capability[i];
	}

	ppin_init(c);

	mcheck_cpu_init(c);

	select_idle_routine(c);

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
	wrmsr(MSR_IA32_SYSENTER_ESP, (unsigned long)(cpu_entry_stack(cpu) + 1), 0);
	wrmsr(MSR_IA32_SYSENTER_EIP, (unsigned long)entry_SYSENTER_32, 0);

	put_cpu();
}

void __init identify_boot_cpu(void)
{
	identify_cpu(&boot_cpu_data);
	if (HAS_KERNEL_IBT && cpu_feature_enabled(X86_FEATURE_IBT))
		pr_info("CET detected: Indirect Branch Tracking enabled\n");
	sysenter_setup();
	enable_sep_cpu();
	cpu_detect_tlb(&boot_cpu_data);
	setup_cr_pinning();

	tsx_init();
}

/* Stubbed - no SMP in minimal kernel */
void identify_secondary_cpu(struct cpuinfo_x86 *c)
{
}

void print_cpu_info(struct cpuinfo_x86 *c)
{
	
}

static __init int setup_clearcpuid(char *arg)
{
	return 1;
}
__setup("clearcpuid=", setup_clearcpuid);

DEFINE_PER_CPU(struct task_struct *, current_task) = &init_task;
DEFINE_PER_CPU(int, __preempt_count) = INIT_PREEMPT_COUNT;

DEFINE_PER_CPU(unsigned long, cpu_current_top_of_stack) =
	(unsigned long)&init_thread_union + THREAD_SIZE;

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

static void wait_for_master_cpu(int cpu)
{
}

static inline void setup_getcpu(int cpu) { }

static inline void ucode_cpu_init(int cpu)
{
	show_ucode_info_early();
}

static inline void tss_setup_ist(struct tss_struct *tss) { }

static inline void tss_setup_io_bitmap(struct tss_struct *tss)
{
	tss->x86_tss.io_bitmap_base = IO_BITMAP_OFFSET_INVALID;

}

void cpu_init_exception_handling(void)
{
	struct tss_struct *tss = this_cpu_ptr(&cpu_tss_rw);
	int cpu = raw_smp_processor_id();

	setup_getcpu(cpu);

	tss_setup_ist(tss);
	tss_setup_io_bitmap(tss);
	set_tss_desc(cpu, &get_cpu_entry_area(cpu)->tss.x86_tss);

	load_TR_desc();

	setup_ghcb();

	load_current_idt();
}

void cpu_init(void)
{
	struct task_struct *cur = current;
	int cpu = raw_smp_processor_id();

	wait_for_master_cpu(cpu);

	ucode_cpu_init(cpu);

	if (IS_ENABLED(CONFIG_X86_64) || cpu_feature_enabled(X86_FEATURE_VME) ||
	    boot_cpu_has(X86_FEATURE_TSC) || boot_cpu_has(X86_FEATURE_DE))
		cr4_clear_bits(X86_CR4_VME|X86_CR4_PVI|X86_CR4_TSD|X86_CR4_DE);

	switch_to_new_gdt(cpu);

	if (IS_ENABLED(CONFIG_X86_64)) {
		loadsegment(fs, 0);
		memset(cur->thread.tls_array, 0, GDT_ENTRY_TLS_ENTRIES * 8);
		syscall_init();

		wrmsrl(MSR_FS_BASE, 0);
		wrmsrl(MSR_KERNEL_GS_BASE, 0);
		barrier();

		x2apic_setup();
	}

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

	fpu__init_cpu();

	if (is_uv_system())
		uv_cpu_init();

	load_fixmap_gdt(cpu);
}

/* Stub: arch_smt_update not used in minimal kernel */
void arch_smt_update(void)
{
}
