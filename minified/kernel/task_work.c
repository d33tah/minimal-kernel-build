#include <linux/spinlock.h>
#include <linux/sched/signal.h>
#include <linux/task_work.h>
#include <linux/resume_user_mode.h>

static struct callback_head work_exited;  

int task_work_add(struct task_struct *task, struct callback_head *work,
		  enum task_work_notify_mode notify)
{
	struct callback_head *head;

	do {
		head = READ_ONCE(task->task_works);
		if (unlikely(head == &work_exited))
			return -ESRCH;
		work->next = head;
	} while (cmpxchg(&task->task_works, head, work) != head);

	switch (notify) {
	case TWA_NONE:
		break;
	case TWA_RESUME:
		set_notify_resume(task);
		break;
	case TWA_SIGNAL:
		set_notify_signal(task);
		break;
	case TWA_SIGNAL_NO_IPI:
		__set_notify_signal(task);
		break;
	default:
		WARN_ON_ONCE(1);
		break;
	}

	return 0;
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
