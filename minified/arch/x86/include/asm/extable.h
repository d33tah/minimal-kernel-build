
#ifndef _ASM_X86_EXTABLE_H
#define _ASM_X86_EXTABLE_H

struct exception_table_entry {
	int insn, fixup, data;
};
struct pt_regs;

extern int fixup_exception(struct pt_regs *regs, int trapnr,
			   unsigned long error_code, unsigned long fault_addr);
extern void early_fixup_exception(struct pt_regs *regs, int trapnr);

#endif
