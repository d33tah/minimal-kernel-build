 
#ifndef _ASM_X86_PERF_EVENT_H
#define _ASM_X86_PERF_EVENT_H

 

#include <linux/static_call.h>
#include <asm/stacktrace.h>

 
struct perf_guest_switch_msr {
	unsigned msr;
	u64 host, guest;
};

extern void perf_clear_dirty_counters(void);

/* perf_guest_get_msrs, perf_misc_flags removed - unused */

#define perf_arch_fetch_caller_regs(regs, __ip) do { \
	(regs)->ip = (__ip); \
	(regs)->sp = (unsigned long)__builtin_frame_address(0); \
	(regs)->cs = __KERNEL_CS; \
	(regs)->flags = 0; \
} while (0)

#define arch_perf_out_copy_user copy_from_user_nmi

#endif  
