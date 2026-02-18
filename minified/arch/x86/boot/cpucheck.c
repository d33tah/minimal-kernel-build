
#ifdef _SETUP
#include "boot.h"
#endif
#define INTEL_FAM6_XEON_PHI_KNL 0x57
#include <asm/msr-index.h>
#include "string.h"
struct msr {
	union {
		struct {
			u32 l;
			u32 h;
		};
		u64 q;
	};
};
static inline void boot_rdmsr(unsigned int reg, struct msr *m)
{
	asm volatile("rdmsr" : "=a"(m->l), "=d"(m->h) : "c"(reg));
}
static inline void boot_wrmsr(unsigned int reg, const struct msr *m)
{
	asm volatile("wrmsr" : : "c"(reg), "a"(m->l), "d"(m->h) : "memory");
}
/* end msr.h */

static u32 err_flags[NCAPINTS];

static const int req_level = CONFIG_X86_MINIMUM_CPU_FAMILY;

static const u32 req_flags[NCAPINTS] = {
	REQUIRED_MASK0,
	REQUIRED_MASK1,
	0,
	0,
	REQUIRED_MASK4,
	0,
	REQUIRED_MASK6,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	REQUIRED_MASK16,
};

#define A32(a, b, c, d) (((d) << 24) + ((c) << 16) + ((b) << 8) + (a))

static int is_intel(void)
{
	return cpu_vendor[0] == A32('G', 'e', 'n', 'u') &&
	       cpu_vendor[1] == A32('i', 'n', 'e', 'I') &&
	       cpu_vendor[2] == A32('n', 't', 'e', 'l');
}

static int check_cpuflags(void)
{
	u32 err;
	int i;

	err = 0;
	for (i = 0; i < NCAPINTS; i++) {
		err_flags[i] = req_flags[i] & ~cpu.flags[i];
		if (err_flags[i])
			err |= 1 << i;
	}

	return err;
}

int check_cpu(int *cpu_level_ptr, int *req_level_ptr, u32 **err_flags_ptr)
{
	int err;

	memset(&cpu.flags, 0, sizeof(cpu.flags));
	cpu.level = 3;

	if (has_eflag(X86_EFLAGS_AC))
		cpu.level = 4;

	get_cpuflags();
	err = check_cpuflags();

	if (test_bit(X86_FEATURE_LM, cpu.flags))
		cpu.level = 64;

	if (err == 0x01 &&
	    !(err_flags[0] &
	      ~((1 << X86_FEATURE_XMM) | (1 << X86_FEATURE_XMM2))) &&
	    cpu_vendor[0] == A32('A', 'u', 't', 'h') &&
	    cpu_vendor[1] == A32('e', 'n', 't', 'i') &&
	    cpu_vendor[2] == A32('c', 'A', 'M', 'D')) {
		struct msr m;

		boot_rdmsr(MSR_K7_HWCR, &m);
		m.l &= ~(1 << 15);
		boot_wrmsr(MSR_K7_HWCR, &m);

		get_cpuflags();
		err = check_cpuflags();
	} else if (err == 0x01 && !(err_flags[0] & ~(1 << X86_FEATURE_CX8)) &&
		   cpu_vendor[0] == A32('C', 'e', 'n', 't') &&
		   cpu_vendor[1] == A32('a', 'u', 'r', 'H') &&
		   cpu_vendor[2] == A32('a', 'u', 'l', 's') && cpu.model >= 6) {
		struct msr m;

		boot_rdmsr(MSR_VIA_FCR, &m);
		m.l |= (1 << 1) | (1 << 7);
		boot_wrmsr(MSR_VIA_FCR, &m);

		set_bit(X86_FEATURE_CX8, cpu.flags);
		err = check_cpuflags();
	} else if (err == 0x01 && cpu_vendor[0] == A32('G', 'e', 'n', 'u') &&
		   cpu_vendor[1] == A32('i', 'n', 'e', 'T') &&
		   cpu_vendor[2] == A32('M', 'x', '8', '6')) {
		struct msr m, m_tmp;
		u32 level = 1;

		boot_rdmsr(0x80860004, &m);
		m_tmp = m;
		m_tmp.l = ~0;
		boot_wrmsr(0x80860004, &m_tmp);
		asm("cpuid" : "+a"(level), "=d"(cpu.flags[0]) : : "ecx", "ebx");
		boot_wrmsr(0x80860004, &m);

		err = check_cpuflags();
	} else if (err == 0x01 && !(err_flags[0] & ~(1 << X86_FEATURE_PAE)) &&
		   is_intel() && cpu.level == 6 &&
		   (cpu.model == 9 || cpu.model == 13)) {
		if (cmdline_find_option_bool("forcepae")) {
			puts("WARNING: Forcing PAE in CPU flags\n");
			set_bit(X86_FEATURE_PAE, cpu.flags);
			err = check_cpuflags();
		} else {
			puts("WARNING: PAE disabled. Use parameter 'forcepae' to enable at your own risk!\n");
		}
	}
	if (!err)
		err = check_knl_erratum();

	if (err_flags_ptr)
		*err_flags_ptr = err ? err_flags : NULL;
	if (cpu_level_ptr)
		*cpu_level_ptr = cpu.level;
	if (req_level_ptr)
		*req_level_ptr = req_level;

	return (cpu.level < req_level || err) ? -1 : 0;
}

int check_knl_erratum(void)
{
	if (!is_intel() || cpu.family != 6 ||
	    cpu.model != INTEL_FAM6_XEON_PHI_KNL)
		return 0;

	/* CONFIG_X86_64 and CONFIG_X86_PAE are not set */
	puts("This 32-bit kernel can not run on this Xeon Phi x200\n"
	     "processor due to a processor erratum.  Use a 64-bit\n"
	     "kernel, or enable PAE in this 32-bit kernel.\n\n");

	return -1;
}
