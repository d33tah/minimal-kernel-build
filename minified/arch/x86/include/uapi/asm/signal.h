 
#ifndef _UAPI_ASM_X86_SIGNAL_H
#define _UAPI_ASM_X86_SIGNAL_H

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
#define SIGSYS		31
/* SIGUNUSED, SIGRTMAX, SIGSTKSZ removed - unused */
#define SIGRTMIN	32

#define SA_RESTORER	0x04000000

/* MINSIGSTKSZ removed - never used */

/* Inlined from asm-generic/signal-defs.h */
#ifndef SA_NOCLDSTOP
#define SA_NOCLDSTOP	0x00000001
#endif
#ifndef SA_NOCLDWAIT
#define SA_NOCLDWAIT	0x00000002
#endif
#ifndef SA_SIGINFO
#define SA_SIGINFO	0x00000004
#endif
/* SA_UNSUPPORTED, SA_EXPOSE_TAGBITS removed - unused */
#ifndef SA_ONSTACK
#define SA_ONSTACK	0x08000000
#endif
#ifndef SA_RESTART
#define SA_RESTART	0x10000000
#endif
#ifndef SA_NODEFER
#define SA_NODEFER	0x40000000
#endif
#ifndef SA_RESETHAND
#define SA_RESETHAND	0x80000000
#endif

/* SA_NOMASK, SA_ONESHOT removed - unused */

#ifndef SIG_BLOCK
#define SIG_BLOCK          0
#endif
#ifndef SIG_UNBLOCK
#define SIG_UNBLOCK        1
#endif
#ifndef SIG_SETMASK
#define SIG_SETMASK        2
#endif

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

#endif  
