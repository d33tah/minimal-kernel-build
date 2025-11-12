// SPDX-License-Identifier: GPL-2.0
/* IA32 Feature Control MSR - STUBBED
 * VMX and SGX initialization not needed for minimal boot
 */
#include <linux/tboot.h>
#include <asm/cpufeature.h>
#include <asm/msr-index.h>
#include <asm/processor.h>
#include "cpu.h"

#undef pr_fmt
#define pr_fmt(fmt)	"x86/cpu: " fmt

static int __init nosgx(char *str)
{
	setup_clear_cpu_cap(X86_FEATURE_SGX);
	return 0;
}

early_param("nosgx", nosgx);

/* Stub - disable VMX and SGX features */
void init_ia32_feat_ctl(struct cpuinfo_x86 *c)
{
	/* Simply clear VMX and SGX caps - not needed for minimal boot */
	clear_cpu_cap(c, X86_FEATURE_VMX);
	clear_cpu_cap(c, X86_FEATURE_SGX);
	clear_cpu_cap(c, X86_FEATURE_SGX_LC);
}
