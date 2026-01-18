#ifndef __LINUX_SMP_H
#define __LINUX_SMP_H


#include <linux/errno.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/cpumask.h>
#include <linux/init.h>
/* smp_types.h removed - empty file after irq_work removal */

typedef void (*smp_call_func_t)(void *info);
typedef bool (*smp_cond_func_t)(int cpu, void *info);

void on_each_cpu_cond_mask(smp_cond_func_t cond_func, smp_call_func_t func,
			   void *info, bool wait, const struct cpumask *mask);

void panic_smp_self_stop(void);
/* nmi_panic_self_stop removed - only called by nmi_panic which was removed */
/* crash_smp_send_stop removed - stub */

static inline void on_each_cpu(smp_call_func_t func, void *info, int wait)
{
	on_each_cpu_cond_mask(NULL, func, info, wait, cpu_online_mask);
}

static inline void on_each_cpu_mask(const struct cpumask *mask,
				    smp_call_func_t func, void *info, bool wait)
{
	on_each_cpu_cond_mask(NULL, func, info, wait, mask);
}

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
