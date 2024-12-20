/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_BUG_H
#define _ASM_X86_BUG_H

#include <linux/stringify.h>
#include <linux/instrumentation.h>
#include <linux/objtool.h>

/*
 * Despite that some emulators terminate on UD2, we use it for WARN().
 */
#define ASM_UD2		".byte 0x0f, 0x0b"
#define INSN_UD2	0x0b0f
#define LEN_UD2		2


#define _BUG_FLAGS(ins, flags, extra)  asm volatile(ins)


#define HAVE_ARCH_BUG
#define BUG()							\
do {								\
	instrumentation_begin();				\
	_BUG_FLAGS(ASM_UD2, 0, "");				\
	__builtin_unreachable();				\
} while (0)

/*
 * This instrumentation_begin() is strictly speaking incorrect; but it
 * suppresses the complaints from WARN()s in noinstr code. If such a WARN()
 * were to trigger, we'd rather wreck the machine in an attempt to get the
 * message out than not know about it.
 */
#define __WARN_FLAGS(flags)					\
do {								\
	__auto_type __flags = BUGFLAG_WARNING|(flags);		\
	instrumentation_begin();				\
	_BUG_FLAGS(ASM_UD2, __flags, ASM_REACHABLE);		\
	instrumentation_end();					\
} while (0)

#include <asm-generic/bug.h>

#endif /* _ASM_X86_BUG_H */
