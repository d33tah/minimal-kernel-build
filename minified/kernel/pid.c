// SPDX-License-Identifier: GPL-2.0-only
/*
 * Generic pidhash and scalable, time-bounded PID allocator
 *
 * (C) 2002-2003 Nadia Yvette Chambers, IBM
 * (C) 2004 Nadia Yvette Chambers, Oracle
 * (C) 2002-2004 Ingo Molnar, Red Hat
 *
 * pid-structures are backing objects for tasks sharing a given ID to chain
 * against. There is very little to them aside from hashing them and
 * parking tasks using given ID's on a list.
 *
 * The hash is always changed with the tasklist_lock write-acquired,
 * and the hash is only accessed with the tasklist_lock at least
 * read-acquired, so there's no additional SMP locking needed here.
 *
 * We have a list of bitmap pages, which bitmaps represent the PID space.
 * Allocating and freeing PIDs is completely lockless. The worst-case
 * allocation scenario when all but one out of 1 million PIDs possible are
 * allocated already: the scanning of 32 list entries and at most PAGE_SIZE
 * bytes. The typical fastpath is a single successful setbit. Freeing is O(1).
 *
 * Pid namespaces:
 *    (C) 2007 Pavel Emelyanov <xemul@openvz.org>, OpenVZ, SWsoft Inc.
 *    (C) 2007 Sukadev Bhattiprolu <sukadev@us.ibm.com>, IBM
 *     Many thanks to Oleg Nesterov for comments and help
 *
 */

#include <linux/pid_namespace.h>
#include <linux/sched/signal.h>

static struct pid_namespace *task_active_pid_ns(struct task_struct *tsk);

struct pid init_struct_pid = {
	.count = REFCOUNT_INIT(1),
	.level = 0,
	.numbers = { {
		.nr = 0,
		.ns = &init_pid_ns,
	}, },
};

static int pid_max = PID_MAX_DEFAULT;

/*
 * PID-map pages start out as NULL, they get allocated upon
 * first use and are never deallocated. This way a low pid_max
 * value does not cause lots of bitmaps to be allocated, but
 * the scheme scales to up to 4 million PIDs, runtime.
 */
struct pid_namespace init_pid_ns = {
	.level = 0,
};

/*
 * Note: disable interrupts while the pidmap_lock is held as an
 * interrupt might come in and do read_lock(&tasklist_lock).
 *
 * If we don't disable interrupts there is a nasty deadlock between
 * detach_pid()->free_pid() and another cpu that does
 * spin_lock(&pidmap_lock) followed by an interrupt routine that does
 * read_lock(&tasklist_lock);
 *
 * After we clean up the tasklist_lock and know there are no
 * irq handlers that take it we can leave the interrupts enabled.
 * For now it is easier to be safe than to prove it can't happen.
 */

static __cacheline_aligned_in_smp DEFINE_SPINLOCK(pidmap_lock);

void put_pid(struct pid *pid)
{
	if (!pid)
		return;

	if (refcount_dec_and_test(&pid->count)) {
		kfree(pid);
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

	pid = kmalloc(sizeof(*pid), GFP_KERNEL);
	if (!pid)
		return ERR_PTR(-ENOMEM);

	spin_lock_irq(&pidmap_lock);
	nr = ns->last_pid + 1 < pid_max ? ++ns->last_pid : -EAGAIN;
	spin_unlock_irq(&pidmap_lock);
	if (nr < 0) {
		kfree(pid);
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

static struct pid_namespace *task_active_pid_ns(struct task_struct *tsk)
{
	return ns_of_pid(task_pid(tsk));
}

void __init pid_idr_init(void)
{
	pid_max = min_t(int, PID_MAX_LIMIT,
			max_t(int, pid_max,
			      PIDS_PER_CPU_DEFAULT *num_possible_cpus()));
}
