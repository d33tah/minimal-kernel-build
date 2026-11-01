// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 2000, 2001, 2002 Andi Kleen, SuSE Labs
 *  Copyright (C) 2011	Don Zickus Red Hat, Inc.
 *
 *  Pentium III FXSR, SSE support
 *	Gareth Hughes <gareth@valinux.com>, May 2000
 */

/*
 * Handle hardware traps and faults.
 */
#include <linux/delay.h>

#include <asm/traps.h>
#include <asm/io.h>
#include <asm/mach_traps.h>

/*
 * Prevent NMI reason port (0x61) being accessed simultaneously, can
 * only be used in NMI handler.
 */
static DEFINE_RAW_SPINLOCK(nmi_reason_lock);

/*
 * NMIs can page fault or hit breakpoints which will cause it to lose
 * its NMI context with the CPU when the breakpoint or page fault does an IRET.
 *
 * As a result, NMIs can nest if NMIs get unmasked due an IRET during
 * NMI processing.  On x86_64, the asm glue protects us from nested NMIs
 * if the outer NMI came from kernel mode, but we can still nest if the
 * outer NMI came from user mode.
 *
 * To handle these nested NMIs, we have three states:
 *
 *  1) not running
 *  2) executing
 *  3) latched
 *
 * When no NMI is in progress, it is in the "not running" state.
 * When an NMI comes in, it goes into the "executing" state.
 * Normally, if another NMI is triggered, it does not interrupt
 * the running NMI and the HW will simply latch it so that when
 * the first NMI finishes, it will restart the second NMI.
 * (Note, the latch is binary, thus multiple NMIs triggering,
 *  when one is running, are ignored. Only one NMI is restarted.)
 *
 * If an NMI executes an iret, another NMI can preempt it. We do not
 * want to allow this new NMI to run, but we want to execute it when the
 * first one finishes.  We set the state to "latched", and the exit of
 * the first NMI will perform a dec_return, if the result is zero
 * (NOT_RUNNING), then it will simply exit the NMI handler. If not, the
 * dec_return would have set the state to NMI_EXECUTING (what we want it
 * to be when we are running). In this case, we simply jump back to
 * rerun the NMI handler again, and restart the 'latched' NMI.
 *
 * No trap (breakpoint or page fault) should be hit before nmi_restart,
 * thus there is no race between the first check of state for NOT_RUNNING
 * and setting it to NMI_EXECUTING. The HW will prevent nested NMIs
 * at this point.
 *
 * In case the NMI takes a page fault, we need to save off the CR2
 * because the NMI could have preempted another page fault and corrupt
 * the CR2 that is about to be read. As nested NMIs must be restarted
 * and they can not take breakpoints or page faults, the update of the
 * CR2 must be done before converting the nmi state back to NOT_RUNNING.
 * Otherwise, there would be a race of another nested NMI coming in
 * after setting state to NOT_RUNNING but before updating the nmi_cr2.
 */
enum nmi_states {
	NMI_NOT_RUNNING = 0,
	NMI_EXECUTING,
	NMI_LATCHED,
};
static DEFINE_PER_CPU(enum nmi_states, nmi_state);
static DEFINE_PER_CPU(unsigned long, nmi_cr2);
static DEFINE_PER_CPU(unsigned long, nmi_dr7);

static void pci_serr_error(unsigned char reason, struct pt_regs *regs)
{
	/* Clear and disable the PCI SERR error line. */
	reason = (reason & NMI_REASON_CLEAR_MASK) | NMI_REASON_CLEAR_SERR;
	outb(reason, NMI_REASON_PORT);
}

static void io_check_error(unsigned char reason, struct pt_regs *regs)
{
	unsigned long i;

	/* Re-enable the IOCK line, wait for a few seconds */
	reason = (reason & NMI_REASON_CLEAR_MASK) | NMI_REASON_CLEAR_IOCHK;
	outb(reason, NMI_REASON_PORT);

	i = 20000;
	while (--i) {
		udelay(100);
	}

	reason &= ~NMI_REASON_CLEAR_IOCHK;
	outb(reason, NMI_REASON_PORT);
}

static noinstr void default_do_nmi(struct pt_regs *regs)
{
	unsigned char reason = 0;

	/*
	 * Non-CPU-specific NMI: NMI sources can be processed on any CPU.
	 */
	while (!raw_spin_trylock(&nmi_reason_lock)) {
		cpu_relax();
	}

	reason = default_get_nmi_reason();

	if (reason & NMI_REASON_MASK) {
		if (reason & NMI_REASON_SERR)
			pci_serr_error(reason, regs);
		else if (reason & NMI_REASON_IOCHK)
			io_check_error(reason, regs);
		/*
		 * Reassert NMI in case it became active
		 * meanwhile as it's edge-triggered:
		 */
		reassert_nmi();
		raw_spin_unlock(&nmi_reason_lock);
		return;
	}
	raw_spin_unlock(&nmi_reason_lock);
}

DEFINE_IDTENTRY_RAW(exc_nmi)
{
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

	default_do_nmi(regs);

	irqentry_nmi_exit(regs, irq_state);

	local_db_restore(this_cpu_read(nmi_dr7));

	if (unlikely(this_cpu_read(nmi_cr2) != read_cr2()))
		write_cr2(this_cpu_read(nmi_cr2));
	if (this_cpu_dec_return(nmi_state))
		goto nmi_restart;
}
