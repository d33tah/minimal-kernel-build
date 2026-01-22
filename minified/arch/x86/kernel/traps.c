

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/kprobes.h>
#include <linux/uaccess.h>
#include <linux/kdebug.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/ptrace.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/init.h>
#include <linux/bug.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/io.h>
#include <linux/hardirq.h>
#include <linux/atomic.h>

#include <asm/stacktrace.h>
#include <asm/processor.h>
#include <asm/debugreg.h>
#include <asm/realmode.h>
#include <asm/text-patching.h>
#include <asm/traps.h>
#include <asm/desc.h>
#include <asm/fpu/api.h>
#include <asm/cpu.h>
#include <asm/cpu_entry_area.h>
/* mce.h removed - header is empty */
#include <asm/fixmap.h>
#include <asm/mach_traps.h>
#include <asm/alternative.h>
#include <asm/fpu/xstate.h>
#include <asm/vm86.h>
#include <asm/vdso.h>
#include <asm/tdx.h>

#include <asm/processor-flags.h>
#include <asm/setup.h>
#include <asm/proto.h>

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

__always_inline int is_valid_bugaddr(unsigned long addr)
{
	if (addr < TASK_SIZE_MAX)
		return 0;

	return *(unsigned short *)addr == INSN_UD2;
}

static nokprobe_inline int do_trap_no_signal(struct task_struct *tsk,
					     int trapnr, const char *str,
					     struct pt_regs *regs,
					     long error_code)
{
	if (v8086_mode(regs)) {
		/* handle_vm86_trap always returns 0 */
		if (trapnr < X86_TRAP_UD)
			return 0;
	} else if (!user_mode(regs)) {
		if (fixup_exception(regs, trapnr, error_code, 0))
			return 0;

		tsk->thread.error_code = error_code;
		tsk->thread.trap_nr = trapnr;
		die(str, regs, error_code);
	} else {
		if (fixup_vdso_exception(regs, trapnr, error_code, 0))
			return 0;
	}

	tsk->thread.error_code = error_code;
	tsk->thread.trap_nr = trapnr;

	return -1;
}

static void do_trap(int trapnr, int signr, char *str, struct pt_regs *regs,
		    long error_code, int sicode, void __user *addr)
{
	struct task_struct *tsk = current;

	if (!do_trap_no_signal(tsk, trapnr, str, regs, error_code))
		return;

	if (!sicode)
		force_sig(signr);
	else
		force_sig_fault(signr, sicode, addr);
}
NOKPROBE_SYMBOL(do_trap);

static void do_error_trap(struct pt_regs *regs, long error_code, char *str,
			  unsigned long trapnr, int signr, int sicode,
			  void __user *addr)
{
	/* notify_die always returns NOTIFY_DONE - check removed */
	notify_die(DIE_TRAP, str, regs, error_code, trapnr, signr);
	cond_local_irq_enable(regs);
	do_trap(trapnr, signr, str, regs, error_code, sicode, addr);
	cond_local_irq_disable(regs);
}

static __always_inline void __user *error_get_trap_addr(struct pt_regs *regs)
{
	return (void __user *)uprobe_get_trap_addr(regs);
}

DEFINE_IDTENTRY(exc_divide_error)
{
	do_error_trap(regs, 0, "divide error", X86_TRAP_DE, SIGFPE, FPE_INTDIV,
		      error_get_trap_addr(regs));
}

DEFINE_IDTENTRY(exc_overflow)
{
	do_error_trap(regs, 0, "overflow", X86_TRAP_OF, SIGSEGV, 0, NULL);
}

/* handle_invalid_op inlined into exc_invalid_op */

static noinstr bool handle_bug(struct pt_regs *regs)
{
	bool handled = false;

	if (!is_valid_bugaddr(regs->ip))
		return handled;

	if (regs->flags & X86_EFLAGS_IF)
		raw_local_irq_enable();
	if (report_bug(regs->ip, regs) == BUG_TRAP_TYPE_WARN) {
		regs->ip += LEN_UD2;
		handled = true;
	}
	if (regs->flags & X86_EFLAGS_IF)
		raw_local_irq_disable();

	return handled;
}

DEFINE_IDTENTRY_RAW(exc_invalid_op)
{
	irqentry_state_t state;

	if (!user_mode(regs) && handle_bug(regs))
		return;

	state = irqentry_enter(regs);
	/* Inlined handle_invalid_op */
	do_error_trap(regs, 0, "invalid opcode", X86_TRAP_UD, SIGILL,
		      ILL_ILLOPN, error_get_trap_addr(regs));
	irqentry_exit(regs, state);
}

DEFINE_IDTENTRY(exc_coproc_segment_overrun)
{
	do_error_trap(regs, 0, "coprocessor segment overrun", X86_TRAP_OLD_MF,
		      SIGFPE, 0, NULL);
}

DEFINE_IDTENTRY_ERRORCODE(exc_invalid_tss)
{
	do_error_trap(regs, error_code, "invalid TSS", X86_TRAP_TS, SIGSEGV, 0,
		      NULL);
}

DEFINE_IDTENTRY_ERRORCODE(exc_segment_not_present)
{
	do_error_trap(regs, error_code, "segment not present", X86_TRAP_NP,
		      SIGBUS, 0, NULL);
}

DEFINE_IDTENTRY_ERRORCODE(exc_stack_segment)
{
	do_error_trap(regs, error_code, "stack segment", X86_TRAP_SS, SIGBUS, 0,
		      NULL);
}

DEFINE_IDTENTRY_ERRORCODE(exc_alignment_check)
{
	/* notify_die always returns NOTIFY_DONE - check removed */
	notify_die(DIE_TRAP, "alignment check", regs, error_code, X86_TRAP_AC,
		   SIGBUS);

	if (!user_mode(regs))
		die("Split lock detected\n", regs, error_code);

	local_irq_enable();

	do_trap(X86_TRAP_AC, SIGBUS, "alignment check", regs, error_code,
		BUS_ADRALN, NULL);

	local_irq_disable();
}

DEFINE_IDTENTRY_DF(exc_double_fault)
{
	static const char str[] = "double fault";
	struct task_struct *tsk = current;

	irqentry_nmi_enter(regs);
	notify_die(DIE_TRAP, str, regs, error_code, X86_TRAP_DF, SIGSEGV);

	tsk->thread.error_code = error_code;
	tsk->thread.trap_nr = X86_TRAP_DF;

	pr_emerg("PANIC: double fault, error_code: 0x%lx\n", error_code);
	die("double fault", regs, error_code);
	panic("Machine halted.");
}

DEFINE_IDTENTRY(exc_bounds)
{
	/* notify_die always returns NOTIFY_DONE - check removed */
	notify_die(DIE_TRAP, "bounds", regs, 0, X86_TRAP_BR, SIGSEGV);
	cond_local_irq_enable(regs);

	if (!user_mode(regs))
		die("bounds", regs, 0);

	do_trap(X86_TRAP_BR, SIGSEGV, "bounds", regs, 0, 0, NULL);

	cond_local_irq_disable(regs);
}

/* get_kernel_gp_address and enum kernel_gp_hint removed - always returned GP_NO_HINT (~8 LOC) */

#define GPFSTR "general protection fault"

static bool gp_try_fixup_and_notify(struct pt_regs *regs, int trapnr,
				    unsigned long error_code, const char *str)
{
	if (fixup_exception(regs, trapnr, error_code, 0))
		return true;

	current->thread.error_code = error_code;
	current->thread.trap_nr = trapnr;

	/* kprobe_running() always returns NULL when CONFIG_KPROBES is disabled */
	/* notify_die always returns NOTIFY_DONE */
	notify_die(DIE_GPF, str, regs, error_code, trapnr, SIGSEGV);
	return false;
}

static void gp_user_force_sig_segv(struct pt_regs *regs, int trapnr,
				   unsigned long error_code, const char *str)
{
	current->thread.error_code = error_code;
	current->thread.trap_nr = trapnr;
	force_sig(SIGSEGV);
}

DEFINE_IDTENTRY_ERRORCODE(exc_general_protection)
{
	char desc[sizeof(GPFSTR) + 50 + 2 * sizeof(unsigned long) + 1] = GPFSTR;
	/* hint, gp_addr simplified - get_kernel_gp_address always returned GP_NO_HINT */

	cond_local_irq_enable(regs);

	if (v8086_mode(regs)) {
		local_irq_enable();
		handle_vm86_fault((struct kernel_vm86_regs *)regs, error_code);
		local_irq_disable();
		return;
	}

	if (user_mode(regs)) {
		if (fixup_vdso_exception(regs, X86_TRAP_GP, error_code, 0))
			goto exit;

		gp_user_force_sig_segv(regs, X86_TRAP_GP, error_code, desc);
		goto exit;
	}

	if (gp_try_fixup_and_notify(regs, X86_TRAP_GP, error_code, desc))
		goto exit;

	if (error_code)
		snprintf(desc, sizeof(desc), "segment-related " GPFSTR);
	/* get_kernel_gp_address call and hint conditionals removed - always GP_NO_HINT (~10 LOC) */

	die_addr(desc, regs, error_code, 0);

exit:
	cond_local_irq_disable(regs);
}

/* notify_die always returns NOTIFY_DONE, so do_int3 always returns false */
static bool do_int3(struct pt_regs *regs)
{
	notify_die(DIE_INT3, "int3", regs, 0, X86_TRAP_BP, SIGTRAP);
	return false;
}
NOKPROBE_SYMBOL(do_int3);

static void do_int3_user(struct pt_regs *regs)
{
	/* do_int3 always returns false - early return removed */
	do_int3(regs);
	cond_local_irq_enable(regs);
	do_trap(X86_TRAP_BP, SIGTRAP, "int3", regs, 0, 0, NULL);
	cond_local_irq_disable(regs);
}

DEFINE_IDTENTRY_RAW(exc_int3)
{
	/* poke_int3_handler check removed - always returned 0 */
	if (user_mode(regs)) {
		irqentry_enter_from_user_mode(regs);
		do_int3_user(regs);
		irqentry_exit_to_user_mode(regs);
	} else {
		irqentry_state_t irq_state = irqentry_nmi_enter(regs);

		/* do_int3 always returns false - unconditional die */
		do_int3(regs);
		die("int3", regs, 0);
		irqentry_nmi_exit(regs, irq_state);
	}
}

/* is_sysenter_singlestep inlined into exc_debug_kernel */
/* debug_read_clear_dr6 inlined into exc_debug_user */

/* notify_die always returns NOTIFY_DONE, so notify_debug always returns false */
static bool notify_debug(struct pt_regs *regs, unsigned long *dr6)
{
	notify_die(DIE_DEBUG, "debug", regs, (long)dr6, 0, SIGTRAP);
	return false;
}

static __always_inline void exc_debug_kernel(struct pt_regs *regs,
					     unsigned long dr6)
{
	unsigned long dr7 = local_db_save();
	irqentry_state_t irq_state = irqentry_nmi_enter(regs);

	WARN_ON_ONCE(user_mode(regs));

	if (test_thread_flag(TIF_BLOCKSTEP)) {
		unsigned long debugctl;

		rdmsrl(MSR_IA32_DEBUGCTLMSR, debugctl);
		debugctl |= DEBUGCTLMSR_BTF;
		wrmsrl(MSR_IA32_DEBUGCTLMSR, debugctl);
	}

	/* is_sysenter_singlestep inlined */
	if ((dr6 & DR_STEP) &&
	    (regs->ip - (unsigned long)__begin_SYSENTER_singlestep_region) <
		    (unsigned long)__end_SYSENTER_singlestep_region -
			    (unsigned long)__begin_SYSENTER_singlestep_region)
		dr6 &= ~DR_STEP;

	if (!dr6)
		goto out;

	/* notify_debug always returns false - check removed */
	notify_debug(regs, &dr6);

	if (WARN_ON_ONCE(dr6 & DR_STEP))
		regs->flags &= ~X86_EFLAGS_TF;
out:
	irqentry_nmi_exit(regs, irq_state);

	local_db_restore(dr7);
}

static __always_inline void exc_debug_user(struct pt_regs *regs,
					   unsigned long dr6)
{
	bool icebp;

	WARN_ON_ONCE(!user_mode(regs));

	irqentry_enter_from_user_mode(regs);

	current->thread.virtual_dr6 = (dr6 & DR_STEP);

	clear_thread_flag(TIF_BLOCKSTEP);

	icebp = !dr6;

	if (notify_debug(regs, &dr6))
		goto out;

	local_irq_enable();

	if (v8086_mode(regs))
		/* handle_vm86_trap always returns 0 */
		goto out_irq;

	/* handle_bus_lock removed - was empty stub in intel.c */

	dr6 |= current->thread.virtual_dr6;
	if (dr6 & (DR_STEP | DR_TRAP_BITS) || icebp)
		send_sigtrap(regs, 0, get_si_code(dr6));

out_irq:
	local_irq_disable();
out:
	irqentry_exit_to_user_mode(regs);
}

DEFINE_IDTENTRY_RAW(exc_debug)
{
	unsigned long dr6;

	/* debug_read_clear_dr6 inlined */
	get_debugreg(dr6, 6);
	set_debugreg(DR6_RESERVED, 6);
	dr6 ^= DR6_RESERVED;

	if (user_mode(regs))
		exc_debug_user(regs, dr6);
	else
		exc_debug_kernel(regs, dr6);
}

static void math_error(struct pt_regs *regs, int trapnr)
{
	struct task_struct *task = current;
	struct fpu *fpu = &task->thread.fpu;
	int si_code;
	char *str = (trapnr == X86_TRAP_MF) ? "fpu exception" :
					      "simd exception";

	cond_local_irq_enable(regs);

	if (!user_mode(regs)) {
		if (fixup_exception(regs, trapnr, 0, 0))
			goto exit;

		task->thread.error_code = 0;
		task->thread.trap_nr = trapnr;

		/* notify_die always returns NOTIFY_DONE - check removed */
		notify_die(DIE_TRAP, str, regs, 0, trapnr, SIGFPE);
		die(str, regs, 0);
		goto exit;
	}

	fpu_sync_fpstate(fpu);

	task->thread.trap_nr = trapnr;
	task->thread.error_code = 0;

	si_code = fpu__exception_code(fpu, trapnr);

	if (!si_code)
		goto exit;

	if (fixup_vdso_exception(regs, trapnr, 0, 0))
		goto exit;

	force_sig_fault(SIGFPE, si_code,
			(void __user *)uprobe_get_trap_addr(regs));
exit:
	cond_local_irq_disable(regs);
}

DEFINE_IDTENTRY(exc_coprocessor_error)
{
	math_error(regs, X86_TRAP_MF);
}

DEFINE_IDTENTRY(exc_simd_coprocessor_error)
{
	/* X86_INVD_BUG disabled */
	math_error(regs, X86_TRAP_XF);
}

DEFINE_IDTENTRY(exc_spurious_interrupt_bug)
{
}

/* handle_xfd_event removed - always returned false (~5 LOC) */

DEFINE_IDTENTRY(exc_device_not_available)
{
	unsigned long cr0 = read_cr0();
	/* handle_xfd_event check removed - function always returned false */

	if (WARN(cr0 & X86_CR0_TS, "CR0.TS was set")) {
		write_cr0(cr0 & ~X86_CR0_TS);
	} else {
		die("unexpected #NM exception", regs, 0);
	}
}

DEFINE_IDTENTRY_SW(iret_error)
{
	local_irq_enable();
	/* notify_die always returns NOTIFY_DONE - check removed */
	notify_die(DIE_TRAP, "iret exception", regs, 0, X86_TRAP_IRET, SIGILL);
	do_trap(X86_TRAP_IRET, SIGILL, "iret exception", regs, 0, ILL_BADSTK,
		(void __user *)NULL);
	local_irq_disable();
}

void __init trap_init(void)
{
	setup_cpu_entry_areas();

	/* sev_es_init_vc_handling removed - empty stub */

	cpu_init_exception_handling();

	idt_setup_traps();
	cpu_init();
}
