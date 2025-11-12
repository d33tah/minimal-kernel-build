// SPDX-License-Identifier: GPL-2.0-only
// Stubbed signal.c - minimal implementations for Hello World kernel

#include <linux/slab.h>
#include <linux/syscalls.h>
#include <linux/security.h>
#include <linux/signalfd.h>
#include <linux/sched/task_stack.h>
#include <linux/sched/cputime.h>
#include <linux/file.h>
#include <linux/freezer.h>
#include <linux/pid_namespace.h>
#include <linux/cgroup.h>
#include <linux/audit.h>
#include <linux/task_work.h>
#include <linux/coredump.h>
#include <linux/cn_proc.h>
#include <linux/tty.h>
#include <linux/proc_fs.h>

#include <trace/events/signal.h>

#include <asm/param.h>
#include <linux/uaccess.h>
#include <asm/unistd.h>
#include <asm/siginfo.h>
#include <asm/cacheflush.h>
#include <asm/syscall.h>

static struct kmem_cache *sigqueue_cachep;
int print_fatal_signals __read_mostly;

#define __si_special(priv) \
	((priv) ? SEND_SIG_PRIV : SEND_SIG_NOINFO)

void recalc_sigpending_and_wake(struct task_struct *t) {}
void recalc_sigpending(void) {}
EXPORT_SYMBOL(recalc_sigpending);

void calculate_sigpending(void) {}
int next_signal(struct sigpending *pending, sigset_t *mask) { return 0; }
bool task_set_jobctl_pending(struct task_struct *task, unsigned long mask) { return false; }
void task_clear_jobctl_trapping(struct task_struct *task) {}
void task_clear_jobctl_pending(struct task_struct *task, unsigned long mask) {}
void task_join_group_stop(struct task_struct *task) {}

void flush_sigqueue(struct sigpending *queue) {}
void flush_signals(struct task_struct *t) {}
EXPORT_SYMBOL(flush_signals);

void ignore_signals(struct task_struct *t) {}
bool unhandled_signal(struct task_struct *tsk, int sig) { return false; }

void flush_signal_handlers(struct task_struct *t, int force_default) {}

struct sighand_struct *__lock_task_sighand(struct task_struct *tsk,
					   unsigned long *flags)
{
	return tsk->sighand;
}

void __set_current_blocked(const sigset_t *newset) {}

void set_current_blocked(sigset_t *newset) {}

int restore_altstack(const stack_t __user *uss)
{
	return 0;
}

long do_no_restart_syscall(struct restart_block *param)
{
	return -EINTR;
}

int copy_siginfo_to_user(siginfo_t __user *to, const kernel_siginfo_t *from)
{
	return 0;
}

int copy_siginfo_from_user(kernel_siginfo_t *to, const siginfo_t __user *from)
{
	return 0;
}

int dequeue_signal(struct task_struct *tsk, sigset_t *mask, kernel_siginfo_t *info, enum pid_type *type)
{
	if (type)
		*type = PIDTYPE_PID;
	return 0;
}
EXPORT_SYMBOL_GPL(dequeue_signal);

void signal_wake_up_state(struct task_struct *t, unsigned int state) {}

int send_signal_locked(int sig, struct kernel_siginfo *info,
		       struct task_struct *t, enum pid_type type)
{
	return 0;
}

int do_send_sig_info(int sig, struct kernel_siginfo *info, struct task_struct *p,
		     enum pid_type type)
{
	return 0;
}

int force_sig_info(struct kernel_siginfo *info) { return 0; }
int zap_other_threads(struct task_struct *p) { return 0; }
int group_send_sig_info(int sig, struct kernel_siginfo *info,
			struct task_struct *p, enum pid_type type)
{
	return 0;
}

int __kill_pgrp_info(int sig, struct kernel_siginfo *info, struct pid *pgrp)
{
	return 0;
}

int kill_pid_info(int sig, struct kernel_siginfo *info, struct pid *pid)
{
	return 0;
}

int kill_pid_usb_asyncio(int sig, int errno, sigval_t addr,
			 struct pid *pid, const struct cred *cred)
{
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(kill_pid_usb_asyncio);

int send_sig_info(int sig, struct kernel_siginfo *info, struct task_struct *p)
{
	return 0;
}
EXPORT_SYMBOL(send_sig_info);

int send_sig(int sig, struct task_struct *p, int priv)
{
	return send_sig_info(sig, NULL, p);
}
EXPORT_SYMBOL(send_sig);

void force_sig(int sig) {}
EXPORT_SYMBOL(force_sig);

void force_fatal_sig(int sig) {}
void force_exit_sig(int sig) {}
void force_sigsegv(int sig) {}

int force_sig_fault_to_task(int sig, int code, void __user *addr,
			    struct task_struct *t)
{
	return 0;
}

int force_sig_fault(int sig, int code, void __user *addr)
{
	return force_sig_fault_to_task(sig, code, addr, current);
}

int send_sig_fault(int sig, int code, void __user *addr, struct task_struct *t)
{
	return 0;
}

int force_sig_mceerr(int code, void __user *addr, short lsb)
{
	return 0;
}

int send_sig_mceerr(int code, void __user *addr, short lsb, struct task_struct *t)
{
	return 0;
}
EXPORT_SYMBOL(send_sig_mceerr);

int force_sig_bnderr(void __user *addr, void __user *lower, void __user *upper)
{
	return 0;
}

int force_sig_pkuerr(void __user *addr, u32 pkey)
{
	return 0;
}

int send_sig_perf(void __user *addr, u32 type, u64 sig_data)
{
	return 0;
}

int force_sig_seccomp(int syscall, int reason, bool force_coredump)
{
	return 0;
}

int force_sig_ptrace_errno_trap(int errno, void __user *addr)
{
	return 0;
}

int force_sig_fault_trapno(int sig, int code, void __user *addr, int trapno)
{
	return 0;
}

int send_sig_fault_trapno(int sig, int code, void __user *addr, int trapno,
			  struct task_struct *t)
{
	return 0;
}

int kill_pgrp(struct pid *pid, int sig, int priv)
{
	return 0;
}
EXPORT_SYMBOL(kill_pgrp);

int kill_pid(struct pid *pid, int sig, int priv)
{
	return kill_pid_info(sig, __si_special(priv), pid);
}
EXPORT_SYMBOL(kill_pid);

void sigqueue_free(struct sigqueue *q) {}

int send_sigqueue(struct sigqueue *q, struct pid *pid, enum pid_type type)
{
	return 0;
}

bool do_notify_parent(struct task_struct *tsk, int sig)
{
	return false;
}

int ptrace_notify(int exit_code, unsigned long message)
{
	return 0;
}

bool get_signal(struct ksignal *ksig)
{
	return false;
}

void signal_setup_done(int failed, struct ksignal *ksig, int stepping) {}

void exit_signals(struct task_struct *tsk) {}

SYSCALL_DEFINE0(restart_syscall)
{
	return -EINTR;
}

int sigprocmask(int how, sigset_t *set, sigset_t *oldset)
{
	return 0;
}
EXPORT_SYMBOL(sigprocmask);

SYSCALL_DEFINE4(rt_sigprocmask, int, how, sigset_t __user *, nset,
		sigset_t __user *, oset, size_t, sigsetsize)
{
	return 0;
}

SYSCALL_DEFINE2(rt_sigpending, sigset_t __user *, uset, size_t, sigsetsize)
{
	return 0;
}

SYSCALL_DEFINE4(rt_sigtimedwait, const sigset_t __user *, uthese,
		siginfo_t __user *, uinfo,
		const struct __kernel_timespec __user *, uts,
		size_t, sigsetsize)
{
	return -EINTR;
}

SYSCALL_DEFINE2(kill, pid_t, pid, int, sig)
{
	return 0;
}

SYSCALL_DEFINE4(pidfd_send_signal, int, pidfd, int, sig,
		siginfo_t __user *, info, unsigned int, flags)
{
	return -ENOSYS;
}

SYSCALL_DEFINE3(tgkill, pid_t, tgid, pid_t, pid, int, sig)
{
	return 0;
}

SYSCALL_DEFINE2(tkill, pid_t, pid, int, sig)
{
	if (pid <= 0)
		return -EINVAL;
	return 0;
}

SYSCALL_DEFINE3(rt_sigqueueinfo, pid_t, pid, int, sig,
		siginfo_t __user *, uinfo)
{
	return 0;
}

SYSCALL_DEFINE4(rt_tgsigqueueinfo, pid_t, tgid, pid_t, pid, int, sig,
		siginfo_t __user *, uinfo)
{
	return 0;
}

void kernel_sigaction(int sig, __sighandler_t handler)
{
	/* Stub - do nothing */
}
EXPORT_SYMBOL(kernel_sigaction);

SYSCALL_DEFINE2(sigaltstack,const stack_t __user *,uss, stack_t __user *,uoss)
{
	return 0;
}

SYSCALL_DEFINE1(sigpending, old_sigset_t __user *, uset)
{
	return 0;
}

SYSCALL_DEFINE3(sigprocmask, int, how, old_sigset_t __user *, nset,
		old_sigset_t __user *, oset)
{
	return 0;
}

SYSCALL_DEFINE4(rt_sigaction, int, sig,
		const struct sigaction __user *, act,
		struct sigaction __user *, oact,
		size_t, sigsetsize)
{
	return 0;
}

#ifdef CONFIG_OLD_SIGACTION
SYSCALL_DEFINE3(sigaction, int, sig,
		const struct old_sigaction __user *, act,
		struct old_sigaction __user *, oact)
{
	return 0;
}
#endif

#ifdef CONFIG_SGETMASK_SYSCALL
SYSCALL_DEFINE0(sgetmask)
{
	return 0;
}

SYSCALL_DEFINE1(ssetmask, int, newmask)
{
	return 0;
}
#endif

#ifdef CONFIG_OLD_SIGSUSPEND
SYSCALL_DEFINE1(sigsuspend, old_sigset_t, mask)
{
	return -EINTR;
}
#endif

#ifdef CONFIG_OLD_SIGSUSPEND3
SYSCALL_DEFINE3(sigsuspend, int, unused1, int, unused2, old_sigset_t, mask)
{
	return -EINTR;
}
#endif

__weak const char *arch_vma_name(struct vm_area_struct *vma)
{
	return NULL;
}

#ifdef CONFIG_COMPAT
COMPAT_SYSCALL_DEFINE4(rt_sigaction, int, sig,
		const struct compat_sigaction __user *, act,
		struct compat_sigaction __user *, oact,
		compat_size_t, sigsetsize)
{
	return 0;
}
#endif

SYSCALL_DEFINE2(signal, int, sig, __sighandler_t, handler)
{
	return 0;
}

SYSCALL_DEFINE0(pause)
{
	while (!signal_pending(current)) {
		__set_current_state(TASK_INTERRUPTIBLE);
		schedule();
	}
	return -ERESTARTNOHAND;
}

SYSCALL_DEFINE2(rt_sigsuspend, sigset_t __user *, unewset, size_t, sigsetsize)
{
	return -EINTR;
}

#ifdef CONFIG_OLD_SIGACTION
SYSCALL_DEFINE3(old_sigprocmask, int, how,
		old_sigset_t __user *, nset,
		old_sigset_t __user *, oset)
{
	return 0;
}
#endif

#ifdef CONFIG_COMPAT
COMPAT_SYSCALL_DEFINE3(sigprocmask, int, how,
		compat_old_sigset_t __user *, nset,
		compat_old_sigset_t __user *, oset)
{
	return 0;
}
#endif

void __init signals_init(void)
{
	sigqueue_cachep = KMEM_CACHE(sigqueue, SLAB_PANIC | SLAB_ACCOUNT);
}
