#include <linux/sched/signal.h>
#include <linux/task_work.h>

static struct callback_head work_exited;  

int task_work_add(struct task_struct *task, struct callback_head *work,
		  enum task_work_notify_mode notify)
{
	/*
	 * SAFE-FALLBACK STUB (runtime-dead). The sole caller fput()
	 * only reaches this on the non-interrupt, non-kthread file-close
	 * path; on a boot-once-and-print artifact that branch never fires
	 * (task_work_add HIT=False). Returning -ESRCH ("cannot queue work")
	 * is the conservative fallback: fput() then defers the __fput via
	 * its llist/delayed_fput_work path, which stays correct. The
	 * cmpxchg/notify body is the dead payoff.
	 */
	return -ESRCH;
}

void task_work_run(void)
{
	struct task_struct *task = current;
	struct callback_head *work, *head, *next;

	for (;;) {
		 
		do {
			head = NULL;
			work = READ_ONCE(task->task_works);
			if (!work) {
				if (task->flags & PF_EXITING)
					head = &work_exited;
				else
					break;
			}
		} while (cmpxchg(&task->task_works, work, head) != work);

		if (!work)
			break;
		 
		raw_spin_lock_irq(&task->pi_lock);
		raw_spin_unlock_irq(&task->pi_lock);

		do {
			next = work->next;
			work->func(work);
			work = next;
			cond_resched();
		} while (work);
	}
}
