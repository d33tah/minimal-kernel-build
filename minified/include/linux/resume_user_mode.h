
#ifndef LINUX_RESUME_USER_MODE_H
#define LINUX_RESUME_USER_MODE_H

#include <linux/sched.h>
#include <linux/task_work.h>
#include <linux/memcontrol.h>

static inline void set_notify_resume(struct task_struct *task)
{
	test_and_set_tsk_thread_flag(task, TIF_NOTIFY_RESUME);
}


static inline void resume_user_mode_work(struct pt_regs *regs)
{
	clear_thread_flag(TIF_NOTIFY_RESUME);
	 
	smp_mb__after_atomic();
	if (unlikely(task_work_pending(current)))
		task_work_run();
}

#endif  
