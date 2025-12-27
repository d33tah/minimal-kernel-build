 
#ifndef _ASM_X86_CPUMASK_H
#define _ASM_X86_CPUMASK_H
#ifndef __ASSEMBLY__
#include <linux/cpumask.h>

/* cpu_callin_mask, cpu_callout_mask, cpu_sibling_setup_mask removed - unused (SMP only) */
extern cpumask_var_t cpu_initialized_mask;

extern void setup_cpu_local_masks(void);

/* NR_CPUS == 1, simplified for single CPU */
static __always_inline bool arch_cpu_online(int cpu)
{
	return cpu == 0;
}

#endif
#endif
