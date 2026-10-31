
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

	/*
	 * report_bug() is an unconditional stub returning BUG_TRAP_TYPE_BUG
	 * on this !CONFIG_GENERIC_BUG build, so the old handle_bug() WARN
	 * fixup never recovered (always returned false). Drop it.
	 */
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

	/*
	 * UMIP is in DISABLED_MASK16 (never enabled) and fixup_umip_exception
	 * was a return-false stub, so the UMIP GP fixup was dead -- dropped.
	 */

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

	get_debugreg(dr6, 6);
	set_debugreg(DR6_RESERVED, 6);
	dr6 ^= DR6_RESERVED;

	return dr6;
}

static __always_inline void exc_debug_kernel(struct pt_regs *regs,
					     unsigned long dr6)
{
	unsigned long dr7 = local_db_save();
	irqentry_state_t irq_state = irqentry_nmi_enter(regs);

	/* TIF_BLOCKSTEP is never set in this build -> branch was always false. */

	if (!dr6)
		goto out;

	if (WARN_ON_ONCE(dr6 & DR_STEP))
		regs->flags &= ~X86_EFLAGS_TF;
out:
	irqentry_nmi_exit(regs, irq_state);

	local_db_restore(dr7);
}

static __always_inline void exc_debug_user(struct pt_regs *regs,
					   unsigned long dr6)
{
	/* TIF_BLOCKSTEP never set in this build -> clear was a no-op. */

	/* Single-step or icebp from user mode: SIGTRAP, i.e. kill the task. */
	do_exit(SIGTRAP);
}

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
}

DEFINE_IDTENTRY(exc_device_not_available)
{
	unsigned long cr0 = read_cr0();

	if (WARN(cr0 & X86_CR0_TS, "CR0.TS was set")) {
		write_cr0(cr0 & ~X86_CR0_TS);
	} else {
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
	cpu_init_exception_handling();

	idt_setup_traps();
	cpu_init();
}
