 
#ifndef _ASM_X86_PTRACE_ABI_H
#define _ASM_X86_PTRACE_ABI_H

/* 32-bit only kernel - removed x86_64 register definitions */
#define EBX 0
#define ECX 1
#define EDX 2
#define ESI 3
#define EDI 4
#define EBP 5
#define EAX 6
#define DS 7
#define ES 8
#define FS 9
#define GS 10
#define ORIG_EAX 11
#define EIP 12
#define CS  13
#define EFL 14
#define UESP 15
#define SS   16
#define FRAME_SIZE 17  

 
#define PTRACE_GETREGS            12
#define PTRACE_SETREGS            13
#define PTRACE_GETFPREGS          14
#define PTRACE_SETFPREGS          15
#define PTRACE_GETFPXREGS         18
#define PTRACE_SETFPXREGS         19

#define PTRACE_OLDSETOPTIONS      21


#define PTRACE_GET_THREAD_AREA    25
#define PTRACE_SET_THREAD_AREA    26
/* Removed PTRACE_ARCH_PRCTL - x86_64 only */

#define PTRACE_SYSEMU		  31
#define PTRACE_SYSEMU_SINGLESTEP  32

#define PTRACE_SINGLEBLOCK	33	 

#ifndef __ASSEMBLY__
#include <linux/types.h>
#endif

#endif  
