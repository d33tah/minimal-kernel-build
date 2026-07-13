 
#ifndef _ASM_X86_TRAPS_H
#define _ASM_X86_TRAPS_H

#include <linux/kprobes.h>

#include <asm/debugreg.h>
#include <asm/idtentry.h>
#include <asm/siginfo.h>

/* Inlined from asm/trap_pf.h */
enum x86_pf_error_code { X86_PF_PROT	=		1 << 0, X86_PF_WRITE	=		1 << 1, X86_PF_USER	=		1 << 2, X86_PF_INSTR	=		1 << 4, X86_PF_PK	=		1 << 5, };


bool fault_in_kernel_space(unsigned long address);


#endif  
