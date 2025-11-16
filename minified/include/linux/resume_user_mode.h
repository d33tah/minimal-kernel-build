 

#ifndef LINUX_RESUME_USER_MODE_H
#define LINUX_RESUME_USER_MODE_H

#include <linux/sched.h>
#include <linux/task_work.h>
#include <linux/memcontrol.h>
#include <linux/blk-cgroup.h>

 
static inline void set_notify_resume(struct task_struct *task)
{
	if (!test_and_set_tsk_thread_flag(task, TIF_NOTIFY_RESUME))
		kick_process(task);
}


 
static inline void resume_user_mode_work(struct pt_regs *regs)
{
	clear_thread_flag(TIF_NOTIFY_RESUME);
	 
	smp_mb__after_atomic();
	if (unlikely(task_work_pending(current)))
		task_work_run();


	mem_cgroup_handle_over_high();
	blkcg_maybe_throttle_current();

	rseq_handle_notify_resume(NULL, regs);
}

#endif  
