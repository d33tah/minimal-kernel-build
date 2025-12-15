#ifndef _LINUX_CPUSET_H
#define _LINUX_CPUSET_H

#include <linux/sched.h>
#include <linux/sched/topology.h>
#include <linux/sched/task.h>
#include <linux/cpumask.h>
#include <linux/nodemask.h>
#include <linux/mm.h>
#include <linux/mmu_context.h>
#include <linux/jump_label.h>


static inline bool cpusets_enabled(void) { return false; }

static inline int cpuset_init(void) { return 0; }
static inline void cpuset_init_smp(void) {}

#define cpuset_current_mems_allowed (node_states[N_MEMORY])
static inline void cpuset_init_current_mems_allowed(void) {}

static inline int cpuset_nodemask_valid_mems_allowed(nodemask_t *nodemask)
{
	return 1;
}

static inline void rebuild_sched_domains(void)
{
	partition_sched_domains(1, NULL, NULL);
}

static inline void set_mems_allowed(nodemask_t nodemask)
{
}

static inline unsigned int read_mems_allowed_begin(void)
{
	return 0;
}

static inline bool read_mems_allowed_retry(unsigned int seq)
{
	return false;
}


#endif  
