 
#ifndef _ASM_X86_SMP_H
#define _ASM_X86_SMP_H
#ifndef __ASSEMBLY__
#include <linux/cpumask.h>
#include <asm/percpu.h>

#include <asm/thread_info.h>
#include <asm/cpumask.h>


DECLARE_PER_CPU_READ_MOSTLY(int, cpu_number);

DECLARE_EARLY_PER_CPU_READ_MOSTLY(u16, x86_cpu_to_apicid);

struct task_struct;

/* set_cpu_sibling_map removed - never defined (SMP only) */


/* disabled_cpus removed - never defined (SMP only) */


#endif  
#endif  
