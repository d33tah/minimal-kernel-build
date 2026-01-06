
#ifndef _ASM_X86_CPU_H
#define _ASM_X86_CPU_H

#include <linux/device.h>
#include <linux/cpu.h>
#include <linux/topology.h>
#include <linux/nodemask.h>
#include <linux/percpu.h>
#include <asm/ibt.h>

/* cpu_physical_id, cpu_acpi_id, safe_smp_processor_id removed - never used */

int mwait_usable(const struct cpuinfo_x86 *);

unsigned int x86_family(unsigned int sig);
unsigned int x86_model(unsigned int sig);
unsigned int x86_stepping(unsigned int sig);
/* sld_setup, handle_bus_lock removed - implementations were removed */

#endif
