 
 
#ifndef _ASM_X86_FPU_SIGNAL_H
#define _ASM_X86_FPU_SIGNAL_H

#include <linux/compat.h>
#include <asm/user.h> /* linux/user.h redirect */

#include <asm/fpu/types.h>

extern void restore_fpregs_from_fpstate(struct fpstate *fpstate, u64 mask);
#endif  
