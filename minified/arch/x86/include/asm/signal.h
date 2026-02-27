 
#ifndef _ASM_X86_SIGNAL_H
#define _ASM_X86_SIGNAL_H

#ifndef __ASSEMBLY__
#include <linux/linkage.h>

#define _NSIG		64
/* __i386__ (32-bit) */
#define _NSIG_BPW	32

#define _NSIG_WORDS	(_NSIG / _NSIG_BPW)

typedef struct {
	unsigned long sig[_NSIG_WORDS];
} sigset_t;

#endif  

#ifndef __ASSEMBLY__
#include <linux/types.h>
#include <linux/compiler.h>

#endif

#define SIGBUS		 7
#define SIGKILL		 9
#define SIGSEGV		11

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

typedef struct sigaltstack {
	void __user *ss_sp;
	int ss_flags;
	__kernel_size_t ss_size;
} stack_t;

#endif
#ifndef __ASSEMBLY__

#define __ARCH_HAS_SA_RESTORER

#include <asm/asm.h>

/* __i386__ - 32-bit x86 */

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
