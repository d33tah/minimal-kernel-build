#include <linux/slab.h>
#include <linux/sched/signal.h>

struct task_struct *kthreadd_task;

bool set_kthread_struct(struct task_struct *p)
{
	if (p->worker_private)
		return false;
	p->worker_private = (void *)1;
	return true;
}

void kthread_set_per_cpu(struct task_struct *k, int cpu)
{
}

int kthreadd(void *unused)
{
	__set_task_comm(current, "kthreadd", false);
	for (;;) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule();
	}
}
