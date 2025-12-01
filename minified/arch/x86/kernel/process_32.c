

#include <linux/cpu.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/sched/task_stack.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/elfcore.h>
#include <linux/smp.h>
#include <linux/stddef.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/user.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/reboot.h>
#include <linux/mc146818rtc.h>
#include <linux/export.h>
#include <linux/kallsyms.h>
#include <linux/ptrace.h>
#include <linux/personality.h>
#include <linux/percpu.h>
#include <linux/prctl.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/kdebug.h>
#include <linux/syscalls.h>

#include <asm/ldt.h>
#include <asm/processor.h>
#include <asm/fpu/sched.h>
#include <asm/desc.h>

#include <linux/err.h>

#include <asm/tlbflush.h>
#include <asm/cpu.h>
#include <asm/debugreg.h>
#include <asm/switch_to.h>
#include <asm/vm86.h>
#include <asm/resctrl.h>
#include <asm/proto.h>

#include "process.h"

void __show_regs(struct pt_regs *regs, enum show_regs_mode mode,
		 const char *log_lvl)
{
	/* Stub: register dump not needed for minimal kernel */
}

void release_thread(struct task_struct *dead_task)
{
	BUG_ON(dead_task->mm);
	release_vm86_irqs(dead_task);
}

void
start_thread(struct pt_regs *regs, unsigned long new_ip, unsigned long new_sp)
{
	loadsegment(gs, 0);
	regs->fs		= 0;
	regs->ds		= __USER_DS;
	regs->es		= __USER_DS;
	regs->ss		= __USER_DS;
	regs->cs		= __USER_CS;
	regs->ip		= new_ip;
	regs->sp		= new_sp;
	regs->flags		= X86_EFLAGS_IF;
}


__visible struct task_struct *
__switch_to(struct task_struct *prev_p, struct task_struct *next_p)
{
	struct thread_struct *prev = &prev_p->thread,
			     *next = &next_p->thread;
	struct fpu *prev_fpu = &prev->fpu;
	int cpu = smp_processor_id();

	 

	if (!test_thread_flag(TIF_NEED_FPU_LOAD))
		switch_fpu_prepare(prev_fpu, cpu);

	 
	savesegment(gs, prev->gs);

	 
	load_TLS(next, cpu);

	switch_to_extra(prev_p, next_p);

	 
	arch_end_context_switch(next_p);

	 
	update_task_stack(next_p);
	refresh_sysenter_cs(next);
	this_cpu_write(cpu_current_top_of_stack,
		       (unsigned long)task_stack_page(next_p) +
		       THREAD_SIZE);

	 
	if (prev->gs | next->gs)
		loadsegment(gs, next->gs);

	this_cpu_write(current_task, next_p);

	switch_fpu_finish();

	 
	resctrl_sched_in();

	return prev_p;
}

SYSCALL_DEFINE2(arch_prctl, int, option, unsigned long, arg2)
{
	return do_arch_prctl_common(option, arg2);
}
