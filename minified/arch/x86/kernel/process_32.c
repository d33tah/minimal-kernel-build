

#include <linux/cpu.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/sched/task_stack.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/stddef.h>
/* linux/slab.h removed - no slab functions */
/* linux/vmalloc.h removed - no vmalloc functions */
/* linux/interrupt.h, linux/reboot.h, linux/export.h removed - unused */
#include <linux/ptrace.h>
#include <linux/personality.h>
#include <linux/percpu.h>
#include <linux/uaccess.h>
/* linux/io.h removed - no I/O functions */
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
#include <asm/proto.h>

/* resctrl_sched_in removed - empty stub */

#include <asm/spec-ctrl.h>
#include <asm/kdebug.h>
void __switch_to_xtra(struct task_struct *prev_p, struct task_struct *next_p);

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
	struct fpu *prev_fpu = &prev->fpu;
	int cpu = smp_processor_id();

	if (!test_thread_flag(TIF_NEED_FPU_LOAD))
		switch_fpu_prepare(prev_fpu, cpu);

	savesegment(gs, prev->gs);

	load_TLS(next, cpu);

	{
		unsigned long next_tif = read_task_thread_flags(next_p);
		unsigned long prev_tif = read_task_thread_flags(prev_p);
		if (unlikely(next_tif & _TIF_WORK_CTXSW_NEXT ||
			     prev_tif & _TIF_WORK_CTXSW_PREV))
			__switch_to_xtra(prev_p, next_p);
	}

	arch_end_context_switch(next_p);

	/* update_task_stack inlined */
	if (static_cpu_has(X86_FEATURE_XENPV))
		load_sp0(next_p->thread.sp0);
	else
		this_cpu_write(cpu_tss_rw.x86_tss.sp1, next_p->thread.sp0);
	/* refresh_sysenter_cs inlined */
	if (unlikely(this_cpu_read(cpu_tss_rw.x86_tss.ss1) !=
		     next->sysenter_cs)) {
		this_cpu_write(cpu_tss_rw.x86_tss.ss1, next->sysenter_cs);
		wrmsr(MSR_IA32_SYSENTER_CS, next->sysenter_cs, 0);
	}
	this_cpu_write(cpu_current_top_of_stack,
		       (unsigned long)task_stack_page(next_p) + THREAD_SIZE);

	if (prev->gs | next->gs)
		loadsegment(gs, next->gs);

	this_cpu_write(current_task, next_p);

	switch_fpu_finish();
	/* resctrl_sched_in removed - empty stub */
	return prev_p;
}

/* arch_prctl replaced with COND_SYSCALL */
