
#ifndef LINUX_RESUME_USER_MODE_H
#define LINUX_RESUME_USER_MODE_H

#include <linux/sched.h>
#include <linux/task_work.h>
#include <linux/memcontrol.h>

/* set_notify_resume: 0-caller static-inline orphan removed (LOC reduction) */


static inline void resume_user_mode_work(struct pt_regs *regs) {
	clear_thread_flag(TIF_NOTIFY_RESUME);
	 
	smp_mb__after_atomic();
	if (unlikely(task_work_pending(current)))
		task_work_run(); }

#endif  
