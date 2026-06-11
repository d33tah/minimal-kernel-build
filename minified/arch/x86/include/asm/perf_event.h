 
#ifndef _ASM_X86_PERF_EVENT_H
#define _ASM_X86_PERF_EVENT_H

 

#include <linux/static_call.h>
#include <asm/stacktrace.h>



static inline unsigned long perf_misc_flags(struct pt_regs *regs) { return 0; }

#define perf_misc_flags(regs) perf_misc_flags(regs)

 
#define perf_arch_fetch_caller_regs(regs, __ip) do { \
	(regs)->ip = (__ip); \
	(regs)->sp = (unsigned long)__builtin_frame_address(0); \
	(regs)->cs = __KERNEL_CS; \
	(regs)->flags = 0; \
} while (0)

#endif
