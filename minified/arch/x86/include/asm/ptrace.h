/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_PTRACE_H
#define _ASM_X86_PTRACE_H

#include <asm/segment.h>
#include <asm/page_types.h>
#include <linux/compiler.h>
#include <asm/processor-flags.h>

#ifndef __ASSEMBLY__
struct pt_regs {
	unsigned long bx;
	unsigned long cx;
	unsigned long dx;
	unsigned long si;
	unsigned long di;
	unsigned long bp;
	unsigned long ax;
	unsigned short ds;
	unsigned short __dsh;
	unsigned short es;
	unsigned short __esh;
	unsigned short fs;
	unsigned short __fsh;
	unsigned short gs;
	unsigned short __gsh;
	unsigned long orig_ax;
	unsigned long ip;
	unsigned short cs;
	unsigned short __csh;
	unsigned long flags;
	unsigned long sp;
	unsigned short ss;
	unsigned short __ssh;
};

/* inlined from asm/proto.h */
#ifndef LDT_ENTRY_SIZE
#define LDT_ENTRY_SIZE 8
#endif
void entry_INT80_32(void);

static __always_inline int user_mode(struct pt_regs *regs)
{
	return ((regs->cs & SEGMENT_RPL_MASK) | (regs->flags & X86_VM_MASK)) >= USER_RPL;
}

static inline unsigned long instruction_pointer(struct pt_regs *regs)
{
	return regs->ip;
}

#endif /* !__ASSEMBLY__ */
#endif /* _ASM_X86_PTRACE_H */
