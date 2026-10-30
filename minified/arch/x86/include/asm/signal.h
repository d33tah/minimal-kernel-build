
#ifndef _ASM_X86_SIGNAL_H
#define _ASM_X86_SIGNAL_H

#ifndef __ASSEMBLY__
#include <linux/linkage.h>

#define _NSIG 64
/* __i386__ (32-bit) */
#define _NSIG_BPW 32

#define _NSIG_WORDS (_NSIG / _NSIG_BPW)

typedef struct {
	unsigned long sig[_NSIG_WORDS];
} sigset_t;

typedef sigset_t compat_sigset_t;

#endif
#include <uapi/asm/signal.h>
#ifndef __ASSEMBLY__

#define __ARCH_HAS_SA_RESTORER

#include <asm/asm.h>
#include <uapi/asm/sigcontext.h>

/* __i386__ - 32-bit x86 */

struct pt_regs;

#endif
#endif
