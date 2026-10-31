#ifndef _LINUX_PANIC_H
#define _LINUX_PANIC_H

struct pt_regs;

__printf(1, 2) void panic(const char *fmt, ...) __noreturn __cold;
/* Removed: oops_may_print - never called */

/* Removed: panic_timeout, panic_on_oops - always 0 (CONFIG), gated only dead branches */

#endif
