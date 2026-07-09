/* Minimal kprobes.h - kprobes disabled */
#ifndef _LINUX_KPROBES_H
#define _LINUX_KPROBES_H

#include <linux/compiler.h>
#include <linux/ptrace.h>

/* Stubs for kprobes-disabled kernel */
#define nokprobe_inline	inline

static nokprobe_inline bool kprobe_page_fault(struct pt_regs *regs,
					      unsigned int trap)
{
	return false;
}

#endif
