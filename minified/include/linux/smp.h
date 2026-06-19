#ifndef __LINUX_SMP_H
#define __LINUX_SMP_H


#include <linux/errno.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/cpumask.h>
#include <linux/init.h>
#include <linux/smp_types.h>

typedef void (*smp_call_func_t)(void *info);
typedef bool (*smp_cond_func_t)(int cpu, void *info);



void panic_smp_self_stop(void);
void nmi_panic_self_stop(struct pt_regs *regs);

#define raw_smp_processor_id()			0

#define smp_prepare_boot_cpu()			do {} while (0)

static inline void smp_init(void) { }


#ifndef __smp_processor_id
#define __smp_processor_id(x) raw_smp_processor_id(x)
#endif

# define smp_processor_id() __smp_processor_id()

#define get_cpu()		({ preempt_disable(); __smp_processor_id(); })
#define put_cpu()		preempt_enable()


void smp_setup_processor_id(void);

#endif  
