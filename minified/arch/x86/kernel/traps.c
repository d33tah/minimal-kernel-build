/*
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 2000, 2001, 2002 Andi Kleen, SuSE Labs
 *
 *  Pentium III FXSR, SSE support
 *	Gareth Hughes <gareth@valinux.com>, May 2000
 */

/*
 * Handle hardware traps and faults.
 */

#include <asm/kdebug.h>

#include <asm/traps.h>

DECLARE_BITMAP(system_vectors, NR_VECTORS);

static inline void cond_local_irq_enable(struct pt_regs *regs)
{
	if (regs->flags & X86_EFLAGS_IF)
		local_irq_enable();
}

static inline void cond_local_irq_disable(struct pt_regs *regs)
{
	if (regs->flags & X86_EFLAGS_IF)
		local_irq_disable();
}

/*
 * There is no signal delivery: a user-mode exception kills the task with the
 * signal upstream would send, instead of returning to re-execute the faulting
 * instruction forever (or silently skipping a trap).
 */
static void do_trap(int trapnr, int signr, char *str, struct pt_regs *regs,
		    long error_code)
{
	if (user_mode(regs))
		do_exit(signr);
	if (!fixup_exception(regs, trapnr, error_code, 0))
		die(str, regs, error_code);
}

static void do_error_trap(struct pt_regs *regs, long error_code, char *str,
			  unsigned long trapnr, int signr)
{
	cond_local_irq_enable(regs);
	do_trap(trapnr, signr, str, regs, error_code);
	cond_local_irq_disable(regs);
}

DEFINE_IDTENTRY(exc_divide_error)
{
	do_error_trap(regs, 0, "divide error", X86_TRAP_DE, SIGFPE);
}

DEFINE_IDTENTRY(exc_overflow)
{
	do_error_trap(regs, 0, "overflow", X86_TRAP_OF, SIGSEGV);
}

DEFINE_IDTENTRY_RAW(exc_invalid_op)
{
	irqentry_state_t state;

	state = irqentry_enter(regs);
	do_error_trap(regs, 0, "invalid opcode", X86_TRAP_UD, SIGILL);
	irqentry_exit(regs, state);
}

DEFINE_IDTENTRY(exc_coproc_segment_overrun)
{
	do_error_trap(regs, 0, "coprocessor segment overrun", X86_TRAP_OLD_MF,
		      SIGFPE);
}

DEFINE_IDTENTRY_ERRORCODE(exc_invalid_tss)
{
	do_error_trap(regs, error_code, "invalid TSS", X86_TRAP_TS, SIGSEGV);
}

DEFINE_IDTENTRY_ERRORCODE(exc_segment_not_present)
{
	do_error_trap(regs, error_code, "segment not present", X86_TRAP_NP,
		      SIGBUS);
}

DEFINE_IDTENTRY_ERRORCODE(exc_stack_segment)
{
	do_error_trap(regs, error_code, "stack segment", X86_TRAP_SS, SIGBUS);
}

DEFINE_IDTENTRY_ERRORCODE(exc_alignment_check)
{
	if (!user_mode(regs))
		die("Split lock detected\n", regs, error_code);

	local_irq_enable();

	do_trap(X86_TRAP_AC, SIGBUS, "alignment check", regs, error_code);

	local_irq_disable();
}

/*
 * Runs on an IST stack for x86_64 and on a special task stack for x86_32.
 *
 * On x86_64, this is more or less a normal kernel entry.  Notwithstanding the
 * SDM's warnings about double faults being unrecoverable, returning works as
 * expected.  Presumably what the SDM actually means is that the CPU may get
 * the register state wrong on entry, so returning could be a bad idea.
 *
 * Various CPU engineers have promised that double faults due to an IRET fault
 * while the stack is read-only are, in fact, recoverable.
 *
 * On x86_32, this is entered through a task gate, and regs are synthesized
 * from the TSS.  Returning is, in principle, okay, but changes to regs will
 * be lost.  If, for some reason, we need to return to a context with modified
 * regs, the shim code could be adjusted to synchronize the registers.
 *
 * The 32bit #DF shim provides CR2 already as an argument. On 64bit it needs
 * to be read before doing anything else.
 */
DEFINE_IDTENTRY_DF(exc_double_fault)
{
	irqentry_nmi_enter(regs);

	die("double fault", regs, error_code);
	panic("Machine halted.");
}

DEFINE_IDTENTRY(exc_bounds)
{
	cond_local_irq_enable(regs);

	if (!user_mode(regs))
		die("bounds", regs, 0);

	do_trap(X86_TRAP_BR, SIGSEGV, "bounds", regs, 0);

	cond_local_irq_disable(regs);
}

#define GPFSTR "general protection fault"

DEFINE_IDTENTRY_ERRORCODE(exc_general_protection)
{
	cond_local_irq_enable(regs);

	if (user_mode(regs))
		do_exit(SIGSEGV); /* no signal delivery */

	if (fixup_exception(regs, X86_TRAP_GP, error_code, 0))
		goto exit;

	/* The address hint was always GP_NO_HINT, so gp_addr was always 0. */
	die_addr(error_code ? "segment-related " GPFSTR : GPFSTR, regs,
		 error_code, 0);

exit:
	cond_local_irq_disable(regs);
}

DEFINE_IDTENTRY_RAW(exc_int3)
{
	if (user_mode(regs)) {
		cond_local_irq_enable(regs);
		do_trap(X86_TRAP_BP, SIGTRAP, "int3", regs, 0);
		cond_local_irq_disable(regs);
		irqentry_exit_to_user_mode(regs);
	} else {
		irqentry_state_t irq_state = irqentry_nmi_enter(regs);

		die("int3", regs, 0);
		irqentry_nmi_exit(regs, irq_state);
	}
}

static __always_inline unsigned long debug_read_clear_dr6(void)
{
	unsigned long dr6;

	/*
	 * The Intel SDM says:
	 *
	 *   Certain debug exceptions may clear bits 0-3. The remaining
	 *   contents of the DR6 register are never cleared by the
	 *   processor. To avoid confusion in identifying debug
	 *   exceptions, debug handlers should clear the register before
	 *   returning to the interrupted task.
	 *
	 * Keep it simple: clear DR6 immediately.
	 */
	get_debugreg(dr6, 6);
	set_debugreg(DR6_RESERVED, 6);
	dr6 ^= DR6_RESERVED; /* Flip to positive polarity */

	return dr6;
}

static __always_inline void exc_debug_kernel(struct pt_regs *regs,
					     unsigned long dr6)
{
	/*
	 * Disable breakpoints during exception handling; recursive exceptions
	 * are exceedingly 'fun'.
	 *
	 * Since this function is NOKPROBE, and that also applies to
	 * HW_BREAKPOINT_X, we can't hit a breakpoint before this (XXX except a
	 * HW_BREAKPOINT_W on our stack)
	 *
	 * Entry text is excluded for HW_BP_X and cpu_entry_area, which
	 * includes the entry stack is excluded for everything.
	 */
	unsigned long dr7 = local_db_save();
	irqentry_state_t irq_state = irqentry_nmi_enter(regs);

	/*
	 * The kernel doesn't use INT1
	 */
	if (!dr6)
		goto out;

	/*
	 * The kernel doesn't use TF single-step outside of:
	 *
	 *  - Kprobes, consumed through kprobe_debug_handler()
	 *  - KGDB, consumed through notify_debug()
	 *
	 * So if we get here with DR_STEP set, something is wonky.
	 *
	 * A known way to trigger this is through QEMU's GDB stub,
	 * which leaks #DB into the guest and causes IST recursion.
	 */
	if (WARN_ON_ONCE(dr6 & DR_STEP))
		regs->flags &= ~X86_EFLAGS_TF;
out:
	irqentry_nmi_exit(regs, irq_state);

	local_db_restore(dr7);
}

static __always_inline void exc_debug_user(struct pt_regs *regs,
					   unsigned long dr6)
{
	/* Single-step or icebp from user mode: SIGTRAP, i.e. kill the task. */
	do_exit(SIGTRAP);
}

/* 32 bit does not have separate entry points. */
DEFINE_IDTENTRY_RAW(exc_debug)
{
	unsigned long dr6 = debug_read_clear_dr6();

	if (user_mode(regs))
		exc_debug_user(regs, dr6);
	else
		exc_debug_kernel(regs, dr6);
}

/* The kernel itself never uses the FPU; a user-mode #MF/#XM kills the task. */
DEFINE_IDTENTRY(exc_coprocessor_error)
{
	if (user_mode(regs))
		do_exit(SIGFPE);
}

DEFINE_IDTENTRY(exc_simd_coprocessor_error)
{
	if (user_mode(regs))
		do_exit(SIGFPE);
}

DEFINE_IDTENTRY(exc_spurious_interrupt_bug)
{
	/*
	 * This addresses a Pentium Pro Erratum:
	 *
	 * PROBLEM: If the APIC subsystem is configured in mixed mode with
	 * Virtual Wire mode implemented through the local APIC, an
	 * interrupt vector of 0Fh (Intel reserved encoding) may be
	 * generated by the local APIC (Int 15).  This vector may be
	 * generated upon receipt of a spurious interrupt (an interrupt
	 * which is removed before the system receives the INTA sequence)
	 * instead of the programmed 8259 spurious interrupt vector.
	 *
	 * IMPLICATION: The spurious interrupt vector programmed in the
	 * 8259 is normally handled by an operating system's spurious
	 * interrupt handler. However, a vector of 0Fh is unknown to some
	 * operating systems, which would crash if this erratum occurred.
	 *
	 * In theory this could be limited to 32bit, but the handler is not
	 * hurting and who knows which other CPUs suffer from this.
	 */
}

DEFINE_IDTENTRY(exc_device_not_available)
{
	unsigned long cr0 = read_cr0();

	/* This should not happen. */
	if (WARN(cr0 & X86_CR0_TS, "CR0.TS was set")) {
		/* Try to fix it up and carry on. */
		write_cr0(cr0 & ~X86_CR0_TS);
	} else {
		/*
		 * Something terrible happened, and we're better off trying
		 * to kill the task than getting stuck in a never-ending
		 * loop of #NM faults.
		 */
		die("unexpected #NM exception", regs, 0);
	}
}

DEFINE_IDTENTRY_SW(iret_error)
{
	local_irq_enable();
	do_trap(X86_TRAP_IRET, SIGILL, "iret exception", regs, 0);
	local_irq_disable();
}

void __init trap_init(void)
{
	/* Initialize TSS before setting up traps so ISTs work */
	cpu_init_exception_handling();

	/* Setup traps as cpu_init() might #GP */
	idt_setup_traps();
	cpu_init();
}
