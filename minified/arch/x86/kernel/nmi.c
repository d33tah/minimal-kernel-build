
#include <linux/spinlock.h>
#include <linux/kprobes.h>
#include <linux/kdebug.h>
#include <linux/sched/debug.h>
#include <linux/nmi.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/hardirq.h>
#include <linux/ratelimit.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/atomic.h>
#include <linux/sched/clock.h>

#include <asm/cpu_entry_area.h>
#include <asm/traps.h>
#include <asm/mach_traps.h>
#include <asm/nmi.h>
#include <asm/x86_init.h>
#include <asm/reboot.h>
#include <asm/cache.h>
#include <asm/nospec-branch.h>
#include <asm/sev.h>


struct nmi_stats {
	unsigned int normal;
	unsigned int unknown;
	unsigned int external;
	unsigned int swallow;
};

static DEFINE_PER_CPU(struct nmi_stats, nmi_stats);

static int ignore_nmis __read_mostly;

int unknown_nmi_panic;
static DEFINE_RAW_SPINLOCK(nmi_reason_lock);


/*
 * No NMI handlers are ever registered in this minimal kernel (the only
 * registrar, arch/x86/kernel/apic/hw_nmi.c, is compiled out because
 * arch_trigger_cpumask_backtrace is undefined), so the per-type handler
 * lists are permanently empty and nmi_handle() always reports 0 handled.
 */
static int nmi_handle(unsigned int type, struct pt_regs *regs)
{
	return 0;
}
NOKPROBE_SYMBOL(nmi_handle);

static void
pci_serr_error(unsigned char reason, struct pt_regs *regs)
{
	 
	if (nmi_handle(NMI_SERR, regs))
		return;

	pr_emerg("NMI: PCI system error (SERR) for reason %02x on CPU %d.\n",
		 reason, smp_processor_id());

	if (panic_on_unrecovered_nmi)
		nmi_panic(regs, "NMI: Not continuing");

	pr_emerg("Dazed and confused, but trying to continue\n");

	 
	reason = (reason & NMI_REASON_CLEAR_MASK) | NMI_REASON_CLEAR_SERR;
	outb(reason, NMI_REASON_PORT);
}
NOKPROBE_SYMBOL(pci_serr_error);

static void
io_check_error(unsigned char reason, struct pt_regs *regs)
{
	unsigned long i;

	 
	if (nmi_handle(NMI_IO_CHECK, regs))
		return;

	pr_emerg(
	"NMI: IOCK error (debug interrupt?) for reason %02x on CPU %d.\n",
		 reason, smp_processor_id());
	show_regs(regs);

	if (panic_on_io_nmi) {
		nmi_panic(regs, "NMI IOCK error: Not continuing");

		 
		return;
	}

	 
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
NOKPROBE_SYMBOL(io_check_error);

static void
unknown_nmi_error(unsigned char reason, struct pt_regs *regs)
{
	int handled;

	 
	handled = nmi_handle(NMI_UNKNOWN, regs);
	if (handled) {
		__this_cpu_add(nmi_stats.unknown, handled);
		return;
	}

	__this_cpu_add(nmi_stats.unknown, 1);

	pr_emerg("Uhhuh. NMI received for unknown reason %02x on CPU %d.\n",
		 reason, smp_processor_id());

	if (unknown_nmi_panic || panic_on_unrecovered_nmi)
		nmi_panic(regs, "NMI: Not continuing");

	pr_emerg("Dazed and confused, but trying to continue\n");
}
NOKPROBE_SYMBOL(unknown_nmi_error);

static DEFINE_PER_CPU(bool, swallow_nmi);
static DEFINE_PER_CPU(unsigned long, last_nmi_rip);

static noinstr void default_do_nmi(struct pt_regs *regs)
{
	unsigned char reason = 0;
	int handled;
	bool b2b = false;

	 

	 
	if (regs->ip == __this_cpu_read(last_nmi_rip))
		b2b = true;
	else
		__this_cpu_write(swallow_nmi, false);

	__this_cpu_write(last_nmi_rip, regs->ip);


	handled = nmi_handle(NMI_LOCAL, regs);
	__this_cpu_add(nmi_stats.normal, handled);
	if (handled) {
		 
		if (handled > 1)
			__this_cpu_write(swallow_nmi, true);
		goto out;
	}

	 
	while (!raw_spin_trylock(&nmi_reason_lock)) {
		run_crash_ipi_callback(regs);
		cpu_relax();
	}

	reason = x86_platform.get_nmi_reason();

	if (reason & NMI_REASON_MASK) {
		if (reason & NMI_REASON_SERR)
			pci_serr_error(reason, regs);
		else if (reason & NMI_REASON_IOCHK)
			io_check_error(reason, regs);
		 
		reassert_nmi();
		__this_cpu_add(nmi_stats.external, 1);
		raw_spin_unlock(&nmi_reason_lock);
		goto out;
	}
	raw_spin_unlock(&nmi_reason_lock);

	 
	if (b2b && __this_cpu_read(swallow_nmi))
		__this_cpu_add(nmi_stats.swallow, 1);
	else
		unknown_nmi_error(reason, regs);

out:
}

enum nmi_states {
	NMI_NOT_RUNNING = 0,
	NMI_EXECUTING,
	NMI_LATCHED,
};
static DEFINE_PER_CPU(enum nmi_states, nmi_state);
static DEFINE_PER_CPU(unsigned long, nmi_cr2);
static DEFINE_PER_CPU(unsigned long, nmi_dr7);

DEFINE_IDTENTRY_RAW(exc_nmi)
{
	irqentry_state_t irq_state;

	 
	sev_es_nmi_complete();

	if (this_cpu_read(nmi_state) != NMI_NOT_RUNNING) {
		this_cpu_write(nmi_state, NMI_LATCHED);
		return;
	}
	this_cpu_write(nmi_state, NMI_EXECUTING);
	this_cpu_write(nmi_cr2, read_cr2());
nmi_restart:

	 
	sev_es_ist_enter(regs);

	this_cpu_write(nmi_dr7, local_db_save());

	irq_state = irqentry_nmi_enter(regs);

	inc_irq_stat(__nmi_count);

	if (!ignore_nmis)
		default_do_nmi(regs);

	irqentry_nmi_exit(regs, irq_state);

	local_db_restore(this_cpu_read(nmi_dr7));

	sev_es_ist_exit();

	if (unlikely(this_cpu_read(nmi_cr2) != read_cr2()))
		write_cr2(this_cpu_read(nmi_cr2));
	if (this_cpu_dec_return(nmi_state))
		goto nmi_restart;
}

#if IS_MODULE(CONFIG_KVM_INTEL)
#endif

void local_touch_nmi(void)
{
	__this_cpu_write(last_nmi_rip, 0);
}
