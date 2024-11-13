/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _ASM_X86_KPROBES_H
#define _ASM_X86_KPROBES_H
/*
 *  Kernel Probes (KProbes)
 *
 * Copyright (C) IBM Corporation, 2002, 2004
 *
 * See arch/x86/kernel/kprobes.c for x86 kprobes history.
 */

#include <asm-generic/kprobes.h>


static inline int kprobe_debug_handler(struct pt_regs *regs) { return 0; }

#endif /* _ASM_X86_KPROBES_H */
