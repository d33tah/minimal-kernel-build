#ifndef _LINUX_SIGNAL_H
#define _LINUX_SIGNAL_H

#include <linux/bug.h>
#include <linux/list.h>
#include <linux/linkage.h>
#include <linux/types.h>
#include <linux/compiler.h>
#include <asm/asm.h>
#include <asm-generic/siginfo.h>
#include <linux/string.h>

/* Inlined from asm/signal.h */
#define _NSIG		64
#define _NSIG_BPW	32
#define _NSIG_WORDS	(_NSIG / _NSIG_BPW)

typedef struct {
	unsigned long sig[_NSIG_WORDS];
} sigset_t;

#define SIGKILL		 9
#define SIGSEGV		11

typedef void __signalfn_t(int);
typedef __signalfn_t __user *__sighandler_t;

typedef void __restorefn_t(void);
typedef __restorefn_t __user *__sigrestore_t;

#define SIG_DFL	((__force __sighandler_t)0)
#define SIG_IGN	((__force __sighandler_t)1)

typedef struct sigaltstack {
	void __user *ss_sp;
	int ss_flags;
	__kernel_size_t ss_size;
} stack_t;

#define __ARCH_HAS_SA_RESTORER

static inline int __const_sigismember(sigset_t *set, int _sig)
{
	unsigned long sig = _sig - 1;
	return 1 & (set->sig[sig / _NSIG_BPW] >> (sig % _NSIG_BPW));
}

static inline int __gen_sigismember(sigset_t *set, int _sig)
{
	bool ret;
	asm("btl %2,%1" CC_SET(c)
	    : CC_OUT(c) (ret) : "m"(*set), "Ir"(_sig-1));
	return ret;
}

#define sigismember(set, sig)			\
	(__builtin_constant_p(sig)		\
	 ? __const_sigismember((set), (sig))	\
	 : __gen_sigismember((set), (sig)))
/* End inlined asm/signal.h */

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


#endif /* _LINUX_SIGNAL_TYPES_INLINED */

struct task_struct;

#ifndef __HAVE_ARCH_SIG_SETOPS

static inline void sigemptyset(sigset_t *set)
{
	set->sig[1] = 0;
	set->sig[0] = 0;
}

#endif

static inline void init_sigpending(struct sigpending *sig)
{
	sigemptyset(&sig->signal);
	INIT_LIST_HEAD(&sig->list);
}

enum pid_type;

#endif
