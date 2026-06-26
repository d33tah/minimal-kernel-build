 
#ifndef _ASM_X86_PTRACE_ABI_H
#define _ASM_X86_PTRACE_ABI_H

/* 32-bit only kernel - removed x86_64 register definitions */
#define ECX 1
#define EDX 2
#define EBP 5
#define EAX 6
#define DS 7
#define ES 8
#define FS 9
#define GS 10
#define EIP 12
#define CS  13
#define SS   16

#ifndef __ASSEMBLY__
#include <linux/types.h>
#endif

#endif  
