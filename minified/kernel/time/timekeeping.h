 
#ifndef _KERNEL_TIME_TIMEKEEPING_H
#define _KERNEL_TIME_TIMEKEEPING_H
 
/* timekeeping_warp_clock, timekeeping_suspend, timekeeping_resume removed - unused */
extern void update_process_times(int user);
extern void do_timer(unsigned long ticks);
extern void update_wall_time(void);

extern raw_spinlock_t jiffies_lock;
extern seqcount_raw_spinlock_t jiffies_seq;
/* CS_NAME_LEN removed - 0-caller object-like const */

#endif
