

#ifndef LDT_ENTRY_SIZE
#define LDT_ENTRY_SIZE 8 /* inlined from asm/ldt.h */
#endif
#include <asm/fpu/sched.h>

#include <asm/tlbflush.h>
#include <asm/cpu.h>
#include <asm/switch_to.h>

#include <asm/kdebug.h>

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

	arch_end_context_switch(next_p);

	if (static_cpu_has(X86_FEATURE_XENPV))
		load_sp0(next_p->thread.sp0);
	else
		this_cpu_write(cpu_tss_rw.x86_tss.sp1, next_p->thread.sp0);
	this_cpu_write(cpu_current_top_of_stack,
		       (unsigned long)task_stack_page(next_p) + THREAD_SIZE);

	if (prev->gs | next->gs)
		loadsegment(gs, next->gs);

	this_cpu_write(current_task, next_p);

	switch_fpu_finish();
	return prev_p;
}

/* arch_prctl replaced with COND_SYSCALL */
