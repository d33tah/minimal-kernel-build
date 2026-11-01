#include <linux/pid_namespace.h>
#include <linux/sched/signal.h>

struct pid init_struct_pid = {
	.count = REFCOUNT_INIT(1),
	.level = 0,
	.numbers = { {
		.nr = 0,
		.ns = &init_pid_ns,
	}, },
};

static int pid_max = PID_MAX_DEFAULT;

struct pid_namespace init_pid_ns = {
	.level = 0,
};

static __cacheline_aligned_in_smp DEFINE_SPINLOCK(pidmap_lock);

void put_pid(struct pid *pid)
{
	struct pid_namespace *ns;

	if (!pid)
		return;

	ns = pid->numbers[pid->level].ns;
	if (refcount_dec_and_test(&pid->count)) {
		kmem_cache_free(ns->pid_cachep, pid);
	}
}

/*
 * Pid numbers come from a counter: nothing looks a pid up by number (there
 * is no find_pid_ns/find_vpid) and free_pid never returns one, so the next
 * unused number is always last_pid + 1 (no pid is ever freed: a failed
 * copy_process cannot happen after alloc_pid). init_task is pid 0, init gets 1.
 * Exhaustion fails with -EAGAIN as alloc_pid did when its IDR was full.
 */
struct pid *alloc_pid(struct pid_namespace *ns)
{
	struct pid *pid;
	int nr;

	pid = kmem_cache_alloc(ns->pid_cachep, GFP_KERNEL);
	if (!pid)
		return ERR_PTR(-ENOMEM);

	spin_lock_irq(&pidmap_lock);
	nr = ns->last_pid + 1 < pid_max ? ++ns->last_pid : -EAGAIN;
	spin_unlock_irq(&pidmap_lock);
	if (nr < 0) {
		kmem_cache_free(ns->pid_cachep, pid);
		return ERR_PTR(nr);
	}

	pid->level = ns->level;
	pid->numbers[ns->level].nr = nr;
	pid->numbers[ns->level].ns = ns;
	get_pid_ns(ns);
	refcount_set(&pid->count, 1);

	return pid;
}

static struct pid **task_pid_ptr(struct task_struct *task, enum pid_type type)
{
	return &task->thread_pid;
}

struct pid *get_task_pid(struct task_struct *task, enum pid_type type)
{
	struct pid *pid;
	rcu_read_lock();
	pid = get_pid(rcu_dereference(*task_pid_ptr(task, type)));
	rcu_read_unlock();
	return pid;
}

static pid_t pid_nr_ns(struct pid *pid, struct pid_namespace *ns)
{
	struct upid *upid;
	pid_t nr = 0;

	if (pid && ns->level <= pid->level) {
		upid = &pid->numbers[ns->level];
		if (upid->ns == ns)
			nr = upid->nr;
	}
	return nr;
}

pid_t pid_vnr(struct pid *pid)
{
	return pid_nr_ns(pid, task_active_pid_ns(current));
}

struct pid_namespace *task_active_pid_ns(struct task_struct *tsk)
{
	return ns_of_pid(task_pid(tsk));
}

void __init pid_idr_init(void)
{
	pid_max = min_t(int, PID_MAX_LIMIT,
			max_t(int, pid_max,
			      PIDS_PER_CPU_DEFAULT *num_possible_cpus()));

	init_pid_ns.pid_cachep =
		KMEM_CACHE(pid, SLAB_HWCACHE_ALIGN | SLAB_PANIC | SLAB_ACCOUNT);
}
