
#include <linux/sched.h>

#include <asm/traps.h>

void send_sigtrap(struct pt_regs *regs, int error_code, int si_code)
{
	struct task_struct *tsk = current;

	tsk->thread.trap_nr = X86_TRAP_DB;


	force_sig_fault(SIGTRAP, si_code, user_mode(regs) ? (void __user *)regs->ip : NULL);
}
