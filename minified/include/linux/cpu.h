/* SPDX-License-Identifier: GPL-2.0 */
/*
 * include/linux/cpu.h - generic cpu definition
 *
 * This is mainly for topological representation. We define the 
 * basic 'struct cpu' here, which can be embedded in per-arch 
 * definitions of processors.
 *
 * Basic handling of the devices is done in drivers/base/cpu.c
 *
 * CPUs are exported via sysfs in the devices/system/cpu
 * directory. 
 */
#ifndef _LINUX_CPU_H_
#define _LINUX_CPU_H_

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
