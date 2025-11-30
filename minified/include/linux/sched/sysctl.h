 
#ifndef _LINUX_SCHED_SYSCTL_H
#define _LINUX_SCHED_SYSCTL_H

#include <linux/types.h>

struct ctl_table;

 
enum { sysctl_hung_task_timeout_secs = 0 };

enum sched_tunable_scaling {
	SCHED_TUNABLESCALING_NONE,
	SCHED_TUNABLESCALING_LOG,
	SCHED_TUNABLESCALING_LINEAR,
	SCHED_TUNABLESCALING_END,
};

#define NUMA_BALANCING_DISABLED		0x0
#define NUMA_BALANCING_NORMAL		0x1
#define NUMA_BALANCING_MEMORY_TIERING	0x2

#define sysctl_numa_balancing_mode	0

int sysctl_numa_balancing(struct ctl_table *table, int write, void *buffer,
		size_t *lenp, loff_t *ppos);

#endif  
