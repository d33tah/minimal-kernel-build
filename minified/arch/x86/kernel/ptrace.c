
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/signal.h>

#include <linux/uaccess.h>
#include <asm/processor.h>
#include <asm/traps.h>

void send_sigtrap(struct pt_regs *regs, int error_code, int si_code)
{
	struct task_struct *tsk = current;

	tsk->thread.trap_nr = X86_TRAP_DB;
	tsk->thread.error_code = error_code;


	force_sig_fault(SIGTRAP, si_code,
			user_mode(regs) ? (void __user *)regs->ip : NULL);
}

void user_single_step_report(struct pt_regs *regs)
{
	send_sigtrap(regs, 0, TRAP_BRKPT);
}
