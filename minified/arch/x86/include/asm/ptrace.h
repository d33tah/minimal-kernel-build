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

#include <asm/proto.h>

static __always_inline int user_mode(struct pt_regs *regs)
{
	return ((regs->cs & SEGMENT_RPL_MASK) | (regs->flags & X86_VM_MASK)) >= USER_RPL;
}

static __always_inline int v8086_mode(struct pt_regs *regs)
{
	return (regs->flags & X86_VM_MASK);
}

static inline unsigned long instruction_pointer(struct pt_regs *regs)
{
	return regs->ip;
}

#define arch_has_single_step()	(1)
#define arch_has_block_step()	(1)
#define ARCH_HAS_USER_SINGLE_STEP_REPORT

#endif /* !__ASSEMBLY__ */
#endif /* _ASM_X86_PTRACE_H */
