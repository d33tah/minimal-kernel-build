/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PANIC_H
#define _LINUX_PANIC_H

struct pt_regs;

__printf(1, 2) void panic(const char *fmt, ...) __noreturn __cold;

#endif /* _LINUX_PANIC_H */
