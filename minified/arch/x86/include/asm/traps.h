 
#ifndef _ASM_X86_TRAPS_H
#define _ASM_X86_TRAPS_H

#ifndef NOKPROBE_SYMBOL
#define NOKPROBE_SYMBOL(fname) /* kprobes disabled */
#endif

#include <asm/debugreg.h>
#include <asm/idtentry.h>
#include <asm-generic/siginfo.h>

enum x86_pf_error_code {
	X86_PF_PROT	=		1 << 0,
	X86_PF_WRITE	=		1 << 1,
	X86_PF_USER	=		1 << 2,
	X86_PF_RSVD	=		1 << 3,
	X86_PF_INSTR	=		1 << 4,
	X86_PF_PK	=		1 << 5,
	X86_PF_SGX	=		1 << 15,
};

/* get_si_code inlined at arch/x86/kernel/traps.c - single caller */

bool fault_in_kernel_space(unsigned long address);

#endif  
