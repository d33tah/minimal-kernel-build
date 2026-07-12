/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_PTRACE_H
#define _ASM_X86_PTRACE_H

#include <asm/segment.h>
#include <asm/page_types.h>
#include <uapi/asm/ptrace.h>

#ifndef __ASSEMBLY__
/* 32-bit only kernel - removed x86_64 pt_regs */
struct pt_regs { unsigned long bx, cx, dx, si, di, bp, ax; unsigned short ds, __dsh, es, __esh, fs, __fsh, gs, __gsh; unsigned long orig_ax, ip; unsigned short cs, __csh; unsigned long flags, sp; unsigned short ss, __ssh; };

#include <asm/proto.h>


extern void send_sigtrap(struct pt_regs *regs, int error_code, int si_code);

static __always_inline int user_mode(struct pt_regs *regs) {
	return ((regs->cs & SEGMENT_RPL_MASK) | (regs->flags & X86_VM_MASK)) >= USER_RPL;
}

static inline unsigned long instruction_pointer(struct pt_regs *regs) {
	return regs->ip;
}


#endif /* !__ASSEMBLY__ */
#endif /* _ASM_X86_PTRACE_H */
