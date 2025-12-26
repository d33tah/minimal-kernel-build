 
#ifndef _ASM_X86_CPU_H
#define _ASM_X86_CPU_H

#include <linux/device.h>
#include <linux/cpu.h>
#include <linux/topology.h>
#include <linux/nodemask.h>
#include <linux/percpu.h>
#include <asm/ibt.h>


static inline void prefill_possible_map(void) {}

#define cpu_physical_id(cpu)			boot_cpu_physical_apicid
#define cpu_acpi_id(cpu)			0
#define safe_smp_processor_id()			0

int mwait_usable(const struct cpuinfo_x86 *);

unsigned int x86_family(unsigned int sig);
unsigned int x86_model(unsigned int sig);
unsigned int x86_stepping(unsigned int sig);
extern void __init sld_setup(struct cpuinfo_x86 *c);
/* handle_guest_split_lock, handle_user_split_lock removed - never called */
extern void handle_bus_lock(struct pt_regs *regs);
/* get_this_hybrid_cpu_type, init_ia32_feat_ctl, cet_disable, intel_cpu_collect_info, intel_cpu_signatures_match removed - never called */

#endif  
