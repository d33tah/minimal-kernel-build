 
#ifndef _KERNEL_TIME_TIMEKEEPING_H
#define _KERNEL_TIME_TIMEKEEPING_H
 
/* timekeeping_warp_clock, timekeeping_suspend, timekeeping_resume,
   sched_clock_suspend, sched_clock_resume removed - unused */

extern raw_spinlock_t jiffies_lock;
extern seqcount_raw_spinlock_t jiffies_seq;

#endif
