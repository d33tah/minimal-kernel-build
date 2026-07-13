 
#ifndef _ASM_X86_SIGNAL_H
#define _ASM_X86_SIGNAL_H

#ifndef __ASSEMBLY__
#include <linux/linkage.h>

 

#define _NSIG		64
/* __i386__ (32-bit) */
#define _NSIG_BPW	32

#define _NSIG_WORDS	(_NSIG / _NSIG_BPW)

typedef struct { unsigned long sig[_NSIG_WORDS]; } sigset_t;

 
typedef sigset_t compat_sigset_t;

#endif  
#include <uapi/asm/signal.h>
#ifndef __ASSEMBLY__

#define __ARCH_HAS_SA_RESTORER

#include <asm/asm.h>
#include <uapi/asm/sigcontext.h>

/* __i386__ - 32-bit x86 */


static inline int __const_sigismember(sigset_t *set, int _sig) {
	unsigned long sig = _sig - 1;
	return 1 & (set->sig[sig / _NSIG_BPW] >> (sig % _NSIG_BPW)); }

static inline int __gen_sigismember(sigset_t *set, int _sig) {
	bool ret;
	asm("btl %2,%1" CC_SET(c) : CC_OUT(c) (ret) : "m"(*set), "Ir"(_sig-1));
	return ret; }

#define sigismember(set, sig)				(__builtin_constant_p(sig)			 ? __const_sigismember((set), (sig))		 : __gen_sigismember((set), (sig)))

struct pt_regs;

#endif  
#endif  
