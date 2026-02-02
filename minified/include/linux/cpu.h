#ifndef _LINUX_CPU_H_
#define _LINUX_CPU_H_

#include <linux/compiler.h>
#include <linux/cpumask.h>
#include <linux/types.h>
#include <linux/device.h>

/* Simplified - only ONLINE state needed, no hotplug callbacks */
enum cpuhp_state {
	/* CPUHP_INVALID removed - unused */
	/* CPUHP_OFFLINE removed - unused */
	CPUHP_ONLINE = 1,
};

/* __cpuhp_setup_state, __cpuhp_setup_state_cpuslocked,
   cpuhp_setup_state_nocalls removed - CPU hotplug callbacks never called (~18 LOC) */


/* cpuhp_online_idle removed - unused */

/* struct cpu removed - never instantiated */

extern void boot_cpu_init(void);
/* boot_cpu_hotplug_init removed - empty stub */
extern void cpu_init(void);
extern void trap_init(void);

void __noreturn cpu_startup_entry(enum cpuhp_state state);

#define __cpuidle	__section(".cpuidle.text")

/* cpu_in_idle removed - never called */

void arch_cpu_idle(void);
void arch_cpu_idle_enter(void);
/* arch_cpu_idle_prepare, arch_cpu_idle_exit, arch_cpu_idle_dead removed - never called */


#endif  
