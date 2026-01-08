 
#ifndef _ASM_X86_HARDIRQ_H
#define _ASM_X86_HARDIRQ_H

#include <linux/threads.h>

typedef struct {
	u16	     __softirq_pending;
	/* __nmi_count, x86_platform_ipis, apic_perf_irqs, apic_irq_work_irqs, irq_tlb_count removed - never read */
} ____cacheline_aligned irq_cpustat_t;

DECLARE_PER_CPU_SHARED_ALIGNED(irq_cpustat_t, irq_stat);

#define __ARCH_IRQ_STAT

#define inc_irq_stat(member)	this_cpu_inc(irq_stat.member)

extern void ack_bad_irq(unsigned int irq);

/* CONFIG_KVM_INTEL not enabled, but called from DEFINE_IDTENTRY_IRQ macro */
static inline void kvm_set_cpu_l1tf_flush_l1d(void) { }

#endif  
