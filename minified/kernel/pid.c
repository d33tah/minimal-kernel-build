
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/rculist.h>
#include <linux/memblock.h>
#include <linux/pid_namespace.h>
#include <linux/init_task.h>
#include <linux/syscalls.h>
#include <linux/proc_ns.h>
#include <linux/refcount.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/ptrace.h>
#include <linux/idr.h>
#include <linux/file.h>

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

#define RESERVED_PIDS 300

int pid_max_min = RESERVED_PIDS + 1;
int pid_max_max = PID_MAX_LIMIT;

struct pid_namespace init_pid_ns = {
	.ns.count = REFCOUNT_INIT(2),
	.idr = IDR_INIT(init_pid_ns.idr),
	.pid_allocated = PIDNS_ADDING,
	.level = 0,
	.child_reaper = &init_task,
	.user_ns = &init_user_ns,
	.ns.inum = PROC_PID_INIT_INO,
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

static void delayed_put_pid(struct rcu_head *rhp)
{
	struct pid *pid = container_of(rhp, struct pid, rcu);
	put_pid(pid);
}

void free_pid(struct pid *pid)
{
	int i;
	unsigned long flags;

	spin_lock_irqsave(&pidmap_lock, flags);
	for (i = 0; i <= pid->level; i++) {
		struct upid *upid = pid->numbers + i;
		struct pid_namespace *ns = upid->ns;
		switch (--ns->pid_allocated) {
		case 2:
		case 1:

			wake_up_process(ns->child_reaper);
			break;
		case PIDNS_ADDING:

			WARN_ON(ns->child_reaper);
			ns->pid_allocated = 0;
			break;
		}

		idr_remove(&ns->idr, upid->nr);
	}
	spin_unlock_irqrestore(&pidmap_lock, flags);

	call_rcu(&pid->rcu, delayed_put_pid);
}

struct pid *alloc_pid(struct pid_namespace *ns, pid_t *set_tid,
		      size_t set_tid_size)
{
	struct pid *pid;
	enum pid_type type;
	int i, nr;
	struct pid_namespace *tmp;
	struct upid *upid;
	int retval = -ENOMEM;

	if (set_tid_size > ns->level + 1)
		return ERR_PTR(-EINVAL);

	pid = kmem_cache_alloc(ns->pid_cachep, GFP_KERNEL);
	if (!pid)
		return ERR_PTR(retval);

	tmp = ns;
	pid->level = ns->level;

	for (i = ns->level; i >= 0; i--) {
		int tid = 0;

		if (set_tid_size) {
			tid = set_tid[ns->level - i];

			retval = -EINVAL;
			if (tid < 1 || tid >= pid_max)
				goto out_free;

			if (tid != 1 && !tmp->child_reaper)
				goto out_free;
			/* checkpoint_restore_ns_capable always true - removed check */
			set_tid_size--;
		}

		idr_preload(GFP_KERNEL);
		spin_lock_irq(&pidmap_lock);

		if (tid) {
			nr = idr_alloc(&tmp->idr, NULL, tid, tid + 1,
				       GFP_ATOMIC);

			if (nr == -ENOSPC)
				nr = -EEXIST;
		} else {
			int pid_min = 1;

			/* idr_get_cursor inlined */
			if (READ_ONCE(tmp->idr.idr_next) > RESERVED_PIDS)
				pid_min = RESERVED_PIDS;

			nr = idr_alloc_cyclic(&tmp->idr, NULL, pid_min, pid_max,
					      GFP_ATOMIC);
		}
		spin_unlock_irq(&pidmap_lock);
		/* idr_preload_end inlined */
		local_unlock(&radix_tree_preloads.lock);

		if (nr < 0) {
			retval = (nr == -ENOSPC) ? -EAGAIN : nr;
			goto out_free;
		}

		pid->numbers[i].nr = nr;
		pid->numbers[i].ns = tmp;
		tmp = tmp->parent;
	}

	retval = -ENOMEM;

	get_pid_ns(ns);
	refcount_set(&pid->count, 1);
	for (type = 0; type < PIDTYPE_MAX; ++type)
		INIT_HLIST_HEAD(&pid->tasks[type]);

	upid = pid->numbers + ns->level;
	spin_lock_irq(&pidmap_lock);
	if (!(ns->pid_allocated & PIDNS_ADDING))
		goto out_unlock;
	for (; upid >= pid->numbers; --upid) {
		idr_replace(&upid->ns->idr, pid, upid->nr);
		upid->ns->pid_allocated++;
	}
	spin_unlock_irq(&pidmap_lock);

	return pid;

out_unlock:
	spin_unlock_irq(&pidmap_lock);

out_free:
	spin_lock_irq(&pidmap_lock);
	while (++i <= ns->level) {
		upid = pid->numbers + i;
		idr_remove(&upid->ns->idr, upid->nr);
	}

	/* idr_set_cursor inlined */
	if (ns->pid_allocated == PIDNS_ADDING)
		WRITE_ONCE(ns->idr.idr_next, 0);

	spin_unlock_irq(&pidmap_lock);

	kmem_cache_free(ns->pid_cachep, pid);
	return ERR_PTR(retval);
}

struct pid *find_pid_ns(int nr, struct pid_namespace *ns)
{
	return idr_find(&ns->idr, nr);
}

/* find_vpid removed - only used by find_get_pid which is also unused */

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

void detach_pid(struct task_struct *task, enum pid_type type)
{
	struct pid **pid_ptr = task_pid_ptr(task, type);
	struct pid *pid;
	int tmp;

	pid = *pid_ptr;

	hlist_del_rcu(&task->pid_links[type]);
	*pid_ptr = NULL;

	for (tmp = PIDTYPE_MAX; --tmp >= 0;)
		if (pid_has_task(pid, tmp))
			return;

	free_pid(pid);
}

void exchange_tids(struct task_struct *left, struct task_struct *right)
{
	struct pid *pid1 = left->thread_pid;
	struct pid *pid2 = right->thread_pid;
	struct hlist_head *head1 = &pid1->tasks[PIDTYPE_PID];
	struct hlist_head *head2 = &pid2->tasks[PIDTYPE_PID];

	hlists_swap_heads_rcu(head1, head2);

	rcu_assign_pointer(left->thread_pid, pid2);
	rcu_assign_pointer(right->thread_pid, pid1);

	WRITE_ONCE(left->pid, pid_nr(pid2));
	WRITE_ONCE(right->pid, pid_nr(pid1));
}

void transfer_pid(struct task_struct *old, struct task_struct *new,
		  enum pid_type type)
{
	if (type == PIDTYPE_PID)
		new->thread_pid = old->thread_pid;
	hlist_replace_rcu(&old->pid_links[type], &new->pid_links[type]);
}

struct task_struct *pid_task(struct pid *pid, enum pid_type type)
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

pid_t __task_pid_nr_ns(struct task_struct *task, enum pid_type type,
		       struct pid_namespace *ns)
{
	pid_t nr = 0;

	rcu_read_lock();
	if (!ns)
		ns = task_active_pid_ns(current);
	nr = pid_nr_ns(rcu_dereference(*task_pid_ptr(task, type)), ns);
	rcu_read_unlock();

	return nr;
}

struct pid_namespace *task_active_pid_ns(struct task_struct *tsk)
{
	return ns_of_pid(task_pid(tsk));
}

/* pidfd_open replaced with COND_SYSCALL */

void __init pid_idr_init(void)
{
	BUILD_BUG_ON(PID_MAX_LIMIT >= PIDNS_ADDING);

	/* num_possible_cpus() == 1 in single-CPU kernel */
	pid_max = min(pid_max_max, max_t(int, pid_max, PIDS_PER_CPU_DEFAULT));
	pid_max_min = max_t(int, pid_max_min, PIDS_PER_CPU_MIN);

	idr_init(&init_pid_ns.idr);

	init_pid_ns.pid_cachep =
		KMEM_CACHE(pid, SLAB_HWCACHE_ALIGN | SLAB_PANIC | SLAB_ACCOUNT);
}

/* pidfd_getfd replaced with COND_SYSCALL */
