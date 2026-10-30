
#ifndef _KERNEL_TIME_TIMEKEEPING_H
#define _KERNEL_TIME_TIMEKEEPING_H

extern void do_timer(unsigned long ticks);
extern void update_wall_time(void);

#endif
