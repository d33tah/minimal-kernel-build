#ifndef _ASM_X86_TIME_H
#define _ASM_X86_TIME_H

#include <linux/clocksource.h>
#include <linux/time64.h>  /* for struct timespec64 */
extern void mach_get_cmos_time(struct timespec64 *now);

static inline void time_init(void) { }

#endif
