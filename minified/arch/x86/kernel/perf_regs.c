 
 
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/perf_event.h>
#include <asm/perf_regs.h>
#include <asm/ptrace.h>

u64 perf_reg_value(struct pt_regs *regs, int idx)
{
	return 0;
}

int perf_reg_validate(u64 mask)
{
	return -EINVAL;
}

u64 perf_reg_abi(struct task_struct *task)
{
	return 0;
}

void perf_get_regs_user(struct perf_regs *regs_user, struct pt_regs *regs)
{
	regs_user->regs = NULL;
	regs_user->abi = 0;
}
