 
#ifndef _ASM_X86_VSYSCALL_H
#define _ASM_X86_VSYSCALL_H

#include <linux/seqlock.h>
#include <uapi/asm/vsyscall.h>

static inline void map_vsyscall(void) {}
static inline bool emulate_vsyscall(unsigned long error_code,
				    struct pt_regs *regs, unsigned long address)
{
	return false;
}

#endif  
