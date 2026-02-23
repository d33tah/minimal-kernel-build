#include <linux/mm.h>
#include <asm/traps.h>

bool fixup_vdso_exception(struct pt_regs *regs, int trapnr,
			  unsigned long error_code, unsigned long fault_addr)
{
	return false;
}
