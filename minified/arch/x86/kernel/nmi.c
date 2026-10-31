
#include <linux/nmi.h>
#include <linux/delay.h>

#include <asm/traps.h>
#include <asm/io.h>

#define NMI_REASON_PORT 0x61
#define NMI_REASON_SERR 0x80
#define NMI_REASON_IOCHK 0x40
#define NMI_REASON_CLEAR_SERR 0x04
#define NMI_REASON_CLEAR_IOCHK 0x08
#define NMI_REASON_CLEAR_MASK 0x0f

static DEFINE_RAW_SPINLOCK(nmi_reason_lock);

/*
 * No NMI handlers are ever registered in this minimal kernel (the only
 * registrar, arch/x86/kernel/apic/hw_nmi.c, is compiled out because
 * arch_trigger_cpumask_backtrace is undefined), so the per-type handler
 * lists are permanently empty and would-be nmi_handle() always reports 0
 * handled -- the dispatch call and its 0-result branches are folded out.
 */

enum nmi_states {
	NMI_NOT_RUNNING = 0,
	NMI_EXECUTING,
	NMI_LATCHED,
};
static DEFINE_PER_CPU(enum nmi_states, nmi_state);
static DEFINE_PER_CPU(unsigned long, nmi_cr2);
static DEFINE_PER_CPU(unsigned long, nmi_dr7);

/*
 * The only NMI sources are the chipset's SERR/IOCHK lines (port 0x61):
 * acknowledge them; an NMI with neither bit set is ignored. printk is a
 * no-op on this build, so there are no diagnostics.
 */
DEFINE_IDTENTRY_RAW(exc_nmi)
{
	irqentry_state_t irq_state;
	unsigned char reason;

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

	while (!raw_spin_trylock(&nmi_reason_lock))
		cpu_relax();

	reason = inb(NMI_REASON_PORT);

	if (reason & NMI_REASON_SERR) {
		reason = (reason & NMI_REASON_CLEAR_MASK) |
			 NMI_REASON_CLEAR_SERR;
		outb(reason, NMI_REASON_PORT);
	} else if (reason & NMI_REASON_IOCHK) {
		unsigned long i;

		reason = (reason & NMI_REASON_CLEAR_MASK) |
			 NMI_REASON_CLEAR_IOCHK;
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

	irqentry_nmi_exit(regs, irq_state);

	local_db_restore(this_cpu_read(nmi_dr7));

	if (unlikely(this_cpu_read(nmi_cr2) != read_cr2()))
		write_cr2(this_cpu_read(nmi_cr2));
	if (this_cpu_dec_return(nmi_state))
		goto nmi_restart;
}
