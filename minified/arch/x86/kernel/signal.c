 
 

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
#include <linux/user-return-notifier.h>
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
#include <asm/sighandling.h>
#include <asm/vm86.h>


#include <asm/syscall.h>
#include <asm/sigframe.h>
#include <asm/signal.h>

# define CONTEXT_COPY_SIZE	sizeof(struct sigcontext)

static bool restore_sigcontext(struct pt_regs *regs,
			       struct sigcontext __user *usc,
			       unsigned long uc_flags)
{
	struct sigcontext sc;

	 
	current->restart_block.fn = do_no_restart_syscall;

	if (copy_from_user(&sc, usc, CONTEXT_COPY_SIZE))
		return false;

	loadsegment(gs, sc.gs);
	regs->fs = sc.fs;
	regs->es = sc.es;
	regs->ds = sc.ds;

	regs->bx = sc.bx;
	regs->cx = sc.cx;
	regs->dx = sc.dx;
	regs->si = sc.si;
	regs->di = sc.di;
	regs->bp = sc.bp;
	regs->ax = sc.ax;
	regs->sp = sc.sp;
	regs->ip = sc.ip;


	 
	regs->cs = sc.cs | 0x03;
	regs->ss = sc.ss | 0x03;

	regs->flags = (regs->flags & ~FIX_EFLAGS) | (sc.flags & FIX_EFLAGS);
	 
	regs->orig_ax = -1;


	return fpu__restore_sig((void __user *)sc.fpstate,
			       IS_ENABLED(CONFIG_X86_32));
}

static __always_inline int
__unsafe_setup_sigcontext(struct sigcontext __user *sc, void __user *fpstate,
		     struct pt_regs *regs, unsigned long mask)
{
	unsigned int gs;
	savesegment(gs, gs);

	unsafe_put_user(gs,	  (unsigned int __user *)&sc->gs, Efault);
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

#define unsafe_put_sigcontext(sc, fp, regs, set, label)			\
do {									\
	if (__unsafe_setup_sigcontext(sc, fp, regs, set->sig[0]))	\
		goto label;						\
} while(0);

#define unsafe_put_sigmask(set, frame, label) \
	unsafe_put_user(*(__u64 *)(set), \
			(__u64 __user *)&(frame)->uc.uc_sigmask, \
			label)

 

 
#define FRAME_ALIGNMENT	16UL

#define MAX_FRAME_PADDING	(FRAME_ALIGNMENT - 1)

 
static unsigned long align_sigframe(unsigned long sp)
{
	 
	sp = ((sp + 4) & -FRAME_ALIGNMENT) - 4;
	return sp;
}

static void __user *
get_sigframe(struct k_sigaction *ka, struct pt_regs *regs, size_t frame_size,
	     void __user **fpstate)
{
	 
	bool nested_altstack = on_sig_stack(regs->sp);
	bool entering_altstack = false;
	unsigned long math_size = 0;
	unsigned long sp = regs->sp;
	unsigned long buf_fx = 0;

	 
	if (IS_ENABLED(CONFIG_X86_64))
		sp -= 128;

	 
	if (ka->sa.sa_flags & SA_ONSTACK) {
		 
		if (sas_ss_flags(sp) == 0) {
			sp = current->sas_ss_sp + current->sas_ss_size;
			entering_altstack = true;
		}
	} else if (IS_ENABLED(CONFIG_X86_32) &&
		   !nested_altstack &&
		   regs->ss != __USER_DS &&
		   !(ka->sa.sa_flags & SA_RESTORER) &&
		   ka->sa.sa_restorer) {
		 
		sp = (unsigned long) ka->sa.sa_restorer;
		entering_altstack = true;
	}

	sp = fpu__alloc_mathframe(sp, IS_ENABLED(CONFIG_X86_32),
				  &buf_fx, &math_size);
	*fpstate = (void __user *)sp;

	sp = align_sigframe(sp - frame_size);

	 
	if (unlikely((nested_altstack || entering_altstack) &&
		     !__on_sig_stack(sp))) {

		if (show_unhandled_signals && printk_ratelimit())
			pr_info("%s[%d] overflowed sigaltstack\n",
				current->comm, task_pid_nr(current));

		return (void __user *)-1L;
	}

	 
	if (!copy_fpstate_to_sigframe(*fpstate, (void __user *)buf_fx, math_size))
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
	u8  movl;
	u32 val;
	u16 int80;
	u8  pad;
} __attribute__((packed)) rt_retcode = {
	0xb8,		 
	__NR_rt_sigreturn,
	0x80cd,		 
	0
};

static int
__setup_frame(int sig, struct ksignal *ksig, sigset_t *set,
	      struct pt_regs *regs)
{
	struct sigframe __user *frame;
	void __user *restorer;
	void __user *fp = NULL;

	frame = get_sigframe(&ksig->ka, regs, sizeof(*frame), &fp);

	if (!user_access_begin(frame, sizeof(*frame)))
		return -EFAULT;

	unsafe_put_user(sig, &frame->sig, Efault);
	unsafe_put_sigcontext(&frame->sc, fp, regs, set, Efault);
	unsafe_put_user(set->sig[1], &frame->extramask[0], Efault);
	if (current->mm->context.vdso)
		restorer = current->mm->context.vdso +
			vdso_image_32.sym___kernel_sigreturn;
	else
		restorer = &frame->retcode;
	if (ksig->ka.sa.sa_flags & SA_RESTORER)
		restorer = ksig->ka.sa.sa_restorer;

	 
	unsafe_put_user(restorer, &frame->pretcode, Efault);

	 
	unsafe_put_user(*((u64 *)&retcode), (u64 *)frame->retcode, Efault);
	user_access_end();

	 
	regs->sp = (unsigned long)frame;
	regs->ip = (unsigned long)ksig->ka.sa.sa_handler;
	regs->ax = (unsigned long)sig;
	regs->dx = 0;
	regs->cx = 0;

	regs->ds = __USER_DS;
	regs->es = __USER_DS;
	regs->ss = __USER_DS;
	regs->cs = __USER_CS;

	return 0;

Efault:
	user_access_end();
	return -EFAULT;
}

static int __setup_rt_frame(int sig, struct ksignal *ksig,
			    sigset_t *set, struct pt_regs *regs)
{
	struct rt_sigframe __user *frame;
	void __user *restorer;
	void __user *fp = NULL;

	frame = get_sigframe(&ksig->ka, regs, sizeof(*frame), &fp);

	if (!user_access_begin(frame, sizeof(*frame)))
		return -EFAULT;

	unsafe_put_user(sig, &frame->sig, Efault);
	unsafe_put_user(&frame->info, &frame->pinfo, Efault);
	unsafe_put_user(&frame->uc, &frame->puc, Efault);

	 
	if (static_cpu_has(X86_FEATURE_XSAVE))
		unsafe_put_user(UC_FP_XSTATE, &frame->uc.uc_flags, Efault);
	else
		unsafe_put_user(0, &frame->uc.uc_flags, Efault);
	unsafe_put_user(0, &frame->uc.uc_link, Efault);
	unsafe_save_altstack(&frame->uc.uc_stack, regs->sp, Efault);

	 
	restorer = current->mm->context.vdso +
		vdso_image_32.sym___kernel_rt_sigreturn;
	if (ksig->ka.sa.sa_flags & SA_RESTORER)
		restorer = ksig->ka.sa.sa_restorer;
	unsafe_put_user(restorer, &frame->pretcode, Efault);

	 
	unsafe_put_user(*((u64 *)&rt_retcode), (u64 *)frame->retcode, Efault);
	unsafe_put_sigcontext(&frame->uc.uc_mcontext, fp, regs, set, Efault);
	unsafe_put_sigmask(set, frame, Efault);
	user_access_end();
	
	if (copy_siginfo_to_user(&frame->info, &ksig->info))
		return -EFAULT;

	 
	regs->sp = (unsigned long)frame;
	regs->ip = (unsigned long)ksig->ka.sa.sa_handler;
	regs->ax = (unsigned long)sig;
	regs->dx = (unsigned long)&frame->info;
	regs->cx = (unsigned long)&frame->uc;

	regs->ds = __USER_DS;
	regs->es = __USER_DS;
	regs->ss = __USER_DS;
	regs->cs = __USER_CS;

	return 0;
Efault:
	user_access_end();
	return -EFAULT;
}


static int x32_setup_rt_frame(struct ksignal *ksig,
			      compat_sigset_t *set,
			      struct pt_regs *regs)
{

	return 0;
}

 
SYSCALL_DEFINE0(sigreturn)
{
	struct pt_regs *regs = current_pt_regs();
	struct sigframe __user *frame;
	sigset_t set;

	frame = (struct sigframe __user *)(regs->sp - 8);

	if (!access_ok(frame, sizeof(*frame)))
		goto badframe;
	if (__get_user(set.sig[0], &frame->sc.oldmask) ||
	    __get_user(set.sig[1], &frame->extramask[0]))
		goto badframe;

	set_current_blocked(&set);

	 
	if (!restore_sigcontext(regs, &frame->sc, 0))
		goto badframe;
	return regs->ax;

badframe:
	signal_fault(regs, frame, "sigreturn");

	return 0;
}

SYSCALL_DEFINE0(rt_sigreturn)
{
	struct pt_regs *regs = current_pt_regs();
	struct rt_sigframe __user *frame;
	sigset_t set;
	unsigned long uc_flags;

	frame = (struct rt_sigframe __user *)(regs->sp - sizeof(long));
	if (!access_ok(frame, sizeof(*frame)))
		goto badframe;
	if (__get_user(*(__u64 *)&set, (__u64 __user *)&frame->uc.uc_sigmask))
		goto badframe;
	if (__get_user(uc_flags, &frame->uc.uc_flags))
		goto badframe;

	set_current_blocked(&set);

	if (!restore_sigcontext(regs, &frame->uc.uc_mcontext, uc_flags))
		goto badframe;

	if (restore_altstack(&frame->uc.uc_stack))
		goto badframe;

	return regs->ax;

badframe:
	signal_fault(regs, frame, "rt_sigreturn");
	return 0;
}

 
# define MAX_FRAME_SIGINFO_UCTXT_SIZE	sizeof(struct sigframe_ia32)

 
#define MAX_XSAVE_PADDING	63UL

 

 
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

static inline int is_ia32_compat_frame(struct ksignal *ksig)
{
	return IS_ENABLED(CONFIG_IA32_EMULATION) &&
		ksig->ka.sa.sa_flags & SA_IA32_ABI;
}

static inline int is_ia32_frame(struct ksignal *ksig)
{
	return IS_ENABLED(CONFIG_X86_32) || is_ia32_compat_frame(ksig);
}

static inline int is_x32_frame(struct ksignal *ksig)
{
	return IS_ENABLED(CONFIG_X86_X32_ABI) &&
		ksig->ka.sa.sa_flags & SA_X32_ABI;
}

static int
setup_rt_frame(struct ksignal *ksig, struct pt_regs *regs)
{
	int usig = ksig->sig;
	sigset_t *set = sigmask_to_save();
	compat_sigset_t *cset = (compat_sigset_t *) set;

	 
	rseq_signal_deliver(ksig, regs);

	 
	if (is_ia32_frame(ksig)) {
		if (ksig->ka.sa.sa_flags & SA_SIGINFO)
			return ia32_setup_rt_frame(usig, ksig, cset, regs);
		else
			return ia32_setup_frame(usig, ksig, cset, regs);
	} else if (is_x32_frame(ksig)) {
		return x32_setup_rt_frame(ksig, cset, regs);
	} else {
		return __setup_rt_frame(ksig->sig, ksig, set, regs);
	}
}

static void
handle_signal(struct ksignal *ksig, struct pt_regs *regs)
{
	bool stepping, failed;
	struct fpu *fpu = &current->thread.fpu;

	if (v8086_mode(regs))
		save_v86_state((struct kernel_vm86_regs *) regs, VM86_SIGNAL);

	 
	if (syscall_get_nr(current, regs) != -1) {
		 
		switch (syscall_get_error(current, regs)) {
		case -ERESTART_RESTARTBLOCK:
		case -ERESTARTNOHAND:
			regs->ax = -EINTR;
			break;

		case -ERESTARTSYS:
			if (!(ksig->ka.sa.sa_flags & SA_RESTART)) {
				regs->ax = -EINTR;
				break;
			}
			fallthrough;
		case -ERESTARTNOINTR:
			regs->ax = regs->orig_ax;
			regs->ip -= 2;
			break;
		}
	}

	 
	stepping = test_thread_flag(TIF_SINGLESTEP);
	if (stepping)
		user_disable_single_step(current);

	failed = (setup_rt_frame(ksig, regs) < 0);
	if (!failed) {
		 
		regs->flags &= ~(X86_EFLAGS_DF|X86_EFLAGS_RF|X86_EFLAGS_TF);
		 
		fpu__clear_user_states(fpu);
	}
	signal_setup_done(failed, ksig, stepping);
}

static inline unsigned long get_nr_restart_syscall(const struct pt_regs *regs)
{
	return __NR_restart_syscall;
}

 
void arch_do_signal_or_restart(struct pt_regs *regs)
{
	struct ksignal ksig;

	if (get_signal(&ksig)) {
		 
		handle_signal(&ksig, regs);
		return;
	}

	 
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

void signal_fault(struct pt_regs *regs, void __user *frame, char *where)
{
	struct task_struct *me = current;

	if (show_unhandled_signals && printk_ratelimit()) {
		printk("%s"
		       "%s[%d] bad frame in %s frame:%p ip:%lx sp:%lx orax:%lx",
		       task_pid_nr(current) > 1 ? KERN_INFO : KERN_EMERG,
		       me->comm, me->pid, where, frame,
		       regs->ip, regs->sp, regs->orig_ax);
		print_vma_addr(KERN_CONT " in ", regs->ip);
		pr_cont("\n");
	}

	force_sig(SIGSEGV);
}

static bool strict_sigaltstack_size __ro_after_init = false;

static int __init strict_sas_size(char *arg)
{
	return kstrtobool(arg, &strict_sigaltstack_size);
}
__setup("strict_sas_size", strict_sas_size);

 
bool sigaltstack_size_valid(size_t ss_size)
{
	unsigned long fsize = max_frame_size - fpu_default_state_size;
	u64 mask;

	lockdep_assert_held(&current->sighand->siglock);

	if (!fpu_state_size_dynamic() && !strict_sigaltstack_size)
		return true;

	fsize += current->group_leader->thread.fpu.perm.__user_state_size;
	if (likely(ss_size > fsize))
		return true;

	if (strict_sigaltstack_size)
		return ss_size > fsize;

	mask = current->group_leader->thread.fpu.perm.__state_perm;
	if (mask & XFEATURE_MASK_USER_DYNAMIC)
		return ss_size > fsize;

	return true;
}

