#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/sched/task.h>
#include <linux/kthread.h>
#include <linux/completion.h>
#include <linux/err.h>
#include <asm/unistd.h>
#include <linux/slab.h>
#include <linux/ptrace.h>
#include <linux/cpumask.h>

static DEFINE_SPINLOCK(kthread_create_lock);
static LIST_HEAD(kthread_create_list);
struct task_struct *kthreadd_task;

struct kthread_create_info {
	int (*threadfn)(void *data);
	void *data;
	int node;

	struct task_struct *result;
	struct completion *done;

	struct list_head list;
};

struct kthread {
	unsigned long flags;
	unsigned int cpu;
	int result;
	int (*threadfn)(void *);
	void *data;
	struct completion parked;
	struct completion exited;

	char *full_name;
};

enum KTHREAD_BITS {
	KTHREAD_IS_PER_CPU = 0,
	KTHREAD_SHOULD_STOP,
	KTHREAD_SHOULD_PARK,
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

	init_completion(&kthread->exited);
	init_completion(&kthread->parked);

	p->worker_private = kthread;
	return true;
}

void free_kthread_struct(struct task_struct *k)
{
	struct kthread *kthread;

	kthread = to_kthread(k);
	if (!kthread)
		return;

	k->worker_private = NULL;
	kfree(kthread->full_name);
	kfree(kthread);
}

static void __noreturn kthread_exit(long result)
{
	struct kthread *kthread = to_kthread(current);
	kthread->result = result;
	do_exit(0);
}

static int kthread(void *_create)
{
	static const struct sched_param param = { .sched_priority = 0 };

	struct kthread_create_info *create = _create;
	int (*threadfn)(void *data) = create->threadfn;
	void *data = create->data;
	struct completion *done;
	struct kthread *self;
	int ret;

	self = to_kthread(current);

	done = xchg(&create->done, NULL);
	if (!done) {
		kfree(create);
		kthread_exit(-EINTR);
	}

	self->threadfn = threadfn;
	self->data = data;

	sched_setscheduler_nocheck(current, SCHED_NORMAL, &param);
	set_cpus_allowed_ptr(current, cpu_possible_mask);

	__set_current_state(TASK_UNINTERRUPTIBLE);
	create->result = current;

	preempt_disable();
	complete(done);
	schedule_preempt_disabled();
	preempt_enable();

	ret = -EINTR;
	if (!test_bit(KTHREAD_SHOULD_STOP, &self->flags)) {
		for (;;) {
			set_special_state(TASK_PARKED);
			if (!test_bit(KTHREAD_SHOULD_PARK, &self->flags))
				break;
			preempt_disable();
			complete(&self->parked);
			schedule_preempt_disabled();
			preempt_enable();
		}
		__set_current_state(TASK_RUNNING);
		ret = threadfn(data);
	}
	kthread_exit(ret);
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

	__set_task_comm(tsk, "kthreadd", false); /* set_task_comm inlined */
	{
		int i;
		for (i = 0; i < _NSIG; ++i)
			tsk->sighand->action[i].sa.sa_handler = SIG_IGN;
	}
	set_cpus_allowed_ptr(tsk, cpu_possible_mask);

	current->flags |= PF_NOFREEZE;

	for (;;) {
		set_current_state(TASK_INTERRUPTIBLE);
		if (list_empty(&kthread_create_list))
			schedule();
		__set_current_state(TASK_RUNNING);

		spin_lock(&kthread_create_lock);
		while (!list_empty(&kthread_create_list)) {
			struct kthread_create_info *create;

			create = list_entry(kthread_create_list.next,
					    struct kthread_create_info, list);
			list_del_init(&create->list);
			spin_unlock(&kthread_create_lock);

			{
				int pid;
				pid = kernel_thread(kthread, create,
						    CLONE_FS | CLONE_FILES |
							    SIGCHLD);
				if (pid < 0) {
					struct completion *done =
						xchg(&create->done, NULL);
					if (!done) {
						kfree(create);
					} else {
						create->result = ERR_PTR(pid);
						complete(done);
					}
				}
			}

			spin_lock(&kthread_create_lock);
		}
		spin_unlock(&kthread_create_lock);
	}

	return 0;
}
