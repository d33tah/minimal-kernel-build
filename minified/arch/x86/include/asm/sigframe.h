 
#ifndef _ASM_X86_SIGFRAME_H
#define _ASM_X86_SIGFRAME_H

#include <uapi/asm/sigcontext.h>
#include <asm-generic/siginfo.h>
/* Inlined from asm/ucontext.h */
struct ucontext {
	unsigned long	  uc_flags;
	struct ucontext  *uc_link;
	stack_t		  uc_stack;
	struct sigcontext uc_mcontext;
	sigset_t	  uc_sigmask;
};

#define sigframe_ia32		sigframe
#define rt_sigframe_ia32	rt_sigframe
#define ucontext_ia32		ucontext

struct sigframe_ia32 {
	u32 pretcode;
	int sig;
	struct sigcontext_32 sc;
	 
	struct _fpstate_32 fpstate_unused;
	unsigned int extramask[1];
	char retcode[8];
	 
};

struct rt_sigframe_ia32 {
	u32 pretcode;
	int sig;
	u32 pinfo;
	u32 puc;
	struct siginfo info;
	struct ucontext_ia32 uc;
	char retcode[8];
	 
};


void __init init_sigframe_size(void);

#endif  
