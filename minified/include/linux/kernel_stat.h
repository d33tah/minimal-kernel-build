#ifndef _LINUX_KERNEL_STAT_H
#define _LINUX_KERNEL_STAT_H

#include <linux/smp.h>
#include <linux/percpu.h>
#include <linux/interrupt.h>

/* enum cpu_usage_stat, struct kernel_cpustat removed - never used (~16 LOC) */
/* struct kernel_stat, kstat per-CPU removed - write-only */
/* kstat_incr_softirqs_this_cpu removed - empty stub with no callers */

#endif  
