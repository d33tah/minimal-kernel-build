// SPDX-License-Identifier: GPL-2.0-only
/*
 * Common interrupt code for 32 and 64 bit
 */
#include <linux/cpu.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/of.h>
#include <linux/seq_file.h>
#include <linux/smp.h>
#include <linux/ftrace.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/irq.h>

#include <asm/irq_stack.h>
#include <asm/apic.h>
#include <asm/io_apic.h>
#include <asm/irq.h>
#include <asm/mce.h>
#include <asm/hw_irq.h>
#include <asm/desc.h>
#include <asm/traps.h>
#include <asm/thermal.h>

#include <asm/trace/irq_vectors.h>

DEFINE_PER_CPU_SHARED_ALIGNED(irq_cpustat_t, irq_stat);

atomic_t irq_err_count;

/*
 * 'what should we do if we get a hw irq event on an illegal vector'.
 * each architecture has to answer this themselves.
 */
void ack_bad_irq(unsigned int irq)
{
	if (printk_ratelimit())
		pr_err("unexpected IRQ trap at vector %02x\n", irq);

	/*
	 * Currently unexpected vectors happen only on SMP and APIC.
	 * We _must_ ack these because every local APIC has only N
	 * irq slots per priority level, and a 'hanging, unacked' IRQ
	 * holds up an irq slot - in excessive cases (when multiple
	 * unexpected vectors occur) that might lock up the APIC
	 * completely.
	 * But only ack when the APIC is enabled -AK
	 */
	ack_APIC_irq();
}

#define irq_stats(x)		(&per_cpu(irq_stat, x))
/*
 * /proc/interrupts printing for arch specific interrupts
 */
int arch_show_interrupts(struct seq_file *p, int prec)
{
	int j;

	seq_printf(p, "%*s: ", prec, "NMI");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", irq_stats(j)->__nmi_count);
	seq_puts(p, "  Non-maskable interrupts\n");
#if IS_ENABLED(CONFIG_HYPERV)
	if (test_bit(HYPERV_REENLIGHTENMENT_VECTOR, system_vectors)) {
		seq_printf(p, "%*s: ", prec, "HRE");
		for_each_online_cpu(j)
			seq_printf(p, "%10u ",
				   irq_stats(j)->irq_hv_reenlightenment_count);
		seq_puts(p, "  Hyper-V reenlightenment interrupts\n");
	}
	if (test_bit(HYPERV_STIMER0_VECTOR, system_vectors)) {
		seq_printf(p, "%*s: ", prec, "HVS");
		for_each_online_cpu(j)
			seq_printf(p, "%10u ",
				   irq_stats(j)->hyperv_stimer0_count);
		seq_puts(p, "  Hyper-V stimer0 interrupts\n");
	}
#endif
	seq_printf(p, "%*s: %10u\n", prec, "ERR", atomic_read(&irq_err_count));
	return 0;
}

/*
 * /proc/stat helpers
 */
u64 arch_irq_stat_cpu(unsigned int cpu)
{
	u64 sum = irq_stats(cpu)->__nmi_count;

	return sum;
}

u64 arch_irq_stat(void)
{
	u64 sum = atomic_read(&irq_err_count);
	return sum;
}

static __always_inline void handle_irq(struct irq_desc *desc,
				       struct pt_regs *regs)
{
	if (IS_ENABLED(CONFIG_X86_64))
		generic_handle_irq_desc(desc);
	else
		__handle_irq(desc, regs);
}

/*
 * common_interrupt() handles all normal device IRQ's (the special SMP
 * cross-CPU interrupts have their own entry points).
 */
DEFINE_IDTENTRY_IRQ(common_interrupt)
{
	struct pt_regs *old_regs = set_irq_regs(regs);
	struct irq_desc *desc;

	/* entry code tells RCU that we're not quiescent.  Check it. */
	RCU_LOCKDEP_WARN(!rcu_is_watching(), "IRQ failed to wake up RCU");

	desc = __this_cpu_read(vector_irq[vector]);
	if (likely(!IS_ERR_OR_NULL(desc))) {
		handle_irq(desc, regs);
	} else {
		ack_APIC_irq();

		if (desc == VECTOR_UNUSED) {
			pr_emerg_ratelimited("%s: %d.%u No irq handler for vector\n",
					     __func__, smp_processor_id(),
					     vector);
		} else {
			__this_cpu_write(vector_irq[vector], VECTOR_UNUSED);
		}
	}

	set_irq_regs(old_regs);
}





