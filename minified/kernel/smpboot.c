// SPDX-License-Identifier: GPL-2.0-only
/*
 * Minimal SMP boot stub for minimal kernel
 * Original: 358 LOC - all SMP hotplug functionality disabled
 */
#include <linux/cpu.h>
#include <linux/err.h>
#include <linux/smp.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/export.h>
#include <linux/percpu.h>
#include <linux/kthread.h>
#include <linux/smpboot.h>

#include "smpboot.h"

/* Stub: no threads to create */
int smpboot_create_threads(unsigned int cpu)
{
	return 0;
}

/* Stub: no threads to unpark */
int smpboot_unpark_threads(unsigned int cpu)
{
	return 0;
}

/* Stub: no threads to park */
int smpboot_park_threads(unsigned int cpu)
{
	return 0;
}

/* Stub: no per-CPU thread registration */
int smpboot_register_percpu_thread(struct smp_hotplug_thread *plug_thread)
{
	return 0;
}

/* Stub: no per-CPU thread unregistration */
void smpboot_unregister_percpu_thread(struct smp_hotplug_thread *plug_thread)
{
}

/* Stub: always report online */
int cpu_report_state(int cpu)
{
	return 0;
}

/* Stub: always ready */
int cpu_check_up_prepare(int cpu)
{
	return 0;
}

/* Stub: no-op */
void cpu_set_state_online(int cpu)
{
}

/* Stub: always complete */
void cpu_report_death(void)
{
}

/* Stub: always idle */
bool cpu_wait_death(unsigned int cpu, int seconds)
{
	return true;
}

/* Stub: always allow */
bool cpu_reports_death(unsigned int cpu)
{
	return true;
}
