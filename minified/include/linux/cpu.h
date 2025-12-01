#ifndef _LINUX_CPU_H_
#define _LINUX_CPU_H_

#include <linux/node.h>
#include <linux/compiler.h>
#include <linux/cpumask.h>
#include <linux/cpuhotplug.h>

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

static inline void cpu_maps_update_begin(void)
{
}

static inline void cpu_maps_update_done(void)
{
}

/* add_cpu removed - unused */

extern struct bus_type cpu_subsys;

extern int lockdep_is_cpus_held(void);


static inline void cpus_read_lock(void) { }
static inline void cpus_read_unlock(void) { }
static inline int  cpus_read_trylock(void) { return true; }
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

static inline void play_idle(unsigned long duration_us)
{
	play_idle_precise(duration_us * NSEC_PER_USEC, U64_MAX);
}

static inline void cpuhp_report_idle_dead(void) { }

/* cpu_smt_control, cpu_smt_possible, cpuhp_smt_enable, cpuhp_smt_disable removed - unused */

extern bool cpu_mitigations_off(void);
extern bool cpu_mitigations_auto_nosmt(void);

#endif  
