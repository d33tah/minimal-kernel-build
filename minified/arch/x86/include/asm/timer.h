
#ifndef _ASM_X86_TIMER_H
#define _ASM_X86_TIMER_H

struct cyc2ns_data {
	u32 cyc2ns_mul, cyc2ns_shift;
	u64 cyc2ns_offset;
};

#endif
