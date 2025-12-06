 
#ifndef _UAPI_ASM_X86_PTRACE_H
#define _UAPI_ASM_X86_PTRACE_H

#include <linux/compiler.h>	 
#include <asm/ptrace-abi.h>
#include <asm/processor-flags.h>


#ifndef __ASSEMBLY__

/* 32-bit only kernel - removed x86_64 pt_regs */

#ifndef __KERNEL__

struct pt_regs {
	long ebx;
	long ecx;
	long edx;
	long esi;
	long edi;
	long ebp;
	long eax;
	int  xds;
	int  xes;
	int  xfs;
	int  xgs;
	long orig_eax;
	long eip;
	int  xcs;
	long eflags;
	long esp;
	int  xss;
};

#endif  



#endif  

#endif  
