#ifndef _ASM_X86_UMIP_H
#define _ASM_X86_UMIP_H

#include <linux/types.h>
#include <asm/ptrace.h>

static inline bool fixup_umip_exception(struct pt_regs *regs) { return false; }
#endif  /* _ASM_X86_UMIP_H */
