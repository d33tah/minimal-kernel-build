#ifndef _ASM_X86_TIME_H
#define _ASM_X86_TIME_H
extern void time_init(void);
extern bool pit_timer_init(void);

extern struct clock_event_device *global_clock_event;

#endif
