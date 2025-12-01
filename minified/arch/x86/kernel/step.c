#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/mm.h>
#include <linux/ptrace.h>
#include <asm/desc.h>
#include <asm/mmu_context.h>

unsigned long convert_ip_to_linear(struct task_struct *child, struct pt_regs *regs)
{
	unsigned long addr, seg;

	addr = regs->ip;
	seg = regs->cs;
	if (v8086_mode(regs)) {
		addr = (addr & 0xffff) + (seg << 4);
		return addr;
	}

	return addr;
}

void set_task_blockstep(struct task_struct *task, bool on)
{
	 
}

void user_enable_single_step(struct task_struct *child)
{
	 
}

void user_enable_block_step(struct task_struct *child)
{
	 
}

void user_disable_single_step(struct task_struct *child)
{
	 
}
