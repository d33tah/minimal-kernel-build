#ifndef _LINUX_KERNEL_STAT_H
#define _LINUX_KERNEL_STAT_H

#include <linux/smp.h>
#include <linux/percpu.h>
#include <linux/interrupt.h>

/* enum cpu_usage_stat, struct kernel_cpustat removed - never used (~16 LOC) */

struct kernel_stat {
	unsigned long irqs_sum;
	unsigned int softirqs[NR_SOFTIRQS];
};

DECLARE_PER_CPU(struct kernel_stat, kstat);

/* kstat_this_cpu, kcpustat_this_cpu, kstat_cpu, kcpustat_cpu removed - unused */



static inline void kstat_incr_softirqs_this_cpu(unsigned int irq)
{
	__this_cpu_inc(kstat.softirqs[irq]);
}


#endif  
