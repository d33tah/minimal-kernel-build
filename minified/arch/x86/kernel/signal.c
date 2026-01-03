
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/kernel.h>
#include <linux/kstrtox.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/unistd.h>
#include <linux/stddef.h>
#include <linux/personality.h>
#include <linux/uaccess.h>
#include <linux/uprobes.h>
#include <linux/context_tracking.h>
#include <linux/entry-common.h>
#include <linux/syscalls.h>

#include <asm/processor.h>
#include <asm/ucontext.h>
#include <asm/fpu/signal.h>
#include <asm/fpu/xstate.h>
#include <asm/vdso.h>
#include <asm/mce.h>
#include <asm/vm86.h>

/* Inlined from asm/sighandling.h */
#define FIX_EFLAGS                                                       \
	(X86_EFLAGS_AC | X86_EFLAGS_OF | X86_EFLAGS_DF | X86_EFLAGS_TF | \
	 X86_EFLAGS_SF | X86_EFLAGS_ZF | X86_EFLAGS_AF | X86_EFLAGS_PF | \
	 X86_EFLAGS_CF | X86_EFLAGS_RF)
void signal_fault(struct pt_regs *regs, void __user *frame, char *where);

#include <asm/syscall.h>
#include <asm/sigframe.h>
#include <asm/signal.h>

#define CONTEXT_COPY_SIZE sizeof(struct sigcontext)

static __always_inline int
__unsafe_setup_sigcontext(struct sigcontext __user *sc, void __user *fpstate,
			  struct pt_regs *regs, unsigned long mask)
{
	unsigned int gs;
	savesegment(gs, gs);

	unsafe_put_user(gs, (unsigned int __user *)&sc->gs, Efault);
	unsafe_put_user(regs->fs, (unsigned int __user *)&sc->fs, Efault);
	unsafe_put_user(regs->es, (unsigned int __user *)&sc->es, Efault);
	unsafe_put_user(regs->ds, (unsigned int __user *)&sc->ds, Efault);

	unsafe_put_user(regs->di, &sc->di, Efault);
	unsafe_put_user(regs->si, &sc->si, Efault);
	unsafe_put_user(regs->bp, &sc->bp, Efault);
	unsafe_put_user(regs->sp, &sc->sp, Efault);
	unsafe_put_user(regs->bx, &sc->bx, Efault);
	unsafe_put_user(regs->dx, &sc->dx, Efault);
	unsafe_put_user(regs->cx, &sc->cx, Efault);
	unsafe_put_user(regs->ax, &sc->ax, Efault);

	unsafe_put_user(current->thread.trap_nr, &sc->trapno, Efault);
	unsafe_put_user(current->thread.error_code, &sc->err, Efault);
	unsafe_put_user(regs->ip, &sc->ip, Efault);
	unsafe_put_user(regs->cs, (unsigned int __user *)&sc->cs, Efault);
	unsafe_put_user(regs->flags, &sc->flags, Efault);
	unsafe_put_user(regs->sp, &sc->sp_at_signal, Efault);
	unsafe_put_user(regs->ss, (unsigned int __user *)&sc->ss, Efault);

	unsafe_put_user(fpstate, (unsigned long __user *)&sc->fpstate, Efault);

	unsafe_put_user(mask, &sc->oldmask, Efault);
	unsafe_put_user(current->thread.cr2, &sc->cr2, Efault);
	return 0;
Efault:
	return -EFAULT;
}

#define unsafe_put_sigcontext(sc, fp, regs, set, label)                   \
	do {                                                              \
		if (__unsafe_setup_sigcontext(sc, fp, regs, set->sig[0])) \
			goto label;                                       \
	} while (0);

#define unsafe_put_sigmask(set, frame, label) \
	unsafe_put_user(*(__u64 *)(set),      \
			(__u64 __user *)&(frame)->uc.uc_sigmask, label)

#define FRAME_ALIGNMENT 16UL

#define MAX_FRAME_PADDING (FRAME_ALIGNMENT - 1)

static unsigned long align_sigframe(unsigned long sp)
{
	sp = ((sp + 4) & -FRAME_ALIGNMENT) - 4;
	return sp;
}

static void __user *get_sigframe(struct k_sigaction *ka, struct pt_regs *regs,
				 size_t frame_size, void __user **fpstate)
{
	bool nested_altstack = on_sig_stack(regs->sp);
	bool entering_altstack = false;
	unsigned long math_size = 0;
	unsigned long sp = regs->sp;
	unsigned long buf_fx = 0;

	if (ka->sa.sa_flags & SA_ONSTACK) {
		if (sas_ss_flags(sp) == 0) {
			sp = current->sas_ss_sp + current->sas_ss_size;
			entering_altstack = true;
		}
	} else if (!nested_altstack && regs->ss != __USER_DS &&
		   !(ka->sa.sa_flags & SA_RESTORER) && ka->sa.sa_restorer) {
		sp = (unsigned long)ka->sa.sa_restorer;
		entering_altstack = true;
	}

	sp = fpu__alloc_mathframe(sp, true, &buf_fx, &math_size);
	*fpstate = (void __user *)sp;

	sp = align_sigframe(sp - frame_size);

	if (unlikely((nested_altstack || entering_altstack) &&
		     !__on_sig_stack(sp))) {
		/* printk_ratelimit() always returns 0 */
		return (void __user *)-1L;
	}

	if (!copy_fpstate_to_sigframe(*fpstate, (void __user *)buf_fx,
				      math_size))
		return (void __user *)-1L;

	return (void __user *)sp;
}

static const struct {
	u16 poplmovl;
	u32 val;
	u16 int80;
} __attribute__((packed)) retcode = {
	0xb858,
	__NR_sigreturn,
	0x80cd,
};

static const struct {
	u8 movl;
	u32 val;
	u16 int80;
	u8 pad;
} __attribute__((packed)) rt_retcode = { 0xb8, __NR_rt_sigreturn, 0x80cd, 0 };

/* __setup_frame, __setup_rt_frame removed - never called (get_signal returns false) */

/* Stub: sigreturn not needed for Hello World */
SYSCALL_DEFINE0(sigreturn)
{
	return 0;
}

/* Stub: rt_sigreturn not needed for Hello World */
SYSCALL_DEFINE0(rt_sigreturn)
{
	return 0;
}

#define MAX_FRAME_SIGINFO_UCTXT_SIZE sizeof(struct sigframe_ia32)

#define MAX_XSAVE_PADDING 63UL

static unsigned long __ro_after_init max_frame_size;
static unsigned int __ro_after_init fpu_default_state_size;

void __init init_sigframe_size(void)
{
	fpu_default_state_size = fpu__get_fpstate_size();

	max_frame_size = MAX_FRAME_SIGINFO_UCTXT_SIZE + MAX_FRAME_PADDING;

	max_frame_size += fpu_default_state_size + MAX_XSAVE_PADDING;

	max_frame_size = round_up(max_frame_size, FRAME_ALIGNMENT);

	pr_info("max sigframe size: %lu\n", max_frame_size);
}

unsigned long get_sigframe_size(void)
{
	return max_frame_size;
}

/* setup_rt_frame, handle_signal removed - get_signal always returns false */

static inline unsigned long get_nr_restart_syscall(const struct pt_regs *regs)
{
	return __NR_restart_syscall;
}

void arch_do_signal_or_restart(struct pt_regs *regs)
{
	/* get_signal always returns false - signal handling dead code removed */

	if (syscall_get_nr(current, regs) != -1) {
		switch (syscall_get_error(current, regs)) {
		case -ERESTARTNOHAND:
		case -ERESTARTSYS:
		case -ERESTARTNOINTR:
			regs->ax = regs->orig_ax;
			regs->ip -= 2;
			break;

		case -ERESTART_RESTARTBLOCK:
			regs->ax = get_nr_restart_syscall(regs);
			regs->ip -= 2;
			break;
		}
	}

	restore_saved_sigmask();
}

/* Stub: signal_fault not used externally */
void signal_fault(struct pt_regs *regs, void __user *frame, char *where)
{
	force_sig(SIGSEGV);
}
