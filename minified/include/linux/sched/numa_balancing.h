#ifndef _LINUX_SCHED_NUMA_BALANCING_H
#define _LINUX_SCHED_NUMA_BALANCING_H


#include <linux/sched.h>

#define TNF_MIGRATED	0x01
#define TNF_NO_GROUP	0x02
#define TNF_SHARED	0x04
#define TNF_FAULT_LOCAL	0x08
#define TNF_MIGRATE_FAIL 0x10

/* task_numa_fault, task_numa_group_id, set_numabalancing_state,
   should_numa_migrate_memory removed - unused */
static inline void task_numa_free(struct task_struct *p, bool final)
{
}

#endif  
