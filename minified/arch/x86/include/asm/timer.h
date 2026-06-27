 
#ifndef _ASM_X86_TIMER_H
#define _ASM_X86_TIMER_H
#include <linux/percpu.h>
#include <linux/interrupt.h>
#include <linux/math64.h>


unsigned long long native_sched_clock(void);

extern int no_timer_check;


struct cyc2ns_data {
	u32 cyc2ns_mul;
	u32 cyc2ns_shift;
	u64 cyc2ns_offset;
};

#endif
