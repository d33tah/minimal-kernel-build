#ifndef _LINUX_PANIC_H
#define _LINUX_PANIC_H

#include <linux/compiler_attributes.h>
#include <linux/types.h>

struct pt_regs;

extern long (*panic_blink)(int state);
__printf(1, 2)
void panic(const char *fmt, ...) __noreturn __cold;
/* Removed: oops_may_print - never called */
/* Removed: oops_exit + add_taint - 0-caller after oops_end stubbed (tick #322) */

/* Removed: panic_timeout, panic_on_oops - always 0 (CONFIG), gated only dead branches */
/* Removed: panic_on_taint_nousertaint, sysctl_panic_on_rcu_stall,
   sysctl_max_rcu_stall_to_panic, sysctl_panic_on_stackoverflow - never defined/used */

extern atomic_t panic_cpu;
#define PANIC_CPU_INVALID	-1

#endif
