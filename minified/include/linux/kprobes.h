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

/* Removed uncalled: kprobe_fault_handler, kprobe_running */
static inline void kprobe_flush_task(struct task_struct *tk) { }
static inline void kprobe_free_init_mem(void) { }
/* Removed uncalled: is_kprobe_insn_slot, is_kprobe_optinsn_slot, kprobe_page_fault */

#endif
