#include <linux/mm.h>
#include <asm/traps.h>

bool fixup_vdso_exception(struct pt_regs *regs, int trapnr, unsigned long error_code, unsigned long fault_addr)
{

	if (trapnr == X86_TRAP_DB || trapnr == X86_TRAP_BP)
		return false;

	/*
	 * mm->context.vdso is never set in this minimal build, so the vdso
	 * exception fixup table is never installed and there is nothing to fix.
	 */
	return false;
}
