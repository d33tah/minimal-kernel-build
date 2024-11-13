/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_SIGFRAME_H
#define _ASM_X86_SIGFRAME_H

#include <uapi/asm/sigcontext.h>
#include <asm/siginfo.h>
#include <asm/ucontext.h>
#include <linux/compat.h>

#define sigframe_ia32		sigframe
#define rt_sigframe_ia32	rt_sigframe
#define ucontext_ia32		ucontext

struct sigframe_ia32 {
	u32 pretcode;
	int sig;
	struct sigcontext_32 sc;
	/*
	 * fpstate is unused. fpstate is moved/allocated after
	 * retcode[] below. This movement allows to have the FP state and the
	 * future state extensions (xsave) stay together.
	 * And at the same time retaining the unused fpstate, prevents changing
	 * the offset of extramask[] in the sigframe and thus prevent any
	 * legacy application accessing/modifying it.
	 */
	struct _fpstate_32 fpstate_unused;
	unsigned int extramask[1];
	char retcode[8];
	/* fp state follows here */
};

struct rt_sigframe_ia32 {
	u32 pretcode;
	int sig;
	u32 pinfo;
	u32 puc;
	struct siginfo info;
	struct ucontext_ia32 uc;
	char retcode[8];
	/* fp state follows here */
};


void __init init_sigframe_size(void);

#endif /* _ASM_X86_SIGFRAME_H */
