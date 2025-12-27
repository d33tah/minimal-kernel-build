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

int sysctl_numa_balancing(struct ctl_table *table, int write, void *buffer,
		size_t *lenp, loff_t *ppos);

#endif  
