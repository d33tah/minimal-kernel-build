
#include <linux/slab.h>
#include <linux/init_task.h>
#include <linux/sched/signal.h>

struct pid init_struct_pid = {
	.count		= REFCOUNT_INIT(1),
	.tasks		= {
		{ .first = NULL },
		{ .first = NULL },
		{ .first = NULL },
	},
	.level		= 0,
	.numbers	= { {
		.nr		= 0,
		.ns		= &init_pid_ns,
	}, }
};

int pid_max = PID_MAX_DEFAULT;

struct pid_namespace init_pid_ns = {
	.ns.count = REFCOUNT_INIT(2),
	.idr = IDR_INIT(init_pid_ns.idr),
	.pid_allocated = PIDNS_ADDING,
	.level = 0,
	.child_reaper = &init_task,
	.user_ns = &init_user_ns,
	.ns.inum = 0xEFFFFFFCU,
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

/* Simplified for single PID namespace (level=0), no set_tid */
struct pid *alloc_pid(struct pid_namespace *ns, pid_t *set_tid,
		      size_t set_tid_size)
{
	struct pid *pid;
	enum pid_type type;
	int nr;

	pid = kmem_cache_alloc(ns->pid_cachep, GFP_KERNEL);
	if (!pid)
		return ERR_PTR(-ENOMEM);

	pid->level = 0;

	idr_preload(GFP_KERNEL);
	spin_lock_irq(&pidmap_lock);
	nr = idr_alloc_cyclic(&ns->idr, NULL, 1, pid_max, GFP_ATOMIC);
	spin_unlock_irq(&pidmap_lock);
	local_unlock(&radix_tree_preloads.lock);

	if (nr < 0) {
		kmem_cache_free(ns->pid_cachep, pid);
		return ERR_PTR((nr == -ENOSPC) ? -EAGAIN : nr);
	}

	pid->numbers[0].nr = nr;
	pid->numbers[0].ns = ns;

	get_pid_ns(ns);
	refcount_set(&pid->count, 1);
	for (type = 0; type < PIDTYPE_MAX; ++type)
		INIT_HLIST_HEAD(&pid->tasks[type]);

	spin_lock_irq(&pidmap_lock);
	if (!(ns->pid_allocated & PIDNS_ADDING)) {
		spin_unlock_irq(&pidmap_lock);
		idr_remove(&ns->idr, nr);
		kmem_cache_free(ns->pid_cachep, pid);
		return ERR_PTR(-ENOMEM);
	}
	idr_replace(&ns->idr, pid, nr);
	ns->pid_allocated++;
	spin_unlock_irq(&pidmap_lock);

	return pid;
}

static struct pid *find_pid_ns(int nr, struct pid_namespace *ns)
{
	return idr_find(&ns->idr, nr);
}

static struct pid **task_pid_ptr(struct task_struct *task, enum pid_type type)
{
	return (type == PIDTYPE_PID) ? &task->thread_pid :
				       &task->signal->pids[type];
}

void attach_pid(struct task_struct *task, enum pid_type type)
{
	struct pid *pid = *task_pid_ptr(task, type);
	hlist_add_head_rcu(&task->pid_links[type], &pid->tasks[type]);
}

static struct task_struct *pid_task(struct pid *pid, enum pid_type type)
{
	struct task_struct *result = NULL;
	if (pid) {
		struct hlist_node *first;
		first = rcu_dereference_check(
			hlist_first_rcu(&pid->tasks[type]),
			lockdep_tasklist_lock_is_held());
		if (first)
			result = hlist_entry(first, struct task_struct,
					     pid_links[(type)]);
	}
	return result;
}

struct task_struct *find_task_by_pid_ns(pid_t nr, struct pid_namespace *ns)
{
	return pid_task(find_pid_ns(nr, ns), PIDTYPE_PID);
}

struct pid *get_task_pid(struct task_struct *task, enum pid_type type)
{
	struct pid *pid;
	rcu_read_lock();
	pid = get_pid(rcu_dereference(*task_pid_ptr(task, type)));
	rcu_read_unlock();
	return pid;
}

pid_t pid_nr_ns(struct pid *pid, struct pid_namespace *ns)
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
	return ns_of_pid(tsk->thread_pid); /* task_pid inlined */
}

void __init pid_idr_init(void)
{
	idr_init_base(&init_pid_ns.idr, 0);

	init_pid_ns.pid_cachep =
		KMEM_CACHE(pid, SLAB_HWCACHE_ALIGN | SLAB_PANIC | SLAB_ACCOUNT);
}
