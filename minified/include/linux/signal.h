#ifndef _LINUX_SIGNAL_H
#define _LINUX_SIGNAL_H

#include <linux/bug.h>
#include <linux/signal_types.h>
#include <linux/string.h>

struct task_struct;

/* Removed: print_fatal_signals - never used */

static inline void copy_siginfo(kernel_siginfo_t *to,
				const kernel_siginfo_t *from)
{
	memcpy(to, from, sizeof(*to));
}

static inline void clear_siginfo(kernel_siginfo_t *info)
{
	memset(info, 0, sizeof(*info));
}


/* copy_siginfo_to_user removed - never called */

#ifndef __HAVE_ARCH_SIG_BITOPS
#include <linux/bitops.h>

static inline void sigaddset(sigset_t *set, int _sig)
{
	unsigned long sig = _sig - 1;
	if (_NSIG_WORDS == 1)
		set->sig[0] |= 1UL << sig;
	else
		set->sig[sig / _NSIG_BPW] |= 1UL << (sig % _NSIG_BPW);
}

static inline void sigdelset(sigset_t *set, int _sig)
{
	unsigned long sig = _sig - 1;
	if (_NSIG_WORDS == 1)
		set->sig[0] &= ~(1UL << sig);
	else
		set->sig[sig / _NSIG_BPW] &= ~(1UL << (sig % _NSIG_BPW));
}


#endif  

/* Simplified - _NSIG_WORDS is 2 on x86-32 (~16 LOC) */
static inline int sigisemptyset(sigset_t *set)
{
	return (set->sig[1] | set->sig[0]) == 0;
}

static inline int sigequalsets(const sigset_t *set1, const sigset_t *set2)
{
	return (set1->sig[1] == set2->sig[1]) && (set1->sig[0] == set2->sig[0]);
}

#define sigmask(sig)	(1UL << ((sig) - 1))

#ifndef __HAVE_ARCH_SIG_SETOPS

/* sigandsets, sigandnsets removed - never called */

/* _SIG_SET_OP and _sig_not macros removed - never used to generate functions */

/* Simplified - _NSIG_WORDS is 2 on x86-32 (~10 LOC) */
static inline void sigemptyset(sigset_t *set)
{
	set->sig[1] = 0;
	set->sig[0] = 0;
}



/* sigdelsetmask removed - never called */

/* Simplified - _NSIG_WORDS is 2 on x86-32 (~9 LOC) */
static inline void siginitsetinv(sigset_t *set, unsigned long mask)
{
	set->sig[0] = ~mask;
	set->sig[1] = -1;
}

#endif  

static inline void init_sigpending(struct sigpending *sig)
{
	sigemptyset(&sig->signal);
	INIT_LIST_HEAD(&sig->list);
}

extern void flush_sigqueue(struct sigpending *queue);

static inline int valid_signal(unsigned long sig)
{
	return sig <= _NSIG ? 1 : 0;
}

/* struct timespec, pt_regs forward decls removed - unused */
enum pid_type;

extern int do_send_sig_info(int sig, struct kernel_siginfo *info,
				struct task_struct *p, enum pid_type type);
extern int group_send_sig_info(int sig, struct kernel_siginfo *info,
			       struct task_struct *p, enum pid_type type);
extern int send_signal_locked(int sig, struct kernel_siginfo *info,
			      struct task_struct *p, enum pid_type type);
/* sigprocmask, set_current_blocked removed - never called */
extern void __set_current_blocked(const sigset_t *);
/* show_unhandled_signals removed - never used */

/* get_signal, signal_setup_done removed - never called */
extern void exit_signals(struct task_struct *tsk);

#define SIG_KTHREAD_KERNEL ((__force __sighandler_t)3)


extern struct kmem_cache *sighand_cachep;

/* SIGRTMIN == BITS_PER_LONG == 32 */
#define rt_sigmask(sig)	sigmask(sig)

#define siginmask(sig, mask) \
	((sig) > 0 && (sig) < SIGRTMIN && (rt_sigmask(sig) & (mask)))

#define SIG_KERNEL_ONLY_MASK (\
	rt_sigmask(SIGKILL)   |  rt_sigmask(SIGSTOP))

#define SIG_KERNEL_IGNORE_MASK (\
        rt_sigmask(SIGCONT)   |  rt_sigmask(SIGCHLD)   | \
	rt_sigmask(SIGWINCH)  |  rt_sigmask(SIGURG)    )

#define sig_kernel_only(sig)		siginmask(sig, SIG_KERNEL_ONLY_MASK)
#define sig_kernel_ignore(sig)		siginmask(sig, SIG_KERNEL_IGNORE_MASK)

void signals_init(void);

/* __save_altstack and unsafe_save_altstack removed - never called */

#endif  
