 
#ifndef _ASM_X86_SMP_H
#define _ASM_X86_SMP_H
#ifndef __ASSEMBLY__
#include <linux/cpumask.h>
#include <asm/percpu.h>

#include <asm/thread_info.h>
#include <asm/cpumask.h>

/* smp_num_siblings removed - never used */

/* cpu_sibling_map, cpu_core_map, cpu_die_map, cpu_llc_shared_map, cpu_l2c_shared_map removed - never used */
/* cpu_llc_id, cpu_l2c_id, cpu_number removed - never used */

/* x86_cpu_to_apicid, x86_cpu_to_acpiid, x86_bios_cpu_apicid removed - never used */

struct task_struct;

#endif
#endif  
