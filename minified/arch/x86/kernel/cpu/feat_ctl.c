 
 
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

 
void init_ia32_feat_ctl(struct cpuinfo_x86 *c)
{
	 
	clear_cpu_cap(c, X86_FEATURE_VMX);
	clear_cpu_cap(c, X86_FEATURE_SGX);
	clear_cpu_cap(c, X86_FEATURE_SGX_LC);
}
