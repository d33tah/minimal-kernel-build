 
#ifndef _KERNEL_TIME_TIMEKEEPING_H
#define _KERNEL_TIME_TIMEKEEPING_H
 
extern ktime_t ktime_get_update_offsets_now(unsigned int *cwsseq,
					    ktime_t *offs_real,
					    ktime_t *offs_boot,
					    ktime_t *offs_tai);

extern int timekeeping_valid_for_hres(void);
/* timekeeping_warp_clock, timekeeping_suspend, timekeeping_resume,
   sched_clock_suspend, sched_clock_resume removed - unused */

extern void update_process_times(int user);
extern void do_timer(unsigned long ticks);
extern void update_wall_time(void);

extern raw_spinlock_t jiffies_lock;
extern seqcount_raw_spinlock_t jiffies_seq;

#define CS_NAME_LEN	32

#endif
