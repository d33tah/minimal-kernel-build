#ifndef _ASM_X86_INSN_EVAL_H
#define _ASM_X86_INSN_EVAL_H

#include <asm/ptrace.h>

int pt_regs_offset(struct pt_regs *regs, int regno);
/* Other insn-eval functions removed - unused */

#endif  
