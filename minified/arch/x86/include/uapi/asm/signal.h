
#ifndef _UAPI_ASM_X86_SIGNAL_H
#define _UAPI_ASM_X86_SIGNAL_H

/* userspace-only NSIG + sigset_t (#ifndef __KERNEL__) removed - kernel uses _NSIG + own sigset_t */

#define SIGILL 4
#define SIGTRAP 5
#define SIGBUS 7
#define SIGFPE 8
#define SIGSEGV 11
#define SIGCHLD 17
/* SIGCONT(18)/SIGSTOP(19)/SIGURG(23)/SIGWINCH(28)/SIGRTMIN(32) removed - 0-ref in this build */
/* SIGUNUSED, SIGRTMAX, SIGSTKSZ removed - unused.
   SIGHUP/INT/QUIT/ABRT/PIPE/ALRM/TERM/TSTP/TTIN/TTOU/IO/SYS removed - 0-ref in this build */

/* SA_RESTORER + MINSIGSTKSZ removed - unused */

/* Inlined from asm-generic/signal-defs.h */
/* SA_NOCLDSTOP/NOCLDWAIT/SIGINFO/UNSUPPORTED/EXPOSE_TAGBITS/ONSTACK/RESTART/
 * NODEFER/RESETHAND removed - unused (only consumer UAPI_SA_FLAGS was dead) */

#define SIG_BLOCK 0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

/* end signal-defs.h inlining */

/* userspace-only struct sigaction + sa_handler/sa_sigaction (#ifndef __KERNEL__) removed - kernel uses struct sigaction in signal_types.h */

#endif
