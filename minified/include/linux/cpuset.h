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

static inline void cpuset_update_active_cpus(void)
{
	partition_sched_domains(1, NULL, NULL);
}

static inline void cpuset_read_lock(void) { }
static inline void cpuset_read_unlock(void) { }

static inline void cpuset_cpus_allowed(struct task_struct *p,
				       struct cpumask *mask)
{
	cpumask_copy(mask, task_cpu_possible_mask(p));
}


static inline nodemask_t cpuset_mems_allowed(struct task_struct *p)
{
	return node_possible_map;
}

#define cpuset_current_mems_allowed (node_states[N_MEMORY])
static inline void cpuset_init_current_mems_allowed(void) {}

static inline int cpuset_nodemask_valid_mems_allowed(nodemask_t *nodemask)
{
	return 1;
}

static inline bool cpuset_node_allowed(int node, gfp_t gfp_mask)
{
	return true;
}

static inline bool __cpuset_zone_allowed(struct zone *z, gfp_t gfp_mask)
{
	return true;
}

static inline bool cpuset_zone_allowed(struct zone *z, gfp_t gfp_mask)
{
	return true;
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
