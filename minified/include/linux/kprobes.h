/* Minimal kprobes.h - kprobes disabled */
#ifndef _LINUX_KPROBES_H
#define _LINUX_KPROBES_H

#include <linux/compiler.h>
#include <linux/ptrace.h>

/* Stubs for kprobes-disabled kernel */
#define NOKPROBE_SYMBOL(fname)
/* __kprobes, nokprobe_inline removed - never used */

/* All kprobe stubs removed - call sites removed */

#endif
