#include <linux/slab.h>
#include <linux/sched/signal.h>

struct task_struct *kthreadd_task;

struct kthread {
	unsigned long flags;
	unsigned int cpu;
};

enum KTHREAD_BITS {
	KTHREAD_IS_PER_CPU = 0,
};

static inline struct kthread *to_kthread(struct task_struct *k)
{
	WARN_ON(!(k->flags & PF_KTHREAD));
	return k->worker_private;
}

bool set_kthread_struct(struct task_struct *p)
{
	struct kthread *kthread;

	if (WARN_ON_ONCE(to_kthread(p)))
		return false;

	kthread = kzalloc(sizeof(*kthread), GFP_KERNEL);
	if (!kthread)
		return false;

	p->worker_private = kthread;
	return true;
}

void kthread_set_per_cpu(struct task_struct *k, int cpu)
{
	struct kthread *kthread = to_kthread(k);
	if (!kthread)
		return;

	WARN_ON_ONCE(!(k->flags & PF_NO_SETAFFINITY));

	if (cpu < 0) {
		clear_bit(KTHREAD_IS_PER_CPU, &kthread->flags);
		return;
	}

	kthread->cpu = cpu;
	set_bit(KTHREAD_IS_PER_CPU, &kthread->flags);
}

int kthreadd(void *unused)
{
	struct task_struct *tsk = current;

	__set_task_comm(tsk, "kthreadd", false);
	{
		int i;
		for (i = 0; i < _NSIG; ++i)
			tsk->sighand->action[i].sa.sa_handler = SIG_IGN;
	}
	set_cpus_allowed_ptr(tsk, cpu_possible_mask);
	current->flags |= PF_NOFREEZE;

	for (;;) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule();
	}
}
