
/* end stackprotector.h */
#include <linux/ptrace.h>

#include <asm/switch_to.h>

#include <linux/cpu.h>

__visible DEFINE_PER_CPU_PAGE_ALIGNED(struct tss_struct, cpu_tss_rw) = { .x86_tss = { .sp0 = TOP_OF_INIT_STACK, .ss0 = __KERNEL_DS, .io_bitmap_base	= IO_BITMAP_OFFSET_INVALID, }, };

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

	frame->flags = X86_EFLAGS_FIXED;

	frame->bx = 0;
	*childregs = *current_pt_regs();
	childregs->ax = 0;

	if (unlikely(args->fn)) {
		childregs->sp = 0;
		childregs->ip = 0;
		kthread_frame_init(frame, args->fn, args->fn_arg);
		return 0;
	}

	return ret;
}

/* __switch_to_xtra + __speculation_ctrl_update (+ amd_set_*_ssb_state) removed -
 * switch_to_extra()'s extra-work test was always false (its TIF mask bits are
 * never set in this build), so __switch_to_xtra was never called. */

void arch_cpu_idle(void)
{
	raw_safe_halt();
}
