// SPDX-License-Identifier: GPL-2.0
#include <linux/ptrace.h>

#include <asm/switch_to.h>

#include <linux/cpu.h>

/*
 * per-CPU TSS segments. Threads are completely 'soft' on Linux,
 * no more per-task TSS's. The TSS size is kept cacheline-aligned
 * so they are allowed to end up in the .data..cacheline_aligned
 * section. Since TSS's are completely CPU-local, we want them
 * on exact cacheline boundaries, to eliminate cacheline ping-pong.
 */
__visible DEFINE_PER_CPU_PAGE_ALIGNED(struct tss_struct, cpu_tss_rw) = {
	.x86_tss = {
		.sp0 = TOP_OF_INIT_STACK,

		.ss0 = __KERNEL_DS,
		.io_bitmap_base	= IO_BITMAP_OFFSET_INVALID,
	 },
};

/*
 * this gets called so that we can store lazy state into memory and copy the
 * current task into the new thread.
 */
int arch_dup_task_struct(struct task_struct *dst, struct task_struct *src)
{
	memcpy(dst, src, sizeof(*dst));

	return 0;
}

int copy_thread(struct task_struct *p, const struct kernel_clone_args *args)
{
	struct inactive_task_frame *frame;
	struct fork_frame *fork_frame;
	struct pt_regs *childregs;
	int ret = 0;

	childregs = task_pt_regs(p);
	fork_frame = container_of(childregs, struct fork_frame, regs);
	frame = &fork_frame->frame;

	frame->bp = 0;
	frame->ret_addr = (unsigned long)ret_from_fork;
	p->thread.sp = (unsigned long)fork_frame;

	p->thread.sp0 = (unsigned long)(childregs + 1);
	savesegment(gs, p->thread.gs);

	/*
	 * Clear all status flags including IF and set fixed bit. 64bit
	 * does not have this initialization as the frame does not contain
	 * flags. The flags consistency (especially vs. AC) is there
	 * ensured via objtool, which lacks 32bit support.
	 */
	frame->flags = X86_EFLAGS_FIXED;

	frame->bx = 0;
	*childregs = *current_pt_regs();
	childregs->ax = 0;

	if (unlikely(args->fn)) {
		/*
		 * A user space thread, but it doesn't return to
		 * ret_after_fork().
		 *
		 * In order to indicate that to tools like gdb,
		 * we reset the stack and instruction pointers.
		 *
		 * It does the same kernel frame setup to return to a kernel
		 * function that a kernel thread does.
		 */
		childregs->sp = 0;
		childregs->ip = 0;
		kthread_frame_init(frame, args->fn, args->fn_arg);
		return 0;
	}

	return ret;
}

/*
 * Called from the generic idle code.
 */
void arch_cpu_idle(void)
{
	raw_safe_halt();
}
