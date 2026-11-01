// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/kernel/fork.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 *  'fork.c' contains the help-routines for the 'fork' system call
 * (see also entry.S and others).
 * Fork is rather simple, once you get the hang of it, but the memory
 * management can be a bitch. See 'mm/memory.c': 'copy_page_range()'
 */

#include <linux/sched/mm.h>
#include <linux/sched/task_stack.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/swap.h>
#include <linux/ptrace.h>
#include <linux/fs_struct.h>

#include <asm/pgalloc.h>
#include <asm/mmu_context.h>

int nr_threads; /* The idle threads do not count.. */

/*
 * MKB_ONE_TASK: upstream sizes this from RAM in set_max_threads(); here the
 * one task this kernel runs (init) is the limit, so every later fork fails
 * with -EAGAIN.
 */
static int max_threads = 1; /* tunable limit on nr_threads */

static inline struct task_struct *alloc_task_struct_node(int node)
{
	return kmalloc_node(sizeof(struct task_struct), GFP_KERNEL, node);
}

static inline void free_task_struct(struct task_struct *tsk)
{
	kfree(tsk);
}

static int alloc_thread_stack_node(struct task_struct *tsk, int node)
{
	struct page *page =
		alloc_pages_node(node, THREADINFO_GFP, THREAD_SIZE_ORDER);

	if (likely(page)) {
		tsk->stack = page_address(page);
		return 0;
	}
	return -ENOMEM;
}

struct vm_area_struct *vm_area_alloc(struct mm_struct *mm)
{
	struct vm_area_struct *vma;

	vma = kmalloc(sizeof(*vma), GFP_KERNEL);
	if (vma)
		vma_init(vma, mm);
	return vma;
}

void vm_area_free(struct vm_area_struct *vma)
{
	kfree(vma);
}

static inline int mm_alloc_pgd(struct mm_struct *mm)
{
	mm->pgd = pgd_alloc(mm);
	if (unlikely(!mm->pgd))
		return -ENOMEM;
	return 0;
}

static inline void mm_free_pgd(struct mm_struct *mm)
{
	pgd_free(mm, mm->pgd);
}

#define allocate_mm() (kmalloc(sizeof(struct mm_struct), GFP_KERNEL))
#define free_mm(mm) (kfree(mm))

/*
 * Called when the last reference to the mm
 * is dropped: either by a lazy thread or by
 * mmput. Free the page directory and the mm.
 */
void __mmdrop(struct mm_struct *mm)
{
	BUG_ON(mm == &init_mm);
	mm_free_pgd(mm);
	free_mm(mm);
}

void set_task_stack_end_magic(struct task_struct *tsk)
{
	unsigned long *stackend;

	stackend = end_of_stack(tsk);
	*stackend = STACK_END_MAGIC; /* for overflow detection */
}

static struct task_struct *dup_task_struct(struct task_struct *orig, int node)
{
	struct task_struct *tsk;
	int err;

	tsk = alloc_task_struct_node(node);
	if (!tsk)
		return NULL;

	err = arch_dup_task_struct(tsk, orig);
	if (err)
		goto free_tsk;

	err = alloc_thread_stack_node(tsk, node);
	if (err)
		goto free_tsk;

	clear_tsk_need_resched(tsk);
	set_task_stack_end_magic(tsk);

	/* One for the rcu users */
	refcount_set(&tsk->usage, 1);

	return tsk;

free_tsk:
	free_task_struct(tsk);
	return NULL;
}

static struct mm_struct *mm_init(struct mm_struct *mm, struct task_struct *p,
				 struct user_namespace *user_ns)
{
	mm->mmap = NULL;
	atomic_set(&mm->mm_users, 1);
	atomic_set(&mm->mm_count, 1);
	mmap_init_lock(mm);
	mm->map_count = 0;
	spin_lock_init(&mm->page_table_lock);

	if (current->mm) {
		mm->flags = current->mm->flags & MMF_INIT_MASK;
		mm->def_flags = current->mm->def_flags & VM_INIT_DEF_MASK;
	} else {
		mm->flags = MMF_DUMP_FILTER_DEFAULT;
		mm->def_flags = 0;
	}

	if (mm_alloc_pgd(mm))
		goto fail_nopgd;

	mm->user_ns = get_user_ns(user_ns);
	return mm;

fail_nopgd:
	free_mm(mm);
	return NULL;
}

/*
 * Allocate and initialize an mm_struct.
 */
struct mm_struct *mm_alloc(void)
{
	struct mm_struct *mm;

	mm = allocate_mm();
	if (!mm)
		return NULL;

	memset(mm, 0, sizeof(*mm));
	return mm_init(mm, current, current_user_ns());
}

static inline void __mmput(struct mm_struct *mm)
{
	exit_mmap(mm);
	mmdrop(mm);
}

/*
 * Decrement the use count and release all resources for an mm.
 */
void mmput(struct mm_struct *mm)
{
	if (atomic_dec_and_test(&mm->mm_users))
		__mmput(mm);
}

/* Please note the differences between mmput and mm_release.
 * mmput is called whenever we stop holding onto a mm_struct,
 * error success whatever.
 *
 * mm_release is called after a mm_struct has been removed
 * from the current process.
 *
 * This difference is important for error handling, when we
 * only half set up a mm_struct for a new process and need to restore
 * the old one.  Because we mmput the new mm_struct before
 * restoring the old one. . .
 * Eric Biederman 10 January 1998
 */
static void mm_release(struct task_struct *tsk, struct mm_struct *mm)
{
	/* Get rid of any cached register state */
	deactivate_mm(tsk, mm);
}

void exec_mm_release(struct task_struct *tsk, struct mm_struct *mm)
{
	mm_release(tsk, mm);
}

static int copy_mm(unsigned long clone_flags, struct task_struct *tsk)
{
	tsk->mm = NULL;
	tsk->active_mm = NULL;

	return 0;
}

static int copy_files(unsigned long clone_flags, struct task_struct *tsk)
{
	struct files_struct *oldf, *newf;
	int error = 0;

	/*
	 * A background process may not have any files ...
	 */
	oldf = current->files;
	if (!oldf)
		goto out;

	newf = dup_fd(oldf, &error);
	if (!newf)
		goto out;

	tsk->files = newf;
	error = 0;
out:
	return error;
}

static int copy_signal(unsigned long clone_flags, struct task_struct *tsk)
{
	struct signal_struct *sig;

	sig = kzalloc(sizeof(*sig), GFP_KERNEL);
	tsk->signal = sig;
	if (!sig)
		return -ENOMEM;

	task_lock(current->group_leader);
	memcpy(sig->rlim, current->signal->rlim, sizeof sig->rlim);
	task_unlock(current->group_leader);

	mutex_init(&sig->cred_guard_mutex);
	init_rwsem(&sig->exec_update_lock);

	return 0;
}

static void rt_mutex_init_task(struct task_struct *p)
{
	raw_spin_lock_init(&p->pi_lock);
}

static inline void init_task_pid(struct task_struct *task, enum pid_type type,
				 struct pid *pid)
{
	if (type == PIDTYPE_PID)
		task->thread_pid = pid;
}

static __latent_entropy struct task_struct *
copy_process(struct pid *pid, int node, struct kernel_clone_args *args)
{
	int retval;
	struct task_struct *p;
	const u64 clone_flags = args->flags;

	retval = -ENOMEM;
	p = dup_task_struct(current, node);
	if (!p)
		goto fork_out;
	p->flags &= ~PF_KTHREAD;

	rt_mutex_init_task(p);

	retval = copy_creds(p, clone_flags);
	if (retval < 0)
		goto bad_fork_free;

	retval = -EAGAIN;
	if (data_race(nr_threads >= max_threads))
		goto bad_fork_cleanup_count;

	p->flags &= ~PF_IDLE;
	spin_lock_init(&p->alloc_lock);

	p->pagefault_disabled = 0;

	/* Perform scheduler related setup. Assign this task to a CPU. */
	retval = sched_fork(clone_flags, p);
	if (retval)
		goto bad_fork_cleanup_policy;

	retval = copy_files(clone_flags, p);
	if (retval)
		goto bad_fork_cleanup_semundo;
	retval = copy_signal(clone_flags, p);
	if (retval)
		goto bad_fork_cleanup_fs;
	retval = copy_mm(clone_flags, p);
	if (retval)
		goto bad_fork_cleanup_signal;
	retval = copy_namespaces(clone_flags, p);
	if (retval)
		goto bad_fork_cleanup_mm;
	retval = copy_thread(p, args);
	if (retval)
		goto bad_fork_cleanup_io;

	pid = alloc_pid(p->nsproxy->pid_ns_for_children);
	if (IS_ERR(pid)) {
		retval = PTR_ERR(pid);
		goto bad_fork_cleanup_io;
	}

	/* ok, now we should be set up.. */
	p->pid = pid_nr(pid);
	p->group_leader = p;

	if (likely(p->pid)) {
		init_task_pid(p, PIDTYPE_PID, pid);
		nr_threads++;
	}

	return p;

bad_fork_cleanup_io:
bad_fork_cleanup_mm:
	if (p->mm) {
		mmput(p->mm);
	}
bad_fork_cleanup_signal:
	kfree(p->signal);
bad_fork_cleanup_fs:
bad_fork_cleanup_semundo:
bad_fork_cleanup_policy:
bad_fork_cleanup_count:
bad_fork_free:
	WRITE_ONCE(p->__state, TASK_DEAD);
fork_out:
	return ERR_PTR(retval);
}

static pid_t kernel_clone(struct kernel_clone_args *args)
{
	struct pid *pid;
	struct task_struct *p;
	pid_t nr;

	p = copy_process(NULL, NUMA_NO_NODE, args);

	if (IS_ERR(p))
		return PTR_ERR(p);

	pid = get_task_pid(p, PIDTYPE_PID);
	nr = pid_vnr(pid);

	wake_up_new_task(p);

	put_pid(pid);
	return nr;
}

/*
 * Create a user mode thread.
 */
pid_t user_mode_thread(int (*fn)(void *), void *arg, unsigned long flags)
{
	struct kernel_clone_args args = {
		.flags = ((lower_32_bits(flags) | CLONE_VM | CLONE_UNTRACED) &
			  ~CSIGNAL),
		.fn = fn,
		.fn_arg = arg,
	};

	return kernel_clone(&args);
}
