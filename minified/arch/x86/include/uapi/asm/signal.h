 
#ifndef _UAPI_ASM_X86_SIGNAL_H
#define _UAPI_ASM_X86_SIGNAL_H

#ifndef __ASSEMBLY__
#include <linux/types.h>
#include <linux/time.h>
#include <linux/compiler.h>

 
struct siginfo;

#ifndef __KERNEL__
 

#define NSIG		32
typedef unsigned long sigset_t;

#endif  
#endif  


#define SIGHUP		 1
#define SIGINT		 2
#define SIGQUIT		 3
#define SIGILL		 4
#define SIGTRAP		 5
#define SIGABRT		 6
#define SIGIOT		 6
#define SIGBUS		 7
#define SIGFPE		 8
#define SIGKILL		 9
#define SIGUSR1		10
#define SIGSEGV		11
#define SIGUSR2		12
#define SIGPIPE		13
#define SIGALRM		14
#define SIGTERM		15
#define SIGSTKFLT	16
#define SIGCHLD		17
#define SIGCONT		18
#define SIGSTOP		19
#define SIGTSTP		20
#define SIGTTIN		21
#define SIGTTOU		22
#define SIGURG		23
#define SIGXCPU		24
#define SIGXFSZ		25
#define SIGVTALRM	26
#define SIGPROF		27
#define SIGWINCH	28
#define SIGIO		29
#define SIGPOLL		SIGIO
 
#define SIGPWR		30
#define SIGSYS		31
#define	SIGUNUSED	31

 
#define SIGRTMIN	32
#define SIGRTMAX	_NSIG

#define SA_RESTORER	0x04000000

#define MINSIGSTKSZ	2048
#define SIGSTKSZ	8192

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
#define SA_UNSUPPORTED	0x00000400
#define SA_EXPOSE_TAGBITS	0x00000800
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

#define SA_NOMASK	SA_NODEFER
#define SA_ONESHOT	SA_RESETHAND

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
#define SIG_ERR	((__force __sighandler_t)-1)
#endif
/* end signal-defs.h inlining */

#ifndef __ASSEMBLY__


# ifndef __KERNEL__
/* 32-bit only kernel - removed x86_64 sigaction */
struct sigaction {
	union {
	  __sighandler_t _sa_handler;
	  void (*_sa_sigaction)(int, struct siginfo *, void *);
	} _u;
	sigset_t sa_mask;
	unsigned long sa_flags;
	void (*sa_restorer)(void);
};

#define sa_handler	_u._sa_handler
#define sa_sigaction	_u._sa_sigaction

# endif  

typedef struct sigaltstack {
	void __user *ss_sp;
	int ss_flags;
	__kernel_size_t ss_size;
} stack_t;

#endif  

#endif  
