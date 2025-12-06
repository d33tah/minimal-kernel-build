/* Minimal freezer.h - stubs only */
#ifndef FREEZER_H_INCLUDED
#define FREEZER_H_INCLUDED

struct task_struct;

static inline bool frozen(struct task_struct *p) { return false; }
static inline bool freezing(struct task_struct *p) { return false; }
/* __thaw_task, __refrigerator, freeze_processes, freeze_kernel_threads removed - unused */
/* thaw_processes, thaw_kernel_threads, try_to_freeze removed - unused */

static inline void freezer_do_not_count(void) {}
static inline void freezer_count(void) {}
/* freezer_should_skip, set_freezable removed - unused */

#define freezable_schedule()  schedule()

#define freezable_schedule_timeout(timeout)  schedule_timeout(timeout)

#define freezable_schedule_timeout_interruptible(timeout)		\
	schedule_timeout_interruptible(timeout)

#define freezable_schedule_timeout_killable(timeout)			\
	schedule_timeout_killable(timeout)

/* Unused macros removed: freezable_schedule_unsafe, *_interruptible_unsafe,
   *_killable_unsafe, *_hrtimeout_range, wait_event_freezekillable_unsafe */


#endif	 
