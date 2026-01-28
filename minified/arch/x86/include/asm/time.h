
#ifndef _ASM_X86_TIME_H
#define _ASM_X86_TIME_H

#include <linux/clocksource.h>
#include <asm/mc146818rtc.h>

/* hpet_time_init, pit_timer_init, global_clock_event removed -
   x86_init.timers.timer_init never called, entire timer init chain is dead */
extern void time_init(void);

#endif
