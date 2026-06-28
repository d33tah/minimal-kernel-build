#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/sched/idle.h>
#include <linux/sched/task.h>
#include <linux/sched/task_stack.h>
#include <linux/random.h>
/* end stackprotector.h */
#include <asm/cpu.h>
#include <asm/apic.h>
#include <asm/fpu/api.h>
#include <asm/fpu/sched.h>
#include <asm/fpu/xstate.h>
#include <asm/debugreg.h>
#include <asm/tlbflush.h>
#include <asm/switch_to.h>
#include <asm/desc.h>
#include <asm/proto.h>
/* --- 2025-12-07 20:47 --- Inlined frame.h */
#include <asm/asm.h>
#define FRAME_BEGIN
#define FRAME_END
#define FRAME_OFFSET 0
#include <asm/unwind.h>

#include "process.h"

__visible DEFINE_PER_CPU_PAGE_ALIGNED(struct tss_struct, cpu_tss_rw) = {
	.x86_tss = {
		 
		.sp0 = (1UL << (BITS_PER_LONG-1)) + 1,

		.sp1 = TOP_OF_INIT_STACK,

		.ss0 = __KERNEL_DS,
		.ss1 = __KERNEL_CS,
		.io_bitmap_base	= IO_BITMAP_OFFSET_INVALID,
	 },
};

int arch_dup_task_struct(struct task_struct *dst, struct task_struct *src)
{
	memcpy(dst, src, arch_task_struct_size);
	 
	dst->thread.fpu.fpstate = NULL;

	return 0;
}


void exit_thread(struct task_struct *tsk)
{
	struct thread_struct *t = &tsk->thread;
	struct fpu *fpu = &t->fpu;

	fpu__drop(fpu);
}

int copy_thread(struct task_struct *p, const struct kernel_clone_args *args)
{
	unsigned long sp = args->stack;
	struct inactive_task_frame *frame;
	struct fork_frame *fork_frame;
	struct pt_regs *childregs;
	int ret = 0;

	childregs = task_pt_regs(p);
	fork_frame = container_of(childregs, struct fork_frame, regs);
	frame = &fork_frame->frame;

	frame->bp = 0;
	frame->ret_addr = (unsigned long) ret_from_fork;
	p->thread.sp = (unsigned long) fork_frame;

	p->thread.sp0 = (unsigned long) (childregs + 1);
	savesegment(gs, p->thread.gs);
	 
	frame->flags = X86_EFLAGS_FIXED;

	fpu_clone(p, args->fn);

	 
	if (unlikely(p->flags & PF_KTHREAD)) {
		memset(childregs, 0, sizeof(struct pt_regs));
		kthread_frame_init(frame, args->fn, args->fn_arg);
		return 0;
	}


	frame->bx = 0;
	*childregs = *current_pt_regs();
	childregs->ax = 0;
	if (sp)
		childregs->sp = sp;

	if (unlikely(args->fn)) {
		 
		childregs->sp = 0;
		childregs->ip = 0;
		kthread_frame_init(frame, args->fn, args->fn_arg);
		return 0;
	}

	return ret;
}

void flush_thread(void)
{
	struct task_struct *tsk = current;

	memset(tsk->thread.tls_array, 0, sizeof(tsk->thread.tls_array));

	fpu_flush_thread();
}


void arch_setup_new_exec(void)
{
	/* TIF_NOCPUID is never set in this build, so the cpuid-faulting
	 * enable path (set_cpuid_faulting/msr_misc_features_shadow) was
	 * dead -> folded away. */
}

/* __switch_to_xtra + __speculation_ctrl_update (+ amd_set_*_ssb_state) removed -
 * switch_to_extra()'s extra-work test was always false (its TIF mask bits are
 * never set in this build), so __switch_to_xtra was never called. */

static void (*x86_idle)(void);

/* arch_cpu_idle_enter/arch_cpu_idle_dead (+ play_dead) removed - unused
 * (idle-loop body never runs; weak stubs in kernel/sched/idle.c suffice) */

void arch_cpu_idle(void)
{
	x86_idle();
}

void __cpuidle default_idle(void)
{
	raw_safe_halt();
}

/* Simplified: just use default_idle for minimal kernel */
void select_idle_routine(const struct cpuinfo_x86 *c)
{
	if (!x86_idle)
		x86_idle = default_idle;
}


unsigned long arch_align_stack(unsigned long sp)
{
	if (!(current->personality & ADDR_NO_RANDOMIZE))
		sp -= get_random_int() % 8192;
	return sp & ~0xf;
}

unsigned long arch_randomize_brk(struct mm_struct *mm)
{
	return randomize_page(mm->brk, 0x02000000);
}

