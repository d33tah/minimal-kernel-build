// SPDX-License-Identifier: GPL-2.0
/* System call table for i386. */

#include <asm/syscall.h>

#define __SYSCALL_WITH_COMPAT(nr, native, compat)	__SYSCALL(nr, native)

#define __SYSCALL(nr, sym) extern long __ia32_##sym(const struct pt_regs *);

#include <asm/syscalls_32.h>

#include "linux/compiler_attributes.h"

struct pt_regs;

#undef __SYSCALL

#define __SYSCALL(nr, sym) __ia32_##sym,

__visible const sys_call_ptr_t ia32_sys_call_table[] = {
};
