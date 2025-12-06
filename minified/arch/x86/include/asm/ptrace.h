/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_PTRACE_H
#define _ASM_X86_PTRACE_H

#include <asm/segment.h>
#include <asm/page_types.h>
#include <uapi/asm/ptrace.h>

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

struct cpuinfo_x86;
struct task_struct;

extern unsigned long profile_pc(struct pt_regs *regs);
extern unsigned long convert_ip_to_linear(struct task_struct *child, struct pt_regs *regs);
extern void send_sigtrap(struct pt_regs *regs, int error_code, int si_code);

static inline unsigned long regs_return_value(struct pt_regs *regs)
{
	return regs->ax;
}

static inline void regs_set_return_value(struct pt_regs *regs, unsigned long rc)
{
	regs->ax = rc;
}

static __always_inline int user_mode(struct pt_regs *regs)
{
	return ((regs->cs & SEGMENT_RPL_MASK) | (regs->flags & X86_VM_MASK)) >= USER_RPL;
}

static __always_inline int v8086_mode(struct pt_regs *regs)
{
	return (regs->flags & X86_VM_MASK);
}

static inline bool user_64bit_mode(struct pt_regs *regs)
{
	return false;
}

static inline bool any_64bit_mode(struct pt_regs *regs)
{
	return false;
}

static inline unsigned long kernel_stack_pointer(struct pt_regs *regs)
{
	return regs->sp;
}

static inline unsigned long instruction_pointer(struct pt_regs *regs)
{
	return regs->ip;
}

static inline void instruction_pointer_set(struct pt_regs *regs, unsigned long val)
{
	regs->ip = val;
}

static inline unsigned long frame_pointer(struct pt_regs *regs)
{
	return regs->bp;
}

static inline unsigned long user_stack_pointer(struct pt_regs *regs)
{
	return regs->sp;
}

static inline void user_stack_pointer_set(struct pt_regs *regs, unsigned long val)
{
	regs->sp = val;
}

static __always_inline bool regs_irqs_disabled(struct pt_regs *regs)
{
	return !(regs->flags & X86_EFLAGS_IF);
}

/* Register query functions - needed for kernel, not used but part of API */
extern int regs_query_register_offset(const char *name);
extern const char *regs_query_register_name(unsigned int offset);
#define MAX_REG_OFFSET (offsetof(struct pt_regs, ss))

#define arch_has_single_step()	(1)
#define arch_has_block_step()	(1)
#define ARCH_HAS_USER_SINGLE_STEP_REPORT

struct user_desc;
extern int do_get_thread_area(struct task_struct *p, int idx,
			      struct user_desc __user *info);
extern int do_set_thread_area(struct task_struct *p, int idx,
			      struct user_desc __user *info, int can_allocate);

#define do_set_thread_area_64(p, s, t)	(0)

#endif /* !__ASSEMBLY__ */
#endif /* _ASM_X86_PTRACE_H */
