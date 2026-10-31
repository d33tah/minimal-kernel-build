#ifndef _LINUX_PANIC_H
#define _LINUX_PANIC_H

struct pt_regs;

__printf(1, 2) void panic(const char *fmt, ...) __noreturn __cold;
/* Removed: oops_may_print - never called */
/* Removed: oops_exit + add_taint - 0-caller after oops_end stubbed (tick #322) */

/* Removed: panic_timeout, panic_on_oops - always 0 (CONFIG), gated only dead branches */
#define PANIC_CPU_INVALID -1

#endif
