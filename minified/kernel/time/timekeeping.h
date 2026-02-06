 
#ifndef _KERNEL_TIME_TIMEKEEPING_H
#define _KERNEL_TIME_TIMEKEEPING_H
 
/* ktime_get_update_offsets_now, timekeeping_valid_for_hres removed - never called */
/* timekeeping_warp_clock, timekeeping_suspend, timekeeping_resume,
   sched_clock_suspend, sched_clock_resume removed - unused */

/* update_process_times, do_timer, update_wall_time removed - never called */

extern raw_spinlock_t jiffies_lock;
extern seqcount_raw_spinlock_t jiffies_seq;

/* CS_NAME_LEN removed - never used */

#endif
