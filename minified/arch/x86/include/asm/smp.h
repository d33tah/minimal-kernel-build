/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_SMP_H
#define _ASM_X86_SMP_H
#ifndef __ASSEMBLY__
#include <linux/cpumask.h>
#include <asm/percpu.h>

#include <asm/thread_info.h>
#include <asm/cpumask.h>

extern int smp_num_siblings;
extern unsigned int num_processors;

DECLARE_PER_CPU_READ_MOSTLY(cpumask_var_t, cpu_sibling_map);
DECLARE_PER_CPU_READ_MOSTLY(cpumask_var_t, cpu_core_map);
DECLARE_PER_CPU_READ_MOSTLY(cpumask_var_t, cpu_die_map);
/* cpus sharing the last level cache: */
DECLARE_PER_CPU_READ_MOSTLY(cpumask_var_t, cpu_llc_shared_map);
DECLARE_PER_CPU_READ_MOSTLY(cpumask_var_t, cpu_l2c_shared_map);
DECLARE_PER_CPU_READ_MOSTLY(u16, cpu_llc_id);
DECLARE_PER_CPU_READ_MOSTLY(u16, cpu_l2c_id);
DECLARE_PER_CPU_READ_MOSTLY(int, cpu_number);

static inline struct cpumask *cpu_llc_shared_mask(int cpu)
{
	return per_cpu(cpu_llc_shared_map, cpu);
}

static inline struct cpumask *cpu_l2c_shared_mask(int cpu)
{
	return per_cpu(cpu_l2c_shared_map, cpu);
}

DECLARE_EARLY_PER_CPU_READ_MOSTLY(u16, x86_cpu_to_apicid);
DECLARE_EARLY_PER_CPU_READ_MOSTLY(u32, x86_cpu_to_acpiid);
DECLARE_EARLY_PER_CPU_READ_MOSTLY(u16, x86_bios_cpu_apicid);

struct task_struct;

struct smp_ops {
	void (*smp_prepare_boot_cpu)(void);
	void (*smp_prepare_cpus)(unsigned max_cpus);
	void (*smp_cpus_done)(unsigned max_cpus);

	void (*stop_other_cpus)(int wait);
	void (*crash_stop_other_cpus)(void);
	void (*smp_send_reschedule)(int cpu);

	int (*cpu_up)(unsigned cpu, struct task_struct *tidle);
	int (*cpu_disable)(void);
	void (*cpu_die)(unsigned int cpu);
	void (*play_dead)(void);

	void (*send_call_func_ipi)(const struct cpumask *mask);
	void (*send_call_func_single_ipi)(int cpu);
};

/* Globals due to paravirt */
extern void set_cpu_sibling_map(int cpu);

#define wbinvd_on_cpu(cpu)     wbinvd()
static inline int wbinvd_on_all_cpus(void)
{
	wbinvd();
	return 0;
}

extern unsigned disabled_cpus;

#define hard_smp_processor_id()	0

#define nmi_selftest() do { } while (0)

#endif /* __ASSEMBLY__ */
#endif /* _ASM_X86_SMP_H */
