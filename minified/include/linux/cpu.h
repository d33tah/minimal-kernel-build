#ifndef _LINUX_CPU_H_
#define _LINUX_CPU_H_

#include <linux/cpumask.h>
#include <linux/types.h>
#include <linux/device.h>

enum cpuhp_state {
	CPUHP_ONLINE = 1,
};

extern void boot_cpu_init(void);
extern void cpu_init(void);
extern void trap_init(void);

void __noreturn cpu_startup_entry(enum cpuhp_state state);

#define __cpuidle	__section(".cpuidle.text")

void arch_cpu_idle(void);
void arch_cpu_idle_enter(void);

#endif  
