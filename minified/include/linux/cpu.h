#ifndef _LINUX_CPU_H_
#define _LINUX_CPU_H_

#include <linux/cpumask.h>

/* cpu_startup_entry() ignores its argument; only CPUHP_ONLINE is passed. */
enum cpuhp_state {
	CPUHP_ONLINE,
};

extern void cpu_init(void);
extern void trap_init(void);

void __noreturn cpu_startup_entry(enum cpuhp_state state);

#define __cpuidle __section(".cpuidle.text")

void arch_cpu_idle(void);

#endif
