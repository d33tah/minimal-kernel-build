 
 

#ifndef FREEZER_H_INCLUDED
#define FREEZER_H_INCLUDED

#include <linux/debug_locks.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/atomic.h>

static inline bool frozen(struct task_struct *p) { return false; }
static inline bool freezing(struct task_struct *p) { return false; }
static inline void __thaw_task(struct task_struct *t) {}

static inline bool __refrigerator(bool check_kthr_stop) { return false; }
static inline int freeze_processes(void) { return -ENOSYS; }
static inline int freeze_kernel_threads(void) { return -ENOSYS; }
static inline void thaw_processes(void) {}
static inline void thaw_kernel_threads(void) {}

static inline bool try_to_freeze(void) { return false; }

static inline void freezer_do_not_count(void) {}
static inline void freezer_count(void) {}
static inline int freezer_should_skip(struct task_struct *p) { return 0; }
static inline void set_freezable(void) {}

#define freezable_schedule()  schedule()

#define freezable_schedule_unsafe()  schedule()

#define freezable_schedule_timeout(timeout)  schedule_timeout(timeout)

#define freezable_schedule_timeout_interruptible(timeout)		\
	schedule_timeout_interruptible(timeout)

#define freezable_schedule_timeout_interruptible_unsafe(timeout)	\
	schedule_timeout_interruptible(timeout)

#define freezable_schedule_timeout_killable(timeout)			\
	schedule_timeout_killable(timeout)

#define freezable_schedule_timeout_killable_unsafe(timeout)		\
	schedule_timeout_killable(timeout)

#define freezable_schedule_hrtimeout_range(expires, delta, mode)	\
	schedule_hrtimeout_range(expires, delta, mode)

#define wait_event_freezekillable_unsafe(wq, condition)			\
		wait_event_killable(wq, condition)


#endif	 
