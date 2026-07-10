 
#ifndef _UAPI_ASM_X86_SIGNAL_H
#define _UAPI_ASM_X86_SIGNAL_H

#ifndef __ASSEMBLY__
#include <linux/types.h>
#include <linux/time.h>
#include <linux/compiler.h>

 

/* userspace-only NSIG + sigset_t (#ifndef __KERNEL__) removed - kernel uses _NSIG + own sigset_t */
#endif  


#define SIGILL		 4
#define SIGTRAP		 5
#define SIGBUS		 7
#define SIGFPE		 8
#define SIGKILL		 9
#define SIGSEGV		11
#define SIGCHLD		17
/* SIGCONT(18)/SIGSTOP(19)/SIGURG(23)/SIGWINCH(28)/SIGRTMIN(32) removed - 0-ref in this build */
/* SIGUNUSED, SIGRTMAX, SIGSTKSZ removed - unused.
   SIGHUP/INT/QUIT/ABRT/PIPE/ALRM/TERM/TSTP/TTIN/TTOU/IO/SYS removed - 0-ref in this build */

/* SA_RESTORER + MINSIGSTKSZ removed - unused */

/* Inlined from asm-generic/signal-defs.h */
/* SA_NOCLDSTOP/NOCLDWAIT/SIGINFO/UNSUPPORTED/EXPOSE_TAGBITS/ONSTACK/RESTART/
 * NODEFER/RESETHAND removed - unused (only consumer UAPI_SA_FLAGS was dead) */

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
#endif
/* end signal-defs.h inlining */

#ifndef __ASSEMBLY__


/* userspace-only struct sigaction + sa_handler/sa_sigaction (#ifndef __KERNEL__) removed - kernel uses struct sigaction in signal_types.h */

typedef struct sigaltstack {
	void __user *ss_sp;
	int ss_flags;
	__kernel_size_t ss_size;
} stack_t;

#endif  

#endif  
