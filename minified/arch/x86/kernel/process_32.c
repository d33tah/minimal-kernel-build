/*
 *  Copyright (C) 1995  Linus Torvalds
 *
 *  Pentium III FXSR, SSE support
 *	Gareth Hughes <gareth@valinux.com>, May 2000
 */

/*
 * This file handles the architecture-dependent parts of process handling..
 */

#include <linux/sched/task_stack.h>
#include <linux/ptrace.h>

#include <asm/switch_to.h>

void start_thread(struct pt_regs *regs, unsigned long new_ip,
		  unsigned long new_sp)
{
	loadsegment(gs, 0);
	regs->fs = 0;
	regs->ds = __USER_DS;
	regs->es = __USER_DS;
	regs->ss = __USER_DS;
	regs->cs = __USER_CS;
	regs->ip = new_ip;
	regs->sp = new_sp;
	regs->flags = X86_EFLAGS_IF;
}

__visible struct task_struct *__switch_to(struct task_struct *prev_p,
					  struct task_struct *next_p)
{
	struct thread_struct *prev = &prev_p->thread, *next = &next_p->thread;

	/*
	 * Save away %gs. No need to save %fs, as it was saved on the
	 * stack on entry.  No need to save %es and %ds, as those are
	 * always kernel segments while inside the kernel.  Doing this
	 * before setting the new TLS descriptors avoids the situation
	 * where we temporarily have non-reloadable segments in %fs
	 * and %gs.  This could be an issue if the NMI handler ever
	 * used %fs or %gs (it does not today), or if the kernel is
	 * running inside of a hypervisor layer.
	 */
	savesegment(gs, prev->gs);

	/* Entries from user mode land on the top of the next task's stack. */
	load_sp0(next->sp0);
	this_cpu_write(cpu_current_top_of_stack,
		       (unsigned long)task_stack_page(next_p) + THREAD_SIZE);

	/*
	 * Restore %gs if needed (which is common)
	 */
	if (prev->gs | next->gs)
		loadsegment(gs, next->gs);

	this_cpu_write(current_task, next_p);

	return prev_p;
}
