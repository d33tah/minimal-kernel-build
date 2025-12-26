 
#ifndef _ASM_X86_SMP_H
#define _ASM_X86_SMP_H
#ifndef __ASSEMBLY__
#include <linux/cpumask.h>
#include <asm/percpu.h>

#include <asm/thread_info.h>
#include <asm/cpumask.h>

extern int smp_num_siblings;
extern unsigned int num_processors;

DECLARE_PER_CPU_READ_MOSTLY(cpumask_var_t, cpu_sibling_map);
DECLARE_PER_CPU_READ_MOSTLY(cpumask_var_t, cpu_core_map);
DECLARE_PER_CPU_READ_MOSTLY(cpumask_var_t, cpu_die_map);
 
DECLARE_PER_CPU_READ_MOSTLY(cpumask_var_t, cpu_llc_shared_map);
DECLARE_PER_CPU_READ_MOSTLY(cpumask_var_t, cpu_l2c_shared_map);
DECLARE_PER_CPU_READ_MOSTLY(u16, cpu_llc_id);
DECLARE_PER_CPU_READ_MOSTLY(u16, cpu_l2c_id);
DECLARE_PER_CPU_READ_MOSTLY(int, cpu_number);

DECLARE_EARLY_PER_CPU_READ_MOSTLY(u16, x86_cpu_to_apicid);
DECLARE_EARLY_PER_CPU_READ_MOSTLY(u32, x86_cpu_to_acpiid);
DECLARE_EARLY_PER_CPU_READ_MOSTLY(u16, x86_bios_cpu_apicid);

struct task_struct;

#endif
#endif  
