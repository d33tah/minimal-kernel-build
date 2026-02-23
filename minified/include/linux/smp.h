#ifndef __LINUX_SMP_H
#define __LINUX_SMP_H

#include <linux/list.h>
#include <linux/cpumask.h>

void panic_smp_self_stop(void);

#define raw_smp_processor_id()			0

#ifndef __smp_processor_id
#define __smp_processor_id(x) raw_smp_processor_id(x)
#endif

# define smp_processor_id() __smp_processor_id()

#endif
