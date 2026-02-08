/* Minimal ptrace - only send_sigtrap and user_single_step_report needed */
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/signal.h>
#include <asm/traps.h>

/* user_regset_view, x86_32_regsets, genregs_get/set, putreg/getreg,
 * set_flags/get_flags, set_segment_reg/get_segment_reg, pt_regs_access,
 * invalid_selector, FLAG_MASK, ioperm_active/get, x86_regset enum
 * all removed - never called externally */

void send_sigtrap(struct pt_regs *regs, int error_code, int si_code)
{
	struct task_struct *tsk = current;

	tsk->thread.trap_nr = X86_TRAP_DB;
	tsk->thread.error_code = error_code;

	force_sig_fault(SIGTRAP, si_code,
			user_mode(regs) ? (void __user *)regs->ip : NULL);
}

/* user_single_step_report removed - ptrace_report_syscall_exit stubbed */
