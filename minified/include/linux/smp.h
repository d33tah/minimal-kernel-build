#ifndef __LINUX_SMP_H
#define __LINUX_SMP_H


/* linux/errno.h removed - no errno constants used */
#include <linux/types.h>
#include <linux/list.h>
#include <linux/cpumask.h>
#include <linux/init.h>
/* smp_types.h removed - empty file after irq_work removal */
/* smp_call_func_t, smp_cond_func_t typedefs removed - never used */
/* on_each_cpu_cond_mask, on_each_cpu_mask, on_each_cpu removed - no callers after TLB simplification */

void panic_smp_self_stop(void);

/* smp_send_stop, smp_send_reschedule, smp_init, smp_prepare_boot_cpu removed - unused stubs */

#define raw_smp_processor_id()			0


#ifndef __smp_processor_id
#define __smp_processor_id(x) raw_smp_processor_id(x)
#endif

# define smp_processor_id() __smp_processor_id()

#define get_cpu()		({ preempt_disable(); __smp_processor_id(); })
#define put_cpu()		preempt_enable()


void smp_setup_processor_id(void);

#endif  
