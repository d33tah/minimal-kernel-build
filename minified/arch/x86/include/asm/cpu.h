/* SPDX-License-Identifier: GPL-2.0 */
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


struct x86_cpu {
	struct cpu cpu;
};


extern void ap_init_aperfmperf(void);

int mwait_usable(const struct cpuinfo_x86 *);

unsigned int x86_family(unsigned int sig);
unsigned int x86_model(unsigned int sig);
unsigned int x86_stepping(unsigned int sig);
extern void __init sld_setup(struct cpuinfo_x86 *c);
extern bool handle_user_split_lock(struct pt_regs *regs, long error_code);
extern bool handle_guest_split_lock(unsigned long ip);
extern void handle_bus_lock(struct pt_regs *regs);
u8 get_this_hybrid_cpu_type(void);
void init_ia32_feat_ctl(struct cpuinfo_x86 *c);

extern __noendbr void cet_disable(void);

struct ucode_cpu_info;

int intel_cpu_collect_info(struct ucode_cpu_info *uci);

static inline bool intel_cpu_signatures_match(unsigned int s1, unsigned int p1,
					      unsigned int s2, unsigned int p2)
{
	if (s1 != s2)
		return false;

	/* Processor flags are either both 0 ... */
	if (!p1 && !p2)
		return true;

	/* ... or they intersect. */
	return p1 & p2;
}

#endif /* _ASM_X86_CPU_H */
