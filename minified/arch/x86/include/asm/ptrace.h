/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_PTRACE_H
#define _ASM_X86_PTRACE_H

#include <asm/segment.h>
#include <asm/page_types.h>
/* Inlined from uapi/asm/ptrace.h (kernel-only parts) */
#include <linux/compiler.h>
/* ptrace-abi.h inlined - was empty (all ptrace constants removed) */
#include <asm/processor-flags.h>

#ifndef __ASSEMBLY__
/* 32-bit only kernel - removed x86_64 pt_regs */
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

/* profile_pc, regs_return_value, send_sigtrap removed - inlined into traps.c */

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

/* user_stack_pointer, regs_irqs_disabled removed - never used */

#define arch_has_single_step()	(1)
#define arch_has_block_step()	(1)
#define ARCH_HAS_USER_SINGLE_STEP_REPORT

/* do_get_thread_area, do_set_thread_area, do_set_thread_area_64 removed - never called */

#endif /* !__ASSEMBLY__ */
#endif /* _ASM_X86_PTRACE_H */
