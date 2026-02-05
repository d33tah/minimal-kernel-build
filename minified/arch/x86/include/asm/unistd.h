
#ifndef _ASM_X86_UNISTD_H
#define _ASM_X86_UNISTD_H 1

#include <uapi/asm/unistd.h>

#  include <asm/unistd_32.h>

#  define IA32_NR_syscalls (__NR_syscalls)

# define NR_syscalls (__NR_syscalls)

/* All __ARCH_WANT_* defines removed - syscall table reduced to 3 entries */

#endif
