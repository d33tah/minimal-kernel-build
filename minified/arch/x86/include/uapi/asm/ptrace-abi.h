#ifndef _ASM_X86_PTRACE_ABI_H
#define _ASM_X86_PTRACE_ABI_H

/* All ptrace register index macros (EBX, ECX, EDX, ESI, EDI, EBP, EAX,
   DS, ES, FS, GS, ORIG_EAX, EIP, CS, EFL, UESP, SS, FRAME_SIZE) removed -
   these are only used by userspace ptrace, not kernel.

   All PTRACE_* operation codes (PTRACE_GETREGS, PTRACE_SETREGS,
   PTRACE_GETFPREGS, PTRACE_SETFPREGS, PTRACE_GETFPXREGS, PTRACE_SETFPXREGS,
   PTRACE_OLDSETOPTIONS, PTRACE_GET_THREAD_AREA, PTRACE_SET_THREAD_AREA,
   PTRACE_SYSEMU, PTRACE_SYSEMU_SINGLESTEP, PTRACE_SINGLEBLOCK) removed -
   ptrace is stubbed in this minimal kernel. */

#ifndef __ASSEMBLY__
#include <linux/types.h>
#endif

#endif  
