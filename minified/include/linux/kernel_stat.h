#ifndef _LINUX_KERNEL_STAT_H
#define _LINUX_KERNEL_STAT_H

#include <linux/smp.h>
#include <linux/percpu.h>
#include <linux/interrupt.h>

/* enum cpu_usage_stat, struct kernel_cpustat removed - never used (~16 LOC) */
/* struct kernel_stat, kstat per-CPU, kstat_incr_softirqs_this_cpu removed - write-only */
static inline void kstat_incr_softirqs_this_cpu(unsigned int irq) { }


#endif  
