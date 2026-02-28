#include <linux/spinlock.h>
#include <linux/task_work.h>
#include <linux/sched/signal.h>

int task_work_add(struct task_struct *task, struct callback_head *work,
		  enum task_work_notify_mode notify)
{
	struct callback_head *head;

	do {
		head = READ_ONCE(task->task_works);
		work->next = head;
	} while (cmpxchg(&task->task_works, head, work) != head);

	test_and_set_tsk_thread_flag(task, TIF_NOTIFY_RESUME);
	return 0;
}

void task_work_run(void)
{
	struct task_struct *task = current;
	struct callback_head *work, *next;

	do {
		work = READ_ONCE(task->task_works);
		if (!work)
			return;
	} while (cmpxchg(&task->task_works, work, NULL) != work);

	do {
		next = work->next;
		work->func(work);
		work = next;
	} while (work);
}
