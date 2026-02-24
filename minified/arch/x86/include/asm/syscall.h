/* --- 2025-12-06 13:10 --- elf-em.h replaced with elf.h */
#ifndef _ASM_X86_SYSCALL_H
#define _ASM_X86_SYSCALL_H

#include <linux/elf.h>
#include <linux/sched.h>

#include <linux/err.h>
#include <asm/thread_info.h>	 
#include <asm/unistd_32.h>
#define IA32_NR_syscalls (__NR_syscalls)

typedef long (*sys_call_ptr_t)(const struct pt_regs *);
extern const sys_call_ptr_t sys_call_table[];

#define ia32_sys_call_table sys_call_table

#endif	 
