/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_TRAPS_H
#define _ASM_X86_TRAPS_H

#include <linux/context_tracking_state.h>
#include <linux/kprobes.h>

#include <asm/debugreg.h>
#include <asm/idtentry.h>
#include <asm/siginfo.h>			/* TRAP_TRACE, ... */
#include <asm/trap_pf.h>


extern bool ibt_selftest(void);


static inline int get_si_code(unsigned long condition)
{
	if (condition & DR_STEP)
		return TRAP_TRACE;
	else if (condition & (DR_TRAP0|DR_TRAP1|DR_TRAP2|DR_TRAP3))
		return TRAP_HWBKPT;
	else
		return TRAP_BRKPT;
}

extern int panic_on_unrecovered_nmi;

void math_emulate(struct math_emu_info *);

bool fault_in_kernel_space(unsigned long address);


#endif /* _ASM_X86_TRAPS_H */
