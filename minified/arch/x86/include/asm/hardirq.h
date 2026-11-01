#ifndef _ASM_X86_HARDIRQ_H
#define _ASM_X86_HARDIRQ_H

typedef struct {
	unsigned int __nmi_count;
} ____cacheline_aligned irq_cpustat_t;

DECLARE_PER_CPU_SHARED_ALIGNED(irq_cpustat_t, irq_stat);

#define inc_irq_stat(member) this_cpu_inc(irq_stat.member)

#endif
