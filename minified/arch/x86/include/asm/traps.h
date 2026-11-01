/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_TRAPS_H
#define _ASM_X86_TRAPS_H

#include <linux/ptrace.h>
#include <asm/debugreg.h>
#include <asm/idtentry.h>

enum x86_pf_error_code {
	X86_PF_PROT = 1 << 0,
	X86_PF_WRITE = 1 << 1,
	X86_PF_USER = 1 << 2,
	X86_PF_INSTR = 1 << 4,
	X86_PF_PK = 1 << 5,
};

#endif
