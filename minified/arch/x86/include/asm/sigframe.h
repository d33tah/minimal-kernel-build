 
#ifndef _ASM_X86_SIGFRAME_H
#define _ASM_X86_SIGFRAME_H

#include <uapi/asm/sigcontext.h>
#include <asm/siginfo.h>
#include <asm/ucontext.h>
#include <linux/compat.h>

#define rt_sigframe_ia32	rt_sigframe
#define ucontext_ia32		ucontext

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
