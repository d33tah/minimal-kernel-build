#ifndef _LINUX_CPU_H_
#define _LINUX_CPU_H_

#include <linux/node.h>
#include <linux/compiler.h>
#include <linux/cpumask.h>
#include <linux/types.h>

/* --- 2025-12-06 16:50 --- cpuhotplug.h inlined */
enum cpuhp_state {
	CPUHP_INVALID = -1,
	CPUHP_OFFLINE = 0,
	CPUHP_SLUB_DEAD,
	CPUHP_SOFTIRQ_DEAD,
	CPUHP_RADIX_DEAD,
	CPUHP_PAGE_ALLOC,
	CPUHP_BP_PREPARE_DYN,
	CPUHP_BP_PREPARE_DYN_END		= CPUHP_BP_PREPARE_DYN + 20,
	CPUHP_AP_ONLINE_DYN,
	CPUHP_AP_ONLINE_DYN_END		= CPUHP_AP_ONLINE_DYN + 30,
	CPUHP_ONLINE,
};

int __cpuhp_setup_state(enum cpuhp_state state,	const char *name, bool invoke,
			int (*startup)(unsigned int cpu),
			int (*teardown)(unsigned int cpu), bool multi_instance);

int __cpuhp_setup_state_cpuslocked(enum cpuhp_state state, const char *name,
				   bool invoke,
				   int (*startup)(unsigned int cpu),
				   int (*teardown)(unsigned int cpu),
				   bool multi_instance);

static inline int cpuhp_setup_state(enum cpuhp_state state,
				    const char *name,
				    int (*startup)(unsigned int cpu),
				    int (*teardown)(unsigned int cpu))
{
	return __cpuhp_setup_state(state, name, true, startup, teardown, false);
}

static inline int cpuhp_setup_state_cpuslocked(enum cpuhp_state state,
					       const char *name,
					       int (*startup)(unsigned int cpu),
					       int (*teardown)(unsigned int cpu))
{
	return __cpuhp_setup_state_cpuslocked(state, name, true, startup,
					      teardown, false);
}

static inline int cpuhp_setup_state_nocalls(enum cpuhp_state state,
					    const char *name,
					    int (*startup)(unsigned int cpu),
					    int (*teardown)(unsigned int cpu))
{
	return __cpuhp_setup_state(state, name, false, startup, teardown,
				   false);
}

static inline int cpuhp_setup_state_nocalls_cpuslocked(enum cpuhp_state state,
						     const char *name,
						     int (*startup)(unsigned int cpu),
						     int (*teardown)(unsigned int cpu))
{
	return __cpuhp_setup_state_cpuslocked(state, name, false, startup,
					    teardown, false);
}

static inline int cpuhp_setup_state_multi(enum cpuhp_state state,
					  const char *name,
					  int (*startup)(unsigned int cpu,
							 struct hlist_node *node),
					  int (*teardown)(unsigned int cpu,
							  struct hlist_node *node))
{
	return __cpuhp_setup_state(state, name, false,
				   (void *) startup,
				   (void *) teardown, true);
}

int __cpuhp_state_add_instance(enum cpuhp_state state, struct hlist_node *node,
			       bool invoke);
int __cpuhp_state_add_instance_cpuslocked(enum cpuhp_state state,
					  struct hlist_node *node, bool invoke);

static inline int cpuhp_state_add_instance(enum cpuhp_state state,
					   struct hlist_node *node)
{
	return __cpuhp_state_add_instance(state, node, true);
}

static inline int cpuhp_state_add_instance_nocalls(enum cpuhp_state state,
						   struct hlist_node *node)
{
	return __cpuhp_state_add_instance(state, node, false);
}

static inline int
cpuhp_state_add_instance_nocalls_cpuslocked(enum cpuhp_state state,
					    struct hlist_node *node)
{
	return __cpuhp_state_add_instance_cpuslocked(state, node, false);
}

void __cpuhp_remove_state(enum cpuhp_state state, bool invoke);
void __cpuhp_remove_state_cpuslocked(enum cpuhp_state state, bool invoke);

static inline void cpuhp_remove_state(enum cpuhp_state state)
{
	__cpuhp_remove_state(state, true);
}

static inline void cpuhp_remove_state_nocalls(enum cpuhp_state state)
{
	__cpuhp_remove_state(state, false);
}

static inline void cpuhp_remove_state_nocalls_cpuslocked(enum cpuhp_state state)
{
	__cpuhp_remove_state_cpuslocked(state, false);
}

static inline void cpuhp_remove_multi_state(enum cpuhp_state state)
{
	__cpuhp_remove_state(state, false);
}

int __cpuhp_state_remove_instance(enum cpuhp_state state,
				  struct hlist_node *node, bool invoke);

static inline int cpuhp_state_remove_instance(enum cpuhp_state state,
					      struct hlist_node *node)
{
	return __cpuhp_state_remove_instance(state, node, true);
}

static inline int cpuhp_state_remove_instance_nocalls(enum cpuhp_state state,
						      struct hlist_node *node)
{
	return __cpuhp_state_remove_instance(state, node, false);
}

static inline void cpuhp_online_idle(enum cpuhp_state state) { }
/* --- end cpuhotplug.h inlined --- */

struct device;
struct device_node;
struct attribute_group;

struct cpu {
	int node_id;		 
	int hotpluggable;	 
	struct device dev;
};

extern void boot_cpu_init(void);
extern void boot_cpu_hotplug_init(void);
extern void cpu_init(void);
extern void trap_init(void);

extern int register_cpu(struct cpu *cpu, int num);
extern struct device *get_cpu_device(unsigned cpu);
extern bool cpu_is_hotpluggable(unsigned cpu);
extern bool arch_match_cpu_phys_id(int cpu, u64 phys_id);
extern bool arch_find_n_match_cpu_physical_id(struct device_node *cpun,
					      int cpu, unsigned int *thread);


#define cpuhp_tasks_frozen	0

/* cpu_maps_update_begin/done removed - unused */

/* add_cpu removed - unused */

extern struct bus_type cpu_subsys;

extern int lockdep_is_cpus_held(void);


static inline void cpus_read_lock(void) { }
static inline void cpus_read_unlock(void) { }
/* cpus_read_trylock removed - unused */
static inline void lockdep_assert_cpus_held(void) { }
static inline void cpu_hotplug_disable(void) { }
/* remove_cpu, suspend_disable_secondary_cpus removed - unused */

void __noreturn cpu_startup_entry(enum cpuhp_state state);

void cpu_idle_poll_ctrl(bool enable);

#define __cpuidle	__section(".cpuidle.text")

bool cpu_in_idle(unsigned long pc);

void arch_cpu_idle(void);
void arch_cpu_idle_prepare(void);
void arch_cpu_idle_enter(void);
void arch_cpu_idle_exit(void);
void arch_cpu_idle_dead(void);

int cpu_report_state(int cpu);
int cpu_check_up_prepare(int cpu);
void cpu_set_state_online(int cpu);
void play_idle_precise(u64 duration_ns, u64 latency_ns);

/* play_idle removed - unused */

static inline void cpuhp_report_idle_dead(void) { }

/* cpu_smt_control, cpu_smt_possible, cpuhp_smt_enable, cpuhp_smt_disable removed - unused */

extern bool cpu_mitigations_off(void);
extern bool cpu_mitigations_auto_nosmt(void);

#endif  
