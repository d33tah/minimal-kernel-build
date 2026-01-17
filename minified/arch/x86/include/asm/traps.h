 
#ifndef _ASM_X86_TRAPS_H
#define _ASM_X86_TRAPS_H

#include <linux/kprobes.h>

#include <asm/debugreg.h>
#include <asm/idtentry.h>
#include <asm/siginfo.h>

/* Inlined from asm/trap_pf.h */
enum x86_pf_error_code {
	X86_PF_PROT	=		1 << 0,
	X86_PF_WRITE	=		1 << 1,
	X86_PF_USER	=		1 << 2,
	X86_PF_RSVD	=		1 << 3,
	X86_PF_INSTR	=		1 << 4,
	X86_PF_PK	=		1 << 5,
	X86_PF_SGX	=		1 << 15,
};

/* ibt_selftest removed - declared but never defined or called */

static inline int get_si_code(unsigned long condition)
{
	if (condition & DR_STEP)
		return TRAP_TRACE;
	else if (condition & (DR_TRAP0|DR_TRAP1|DR_TRAP2|DR_TRAP3))
		return TRAP_HWBKPT;
	else
		return TRAP_BRKPT;
}

/* panic_on_unrecovered_nmi removed - never set to non-zero */

bool fault_in_kernel_space(unsigned long address);


#endif  
