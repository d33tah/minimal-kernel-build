
#include <linux/sched/debug.h>
#include <linux/nmi.h>
#include <linux/delay.h>

#include <asm/traps.h>
#include <asm/mach_traps.h>



static DEFINE_RAW_SPINLOCK(nmi_reason_lock);


/*
 * No NMI handlers are ever registered in this minimal kernel (the only
 * registrar, arch/x86/kernel/apic/hw_nmi.c, is compiled out because
 * arch_trigger_cpumask_backtrace is undefined), so the per-type handler
 * lists are permanently empty and would-be nmi_handle() always reports 0
 * handled -- the dispatch call and its 0-result branches are folded out.
 */

static DEFINE_PER_CPU(bool, swallow_nmi);
static DEFINE_PER_CPU(unsigned long, last_nmi_rip);

enum nmi_states { NMI_NOT_RUNNING = 0, NMI_EXECUTING, NMI_LATCHED, };
static DEFINE_PER_CPU(enum nmi_states, nmi_state);
static DEFINE_PER_CPU(unsigned long, nmi_cr2);
static DEFINE_PER_CPU(unsigned long, nmi_dr7);

DEFINE_IDTENTRY_RAW(exc_nmi) {
	irqentry_state_t irq_state;

	if (this_cpu_read(nmi_state) != NMI_NOT_RUNNING) {
		this_cpu_write(nmi_state, NMI_LATCHED);
		return;
	}
	this_cpu_write(nmi_state, NMI_EXECUTING);
	this_cpu_write(nmi_cr2, read_cr2());
nmi_restart:

	this_cpu_write(nmi_dr7, local_db_save());

	irq_state = irqentry_nmi_enter(regs);

	inc_irq_stat(__nmi_count);

	unsigned char reason = 0;
	bool b2b = false;

	if (regs->ip == __this_cpu_read(last_nmi_rip))
		b2b = true;
	else
		__this_cpu_write(swallow_nmi, false);

	__this_cpu_write(last_nmi_rip, regs->ip);


	while (!raw_spin_trylock(&nmi_reason_lock)) {
		cpu_relax();
	}

	reason = x86_platform.get_nmi_reason();

	if (reason & NMI_REASON_MASK) {
		if (reason & NMI_REASON_SERR) {
			pr_emerg("NMI: PCI system error (SERR) for reason %02x on CPU %d.\n", reason, smp_processor_id());

			pr_emerg("Dazed and confused, but trying to continue\n");

			reason = (reason & NMI_REASON_CLEAR_MASK) | NMI_REASON_CLEAR_SERR;
			outb(reason, NMI_REASON_PORT);
		} else if (reason & NMI_REASON_IOCHK) {
			unsigned long i;

			pr_emerg( "NMI: IOCK error (debug interrupt?) for reason %02x on CPU %d.\n", reason, smp_processor_id());
			show_regs(regs);

			reason = (reason & NMI_REASON_CLEAR_MASK) | NMI_REASON_CLEAR_IOCHK;
			outb(reason, NMI_REASON_PORT);

			i = 20000;
			while (--i) {
				touch_nmi_watchdog();
				udelay(100);
			}

			reason &= ~NMI_REASON_CLEAR_IOCHK;
			outb(reason, NMI_REASON_PORT);
		}
		raw_spin_unlock(&nmi_reason_lock);
	} else {
		raw_spin_unlock(&nmi_reason_lock);


		if (!(b2b && __this_cpu_read(swallow_nmi))) {
			pr_emerg("Uhhuh. NMI received for unknown reason %02x on CPU %d.\n", reason, smp_processor_id());

			pr_emerg("Dazed and confused, but trying to continue\n");
		}
	}

	irqentry_nmi_exit(regs, irq_state);

	local_db_restore(this_cpu_read(nmi_dr7));

	if (unlikely(this_cpu_read(nmi_cr2) != read_cr2()))
		write_cr2(this_cpu_read(nmi_cr2));
	if (this_cpu_dec_return(nmi_state))
		goto nmi_restart;
}
