#ifndef _LINUX_CPU_H_
#define _LINUX_CPU_H_

#include <linux/compiler.h>
#include <linux/cpumask.h>
#include <linux/types.h>
#include <linux/device.h>

/* Simplified - only OFFLINE and ONLINE states needed, no hotplug callbacks */
enum cpuhp_state {
	CPUHP_INVALID = -1,
	CPUHP_OFFLINE = 0,
	CPUHP_ONLINE,
};

/* __cpuhp_setup_state, __cpuhp_setup_state_cpuslocked,
   cpuhp_setup_state_nocalls removed - CPU hotplug callbacks never called (~18 LOC) */


/* cpuhp_online_idle removed - unused */

struct device;

struct cpu {
	int node_id;		 
	int hotpluggable;	 
	struct device dev;
};

extern void boot_cpu_init(void);
extern void boot_cpu_hotplug_init(void);
extern void cpu_init(void);
extern void trap_init(void);

void __noreturn cpu_startup_entry(enum cpuhp_state state);

#define __cpuidle	__section(".cpuidle.text")

/* cpu_in_idle removed - never called */

void arch_cpu_idle(void);
void arch_cpu_idle_prepare(void);
void arch_cpu_idle_enter(void);
void arch_cpu_idle_exit(void);
/* arch_cpu_idle_dead removed - never called */


#endif  
