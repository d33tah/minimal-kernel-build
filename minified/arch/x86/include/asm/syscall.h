/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Access to user system call parameters and results
 *
 * Copyright (C) 2008-2009 Red Hat, Inc.  All rights reserved.
 *
 * See asm-generic/syscall.h for descriptions of what we must do here.
 */

#ifndef _ASM_X86_SYSCALL_H
#define _ASM_X86_SYSCALL_H

#include <linux/elf.h>
#include <linux/sched.h>

#include <asm/unistd.h>

typedef long (*sys_call_ptr_t)(const struct pt_regs *);
extern const sys_call_ptr_t sys_call_table[];

#define ia32_sys_call_table sys_call_table

#endif
