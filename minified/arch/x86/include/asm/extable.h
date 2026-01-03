 
#ifndef _ASM_X86_EXTABLE_H
#define _ASM_X86_EXTABLE_H

#include <asm/extable_fixup_types.h>

 

struct exception_table_entry {
	int insn, fixup, data;
};
struct pt_regs;

#define ARCH_HAS_RELATIVE_EXTABLE

#define swap_ex_entry_fixup(a, b, tmp, delta)			\
	do {							\
		(a)->fixup = (b)->fixup + (delta);		\
		(b)->fixup = (tmp).fixup - (delta);		\
		(a)->data = (b)->data;				\
		(b)->data = (tmp).data;				\
	} while (0)

extern int fixup_exception(struct pt_regs *regs, int trapnr,
			   unsigned long error_code, unsigned long fault_addr);
extern int ex_get_fixup_type(unsigned long ip);
extern void early_fixup_exception(struct pt_regs *regs, int trapnr);

/* ex_handler_msr_mce, ex_handler_bpf removed - never used in minimal kernel */

#endif
