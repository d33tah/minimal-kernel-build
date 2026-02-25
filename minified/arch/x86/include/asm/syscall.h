/* --- 2025-12-06 13:10 --- elf-em.h replaced with elf.h */
#ifndef _ASM_X86_SYSCALL_H
#define _ASM_X86_SYSCALL_H

#include <linux/elf.h>
#include <linux/sched.h>

#include <linux/err.h>
#include <asm/thread_info.h>	 
/* unistd_32.h inlined */
#define __NR_ni_syscall 0
#define __NR_exit 1
#define __NR_write 4
#define __NR_syscalls 5
#define IA32_NR_syscalls (__NR_syscalls)

typedef long (*sys_call_ptr_t)(const struct pt_regs *);
extern const sys_call_ptr_t sys_call_table[];

#define ia32_sys_call_table sys_call_table

#endif	 
