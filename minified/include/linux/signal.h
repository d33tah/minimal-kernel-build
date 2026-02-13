#ifndef _LINUX_SIGNAL_H
#define _LINUX_SIGNAL_H

#include <linux/bug.h>
#include <linux/list.h>
#include <asm/signal.h>
#include <asm-generic/siginfo.h>
#include <linux/string.h>

#ifndef _LINUX_SIGNAL_TYPES_INLINED
#define _LINUX_SIGNAL_TYPES_INLINED

typedef struct kernel_siginfo {
	__SIGINFO;
} kernel_siginfo_t;

struct sigpending {
	struct list_head list;
	sigset_t signal;
};

struct sigaction {
	__sighandler_t	sa_handler;
	unsigned long	sa_flags;
#ifdef __ARCH_HAS_SA_RESTORER
	__sigrestore_t sa_restorer;
#endif
	sigset_t	sa_mask;
};

struct k_sigaction {
	struct sigaction sa;
};

#define SA_IMMUTABLE		0x00800000
#endif /* _LINUX_SIGNAL_TYPES_INLINED */

struct task_struct;

static inline void clear_siginfo(kernel_siginfo_t *info)
{
	memset(info, 0, sizeof(*info));
}

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

static inline int sigequalsets(const sigset_t *set1, const sigset_t *set2)
{
	return (set1->sig[1] == set2->sig[1]) && (set1->sig[0] == set2->sig[0]);
}

#define sigmask(sig)	(1UL << ((sig) - 1))

#ifndef __HAVE_ARCH_SIG_SETOPS

/* Simplified - _NSIG_WORDS is 2 on x86-32 (~10 LOC) */
static inline void sigemptyset(sigset_t *set)
{
	set->sig[1] = 0;
	set->sig[0] = 0;
}

/* Simplified - _NSIG_WORDS is 2 on x86-32 (~9 LOC) */
#endif  

static inline void init_sigpending(struct sigpending *sig)
{
	sigemptyset(&sig->signal);
	INIT_LIST_HEAD(&sig->list);
}

enum pid_type;

extern void __set_current_blocked(const sigset_t *);

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
