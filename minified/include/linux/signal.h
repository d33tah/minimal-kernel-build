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

#define sigmask(sig)	(1UL << ((sig) - 1))

#ifndef __HAVE_ARCH_SIG_SETOPS

static inline void sigemptyset(sigset_t *set)
{
	switch (_NSIG_WORDS) {
	default:
		memset(set, 0, sizeof(sigset_t));
		break;
	case 2: set->sig[1] = 0;
		fallthrough;
	case 1:	set->sig[0] = 0;
		break;
	}
}




#endif

static inline void init_sigpending(struct sigpending *sig)
{
	sigemptyset(&sig->signal);
	INIT_LIST_HEAD(&sig->list);
}

extern void flush_sigqueue(struct sigpending *queue);


struct pt_regs;
enum pid_type;

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


#endif  
