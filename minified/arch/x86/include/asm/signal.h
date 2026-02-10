 
#ifndef _ASM_X86_SIGNAL_H
#define _ASM_X86_SIGNAL_H

#ifndef __ASSEMBLY__
#include <linux/linkage.h>

 

#define _NSIG		64
/* __i386__ (32-bit) */
#define _NSIG_BPW	32

#define _NSIG_WORDS	(_NSIG / _NSIG_BPW)

typedef unsigned long old_sigset_t;		 

typedef struct {
	unsigned long sig[_NSIG_WORDS];
} sigset_t;

/* SA_IA32_ABI, SA_X32_ABI removed - never used */

/* compat_sigset_t removed - never used */

#endif  
/* Inlined from uapi/asm/signal.h */

#ifndef __ASSEMBLY__
#include <linux/types.h>
#include <linux/time.h>
#include <linux/compiler.h>

 
struct siginfo;

/* NSIG, sigset_t removed - userspace only (#ifndef __KERNEL__) */
#endif


/* Unused signals removed: SIGHUP, SIGINT, SIGQUIT, SIGABRT, SIGIOT,
   SIGUSR1, SIGUSR2, SIGPIPE, SIGALRM, SIGTERM, SIGSTKFLT, SIGTSTP,
   SIGTTIN, SIGTTOU, SIGXCPU, SIGXFSZ, SIGVTALRM, SIGPROF, SIGIO,
   SIGPOLL, SIGPWR */
#define SIGILL		 4
#define SIGTRAP		 5
#define SIGBUS		 7
#define SIGFPE		 8
#define SIGKILL		 9
#define SIGSEGV		11
#define SIGCHLD		17
#define SIGCONT		18
#define SIGSTOP		19
#define SIGURG		23
#define SIGWINCH	28
/* SIGUNUSED, SIGRTMAX, SIGSTKSZ removed - unused */
#define SIGRTMIN	32

#define SA_RESTORER	0x04000000

/* MINSIGSTKSZ removed - never used */




#ifndef __ASSEMBLY__
typedef void __signalfn_t(int);
typedef __signalfn_t __user *__sighandler_t;

typedef void __restorefn_t(void);
typedef __restorefn_t __user *__sigrestore_t;

#define SIG_DFL	((__force __sighandler_t)0)
#define SIG_IGN	((__force __sighandler_t)1)
/* SIG_ERR removed - never used */
#endif
/* end signal-defs.h inlining */

#ifndef __ASSEMBLY__


/* Userspace-only struct sigaction removed - __KERNEL__ always defined */

typedef struct sigaltstack {
	void __user *ss_sp;
	int ss_flags;
	__kernel_size_t ss_size;
} stack_t;

#endif
#ifndef __ASSEMBLY__

#define __ARCH_HAS_SA_RESTORER

#include <asm/asm.h>
#include <uapi/asm/sigcontext.h>

/* __i386__ - 32-bit x86 */
#define __HAVE_ARCH_SIG_BITOPS

#define sigaddset(set,sig)		    \
	(__builtin_constant_p(sig)	    \
	 ? __const_sigaddset((set), (sig))  \
	 : __gen_sigaddset((set), (sig)))

static inline void __gen_sigaddset(sigset_t *set, int _sig)
{
	asm("btsl %1,%0" : "+m"(*set) : "Ir"(_sig - 1) : "cc");
}

static inline void __const_sigaddset(sigset_t *set, int _sig)
{
	unsigned long sig = _sig - 1;
	set->sig[sig / _NSIG_BPW] |= 1 << (sig % _NSIG_BPW);
}

#define sigdelset(set, sig)		    \
	(__builtin_constant_p(sig)	    \
	 ? __const_sigdelset((set), (sig))  \
	 : __gen_sigdelset((set), (sig)))

static inline void __gen_sigdelset(sigset_t *set, int _sig)
{
	asm("btrl %1,%0" : "+m"(*set) : "Ir"(_sig - 1) : "cc");
}

static inline void __const_sigdelset(sigset_t *set, int _sig)
{
	unsigned long sig = _sig - 1;
	set->sig[sig / _NSIG_BPW] &= ~(1 << (sig % _NSIG_BPW));
}

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

#endif  
#endif  
