/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_KGDB_H
#define _ASM_X86_KGDB_H

/*
 * Copyright (C) 2001-2004 Amit S. Kale
 * Copyright (C) 2008 Wind River Systems, Inc.
 */

#include <asm/ptrace.h>

/*
 * BUFMAX defines the maximum number of characters in inbound/outbound
 * buffers at least NUMREGBYTES*2 are needed for register packets
 * Longer buffer is needed to list all threads
 */
#define BUFMAX			1024

/*
 *  Note that this register image is in a different order than
 *  the register image that Linux produces at interrupt time.
 *
 *  Linux's register image is defined by struct pt_regs in ptrace.h.
 *  Just why GDB uses a different order is a historical mystery.
 */
enum regnames {
	GDB_AX,			/* 0 */
	GDB_CX,			/* 1 */
	GDB_DX,			/* 2 */
	GDB_BX,			/* 3 */
	GDB_SP,			/* 4 */
	GDB_BP,			/* 5 */
	GDB_SI,			/* 6 */
	GDB_DI,			/* 7 */
	GDB_PC,			/* 8 also known as eip */
	GDB_PS,			/* 9 also known as eflags */
	GDB_CS,			/* 10 */
	GDB_SS,			/* 11 */
	GDB_DS,			/* 12 */
	GDB_ES,			/* 13 */
	GDB_FS,			/* 14 */
	GDB_GS,			/* 15 */
};
#define GDB_ORIG_AX		41
#define DBG_MAX_REG_NUM		16
#define NUMREGBYTES		((GDB_GS+1)*4)

static inline void arch_kgdb_breakpoint(void)
{
	asm("   int $3");
}
#define BREAK_INSTR_SIZE	1
#define CACHE_FLUSH_IS_SAFE	1
#define GDB_ADJUSTS_BREAK_OFFSET

extern int kgdb_ll_trap(int cmd, const char *str,
			struct pt_regs *regs, long err, int trap, int sig);

#endif /* _ASM_X86_KGDB_H */
