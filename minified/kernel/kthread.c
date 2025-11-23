 
 
#include <uapi/linux/sched/types.h>
#include <linux/mm.h>
#include <linux/mmu_context.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/sched/task.h>
#include <linux/kthread.h>
#include <linux/completion.h>
#include <linux/err.h>
#include <linux/cgroup.h>
#include <linux/cpuset.h>
#include <linux/unistd.h>
#include <linux/file.h>
#include <linux/export.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/freezer.h>
#include <linux/ptrace.h>
#include <linux/uaccess.h>
#include <linux/numa.h>
#include <linux/sched/isolation.h>


static DEFINE_SPINLOCK(kthread_create_lock);
static LIST_HEAD(kthread_create_list);
struct task_struct *kthreadd_task;

struct kthread_create_info
{
	 
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

 
static inline struct kthread *__to_kthread(struct task_struct *p)
{
	void *kthread = p->worker_private;
	if (kthread && !(p->flags & PF_KTHREAD))
		kthread = NULL;
	return kthread;
}

void get_kthread_comm(char *buf, size_t buf_size, struct task_struct *tsk)
{
	struct kthread *kthread = to_kthread(tsk);

	if (!kthread || !kthread->full_name) {
		__get_task_comm(buf, buf_size, tsk);
		return;
	}

	strscpy_pad(buf, kthread->full_name, buf_size);
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
	p->vfork_done = &kthread->exited;

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

 
bool kthread_should_stop(void)
{
	return test_bit(KTHREAD_SHOULD_STOP, &to_kthread(current)->flags);
}

bool __kthread_should_park(struct task_struct *k)
{
	return test_bit(KTHREAD_SHOULD_PARK, &to_kthread(k)->flags);
}

 
bool kthread_should_park(void)
{
	return __kthread_should_park(current);
}

 
bool kthread_freezable_should_stop(bool *was_frozen)
{
	bool frozen = false;

	might_sleep();

	if (unlikely(freezing(current)))
		frozen = __refrigerator(true);

	if (was_frozen)
		*was_frozen = frozen;

	return kthread_should_stop();
}

 
void *kthread_func(struct task_struct *task)
{
	struct kthread *kthread = __to_kthread(task);
	if (kthread)
		return kthread->threadfn;
	return NULL;
}

 
void *kthread_data(struct task_struct *task)
{
	return to_kthread(task)->data;
}

 
void *kthread_probe_data(struct task_struct *task)
{
	struct kthread *kthread = __to_kthread(task);
	void *data = NULL;

	if (kthread)
		copy_from_kernel_nofault(&data, &kthread->data, sizeof(data));
	return data;
}

static void __kthread_parkme(struct kthread *self)
{
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
}

void kthread_parkme(void)
{
	__kthread_parkme(to_kthread(current));
}

 
void __noreturn kthread_exit(long result)
{
	struct kthread *kthread = to_kthread(current);
	kthread->result = result;
	do_exit(0);
}

 
void __noreturn kthread_complete_and_exit(struct completion *comp, long code)
{
	if (comp)
		complete(comp);

	kthread_exit(code);
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
	set_cpus_allowed_ptr(current, housekeeping_cpumask(HK_TYPE_KTHREAD));

	 
	__set_current_state(TASK_UNINTERRUPTIBLE);
	create->result = current;
	 
	preempt_disable();
	complete(done);
	schedule_preempt_disabled();
	preempt_enable();

	ret = -EINTR;
	if (!test_bit(KTHREAD_SHOULD_STOP, &self->flags)) {
		cgroup_kthread_ready();
		__kthread_parkme(self);
		ret = threadfn(data);
	}
	kthread_exit(ret);
}

 
int tsk_fork_get_node(struct task_struct *tsk)
{
	return NUMA_NO_NODE;
}

static void create_kthread(struct kthread_create_info *create)
{
	int pid;

	 
	pid = kernel_thread(kthread, create, CLONE_FS | CLONE_FILES | SIGCHLD);
	if (pid < 0) {
		 
		struct completion *done = xchg(&create->done, NULL);

		if (!done) {
			kfree(create);
			return;
		}
		create->result = ERR_PTR(pid);
		complete(done);
	}
}

static __printf(4, 0)
struct task_struct *__kthread_create_on_node(int (*threadfn)(void *data),
						    void *data, int node,
						    const char namefmt[],
						    va_list args)
{
	DECLARE_COMPLETION_ONSTACK(done);
	struct task_struct *task;
	struct kthread_create_info *create = kmalloc(sizeof(*create),
						     GFP_KERNEL);

	if (!create)
		return ERR_PTR(-ENOMEM);
	create->threadfn = threadfn;
	create->data = data;
	create->node = node;
	create->done = &done;

	spin_lock(&kthread_create_lock);
	list_add_tail(&create->list, &kthread_create_list);
	spin_unlock(&kthread_create_lock);

	wake_up_process(kthreadd_task);
	 
	if (unlikely(wait_for_completion_killable(&done))) {
		 
		if (xchg(&create->done, NULL))
			return ERR_PTR(-EINTR);
		 
		wait_for_completion(&done);
	}
	task = create->result;
	if (!IS_ERR(task)) {
		char name[TASK_COMM_LEN];
		va_list aq;
		int len;

		 
		va_copy(aq, args);
		len = vsnprintf(name, sizeof(name), namefmt, aq);
		va_end(aq);
		if (len >= TASK_COMM_LEN) {
			struct kthread *kthread = to_kthread(task);

			 
			kthread->full_name = kvasprintf(GFP_KERNEL, namefmt, args);
		}
		set_task_comm(task, name);
	}
	kfree(create);
	return task;
}

 
struct task_struct *kthread_create_on_node(int (*threadfn)(void *data),
					   void *data, int node,
					   const char namefmt[],
					   ...)
{
	struct task_struct *task;
	va_list args;

	va_start(args, namefmt);
	task = __kthread_create_on_node(threadfn, data, node, namefmt, args);
	va_end(args);

	return task;
}

static void __kthread_bind_mask(struct task_struct *p, const struct cpumask *mask, unsigned int state)
{
	unsigned long flags;

	if (!wait_task_inactive(p, state)) {
		WARN_ON(1);
		return;
	}

	 
	raw_spin_lock_irqsave(&p->pi_lock, flags);
	do_set_cpus_allowed(p, mask);
	p->flags |= PF_NO_SETAFFINITY;
	raw_spin_unlock_irqrestore(&p->pi_lock, flags);
}

static void __kthread_bind(struct task_struct *p, unsigned int cpu, unsigned int state)
{
	__kthread_bind_mask(p, cpumask_of(cpu), state);
}

void kthread_bind_mask(struct task_struct *p, const struct cpumask *mask)
{
	__kthread_bind_mask(p, mask, TASK_UNINTERRUPTIBLE);
}

 
void kthread_bind(struct task_struct *p, unsigned int cpu)
{
	__kthread_bind(p, cpu, TASK_UNINTERRUPTIBLE);
}

 
struct task_struct *kthread_create_on_cpu(int (*threadfn)(void *data),
					  void *data, unsigned int cpu,
					  const char *namefmt)
{
	struct task_struct *p;

	p = kthread_create_on_node(threadfn, data, cpu_to_node(cpu), namefmt,
				   cpu);
	if (IS_ERR(p))
		return p;
	kthread_bind(p, cpu);
	 
	to_kthread(p)->cpu = cpu;
	return p;
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

bool kthread_is_per_cpu(struct task_struct *p)
{
	struct kthread *kthread = __to_kthread(p);
	if (!kthread)
		return false;

	return test_bit(KTHREAD_IS_PER_CPU, &kthread->flags);
}

 
void kthread_unpark(struct task_struct *k)
{
	struct kthread *kthread = to_kthread(k);

	 
	if (test_bit(KTHREAD_IS_PER_CPU, &kthread->flags))
		__kthread_bind(k, kthread->cpu, TASK_PARKED);

	clear_bit(KTHREAD_SHOULD_PARK, &kthread->flags);
	 
	wake_up_state(k, TASK_PARKED);
}

 
int kthread_park(struct task_struct *k)
{
	struct kthread *kthread = to_kthread(k);

	if (WARN_ON(k->flags & PF_EXITING))
		return -ENOSYS;

	if (WARN_ON_ONCE(test_bit(KTHREAD_SHOULD_PARK, &kthread->flags)))
		return -EBUSY;

	set_bit(KTHREAD_SHOULD_PARK, &kthread->flags);
	if (k != current) {
		wake_up_process(k);
		 
		wait_for_completion(&kthread->parked);
		 
		WARN_ON_ONCE(!wait_task_inactive(k, TASK_PARKED));
	}

	return 0;
}

 
int kthread_stop(struct task_struct *k)
{
	struct kthread *kthread;
	int ret;

	 

	get_task_struct(k);
	kthread = to_kthread(k);
	set_bit(KTHREAD_SHOULD_STOP, &kthread->flags);
	kthread_unpark(k);
	wake_up_process(k);
	wait_for_completion(&kthread->exited);
	ret = kthread->result;
	put_task_struct(k);

	 
	return ret;
}

int kthreadd(void *unused)
{
	struct task_struct *tsk = current;

	 
	set_task_comm(tsk, "kthreadd");
	ignore_signals(tsk);
	set_cpus_allowed_ptr(tsk, housekeeping_cpumask(HK_TYPE_KTHREAD));
	set_mems_allowed(node_states[N_MEMORY]);

	current->flags |= PF_NOFREEZE;
	cgroup_init_kthreadd();

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

			create_kthread(create);

			spin_lock(&kthread_create_lock);
		}
		spin_unlock(&kthread_create_lock);
	}

	return 0;
}

/* Kthread worker infrastructure - stubbed (not used) */
void __kthread_init_worker(struct kthread_worker *worker,
				const char *name,
				struct lock_class_key *key)
{
}

int kthread_worker_fn(void *worker_ptr)
{
	return 0;
}

struct kthread_worker *
kthread_create_worker(unsigned int flags, const char namefmt[], ...)
{
	return ERR_PTR(-ENOSYS);
}

struct kthread_worker *
kthread_create_worker_on_cpu(int cpu, unsigned int flags,
			     const char namefmt[], ...)
{
	return ERR_PTR(-ENOSYS);
}

bool kthread_queue_work(struct kthread_worker *worker,
			struct kthread_work *work)
{
	return false;
}

void kthread_delayed_work_timer_fn(struct timer_list *t)
{
}

bool kthread_queue_delayed_work(struct kthread_worker *worker,
				struct kthread_delayed_work *dwork,
				unsigned long delay)
{
	return false;
}

void kthread_flush_work(struct kthread_work *work)
{
}

bool kthread_mod_delayed_work(struct kthread_worker *worker,
			      struct kthread_delayed_work *dwork,
			      unsigned long delay)
{
	return false;
}

bool kthread_cancel_work_sync(struct kthread_work *work)
{
	return false;
}

bool kthread_cancel_delayed_work_sync(struct kthread_delayed_work *dwork)
{
	return false;
}

void kthread_flush_worker(struct kthread_worker *worker)
{
}

void kthread_destroy_worker(struct kthread_worker *worker)
{
}

void kthread_use_mm(struct mm_struct *mm)
{
}

void kthread_unuse_mm(struct mm_struct *mm)
{
}

