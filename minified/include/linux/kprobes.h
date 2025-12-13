/* Minimal kprobes.h - kprobes disabled */
#ifndef _LINUX_KPROBES_H
#define _LINUX_KPROBES_H

#include <linux/compiler.h>
#include <linux/ptrace.h>

/* Stubs for kprobes-disabled kernel */
#define NOKPROBE_SYMBOL(fname)
#define __kprobes
#define nokprobe_inline	inline

struct kprobe;
struct task_struct;

static inline int kprobe_fault_handler(struct pt_regs *regs, int trapnr) { return 0; }
static inline struct kprobe *kprobe_running(void) { return NULL; }

static inline void kprobe_flush_task(struct task_struct *tk) { }
static inline void kprobe_free_init_mem(void) { }

static inline bool is_kprobe_insn_slot(unsigned long addr) { return false; }
static inline bool is_kprobe_optinsn_slot(unsigned long addr) { return false; }

static nokprobe_inline bool kprobe_page_fault(struct pt_regs *regs,
					      unsigned int trap)
{
	return false;
}

#endif
