
struct file_operations;
struct file *anon_inode_getfile(const char *name,
				const struct file_operations *fops, void *priv,
				int flags);
#include <linux/slab.h>
#include <linux/sched/mm.h>
#include <linux/sched/coredump.h>
#include <linux/sched/user.h>
/* Removed: task_numa_free, rt_mutex_debug_task_free, copy_semundo, exit_sem, shm_init_task
 * - All were no-op stubs (~25 LOC) */
#include <linux/sched/task.h>
#include <linux/sched/task_stack.h>
#include <linux/sched/cputime.h>
/* seq_file.h removed - header is empty */
#include <linux/init.h>
#include <linux/unistd.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/completion.h>
#include <linux/personality.h>
#include <linux/mempolicy.h>
#include <linux/file.h>
#include <linux/fdtable.h>
/* key.h removed - unused */
#include <linux/binfmts.h>
#include <linux/mman.h>
#include <linux/mmu_notifier.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/mm_inline.h>
#include <linux/vmacache.h>
#include <linux/nsproxy.h>
#include <linux/capability.h>
#include <linux/cpu.h>
#include <linux/cgroup.h>
#include <linux/security.h>
#include <linux/hugetlb.h>
/* seccomp.h removed - header is empty */
#include <linux/swap.h>
#include <linux/syscalls.h>
#include <linux/futex.h>
/* compat.h removed - unused */
#include <linux/kthread.h>
/* task_io_accounting_init removed - empty stub */
#include <linux/rcupdate.h>
#include <linux/ptrace.h>
#include <linux/mount.h>
/* audit.h removed - unused */
#include <linux/proc_fs.h>
#include <linux/rmap.h>
/* ksm_exit, acct_*, proc_fork_connector, delayacct_*, taskstats_tgid_free removed - empty stubs */
#include <linux/userfaultfd_k.h>
/* freezer.h removed - unused */
#include <linux/tty.h>
#include <linux/fs_struct.h>
#include <linux/magic.h>
/* perf_event.h, posix-timers.h removed - unused */
/* user_return_notifier stubs, khugepaged_exit, scs_* removed - empty stubs */
/* uprobes.h removed - unused */
#include <linux/compiler.h>
#include <linux/init_task.h>
#include <linux/thread_info.h>
/* io_uring.h removed - unused */

#include <asm/pgalloc.h>
#include <linux/uaccess.h>
#include <asm/mmu_context.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>

#define MIN_THREADS 20

#define MAX_THREADS FUTEX_TID_MASK

/* total_forks removed - only incremented, never read */
int nr_threads;

static int max_threads;

/* process_counts removed - only incremented/decremented, never read */

__cacheline_aligned DEFINE_RWLOCK(tasklist_lock);

/* arch_release_task_struct removed - empty weak stub, no x86 override */

#ifndef CONFIG_ARCH_TASK_STRUCT_ALLOCATOR
static struct kmem_cache *task_struct_cachep;

static inline void free_task_struct(struct task_struct *tsk)
{
	kmem_cache_free(task_struct_cachep, tsk);
}
#endif

#ifndef CONFIG_ARCH_THREAD_STACK_ALLOCATOR

#if THREAD_SIZE >= PAGE_SIZE || defined(CONFIG_VMAP_STACK)

static void thread_stack_free_rcu(struct rcu_head *rh)
{
	__free_pages(virt_to_page(rh), THREAD_SIZE_ORDER);
}

/* thread_stack_delayed_free inlined into free_thread_stack */

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

static void free_thread_stack(struct task_struct *tsk)
{
	struct rcu_head *rh = tsk->stack;
	call_rcu(rh, thread_stack_free_rcu);
	tsk->stack = NULL;
}

/* #else block removed - dead code since THREAD_SIZE >= PAGE_SIZE on x86 */
/* thread_stack_cache_init removed - never called */

#endif
/* CONFIG_ARCH_THREAD_STACK_ALLOCATOR #else block removed - not set on x86 */
#endif

static struct kmem_cache *signal_cachep;

struct kmem_cache *sighand_cachep;

struct kmem_cache *files_cachep;

struct kmem_cache *fs_cachep;

static struct kmem_cache *vm_area_cachep;

static struct kmem_cache *mm_cachep;

struct vm_area_struct *vm_area_alloc(struct mm_struct *mm)
{
	struct vm_area_struct *vma;

	vma = kmem_cache_alloc(vm_area_cachep, GFP_KERNEL);
	if (vma)
		vma_init(vma, mm);
	return vma;
}

struct vm_area_struct *vm_area_dup(struct vm_area_struct *orig)
{
	struct vm_area_struct *new =
		kmem_cache_alloc(vm_area_cachep, GFP_KERNEL);

	if (new) {
		*new = data_race(*orig);
		INIT_LIST_HEAD(&new->anon_vma_chain);
		new->vm_next = new->vm_prev = NULL;
		/* dup_anon_vma_name is empty stub - removed */
	}
	return new;
}

void vm_area_free(struct vm_area_struct *vma)
{
	/* free_anon_vma_name removed - empty stub */
	kmem_cache_free(vm_area_cachep, vma);
}

static void account_kernel_stack(struct task_struct *tsk, int account)
{
	void *stack = task_stack_page(tsk);

	/* !VMAP_STACK - simplified path */
	mod_lruvec_kmem_state(stack, NR_KERNEL_STACK_KB,
			      account * (THREAD_SIZE / 1024));
}

void exit_task_stack_account(struct task_struct *tsk)
{
	account_kernel_stack(tsk, -1);
}

void put_task_stack(struct task_struct *tsk)
{
	if (refcount_dec_and_test(&tsk->stack_refcount)) {
		if (WARN_ON(READ_ONCE(tsk->__state) != TASK_DEAD))
			return;
		free_thread_stack(tsk);
	}
}

void free_task(struct task_struct *tsk)
{
	WARN_ON_ONCE(refcount_read(&tsk->stack_refcount) != 0);
	if (tsk->flags & PF_KTHREAD)
		free_kthread_struct(tsk);
	free_task_struct(tsk);
}

static __latent_entropy int dup_mmap(struct mm_struct *mm,
				     struct mm_struct *oldmm)
{
	/* Minimal stub: simplified VMA duplication for fork */
	struct vm_area_struct *mpnt, *tmp, *prev, **pprev;
	struct rb_node **rb_link, *rb_parent;
	int retval = 0;

	if (mmap_write_lock_killable(oldmm))
		return -EINTR;

	mmap_write_lock_nested(mm, SINGLE_DEPTH_NESTING);

	/* get_mm_exe_file returns NULL, no exe_file handling needed */
	RCU_INIT_POINTER(mm->exe_file, NULL);
	mm->total_vm = oldmm->total_vm;

	rb_link = &mm->mm_rb.rb_node;
	rb_parent = NULL;
	pprev = &mm->mmap;
	prev = NULL;

	for (mpnt = oldmm->mmap; mpnt; mpnt = mpnt->vm_next) {
		if (mpnt->vm_flags & VM_DONTCOPY)
			continue;

		tmp = vm_area_dup(mpnt);
		if (!tmp) {
			retval = -ENOMEM;
			break;
		}

		tmp->vm_mm = mm;
		tmp->vm_flags &= ~(VM_LOCKED | VM_LOCKONFAULT);

		if (tmp->vm_file)
			get_file(tmp->vm_file);

		*pprev = tmp;
		pprev = &tmp->vm_next;
		tmp->vm_prev = prev;
		prev = tmp;

		__vma_link_rb(mm, tmp, rb_link, rb_parent);
		rb_link = &tmp->vm_rb.rb_right;
		rb_parent = &tmp->vm_rb;
		mm->map_count++;
	}

	mmap_write_unlock(mm);
	mmap_write_unlock(oldmm);
	return retval;
}

#define allocate_mm() (kmem_cache_alloc(mm_cachep, GFP_KERNEL))
#define free_mm(mm) (kmem_cache_free(mm_cachep, (mm)))

void __mmdrop(struct mm_struct *mm)
{
	BUG_ON(mm == &init_mm);
	WARN_ON_ONCE(mm == current->mm);
	WARN_ON_ONCE(mm == current->active_mm);
	pgd_free(mm, mm->pgd);
	destroy_context(mm);
	mmu_notifier_subscriptions_destroy(mm);
	put_user_ns(mm->user_ns);
	free_mm(mm);
}

/* mmdrop_async_fn removed - async_put_work is never scheduled */

static inline void free_signal_struct(struct signal_struct *sig)
{
	/* taskstats_tgid_free, sched_autogroup_exit, oom_mm cleanup - removed */
	kmem_cache_free(signal_cachep, sig);
}

void __put_task_struct(struct task_struct *tsk)
{
	WARN_ON(!tsk->exit_state);
	WARN_ON(refcount_read(&tsk->usage));
	WARN_ON(tsk == current);

	/* io_uring_free, security_task_free - empty stubs */
	exit_creds(tsk);
	/* delayacct_tsk_free, sched_core_free removed - empty stubs */
	if (refcount_dec_and_test(&tsk->signal->sigcnt))
		free_signal_struct(tsk->signal);
	free_task(tsk);
}

int arch_task_struct_size __read_mostly;

void __init fork_init(void)
{
	int i;
	u64 threads;
	unsigned long nr_pages = totalram_pages();
#ifndef CONFIG_ARCH_TASK_STRUCT_ALLOCATOR
#ifndef ARCH_MIN_TASKALIGN
#define ARCH_MIN_TASKALIGN 0
#endif
	int align = max_t(int, L1_CACHE_BYTES, ARCH_MIN_TASKALIGN);
	unsigned long useroffset, usersize;

	/* Inlined task_struct_whitelist */
	arch_thread_struct_whitelist(&useroffset, &usersize);
	if (unlikely(usersize == 0))
		useroffset = 0;
	else
		useroffset += offsetof(struct task_struct, thread);
	task_struct_cachep = kmem_cache_create_usercopy(
		"task_struct", arch_task_struct_size, align,
		SLAB_PANIC | SLAB_ACCOUNT, useroffset, usersize, NULL);
#endif

	/* Inlined set_max_threads */
	if (fls64(nr_pages) + fls64(PAGE_SIZE) > 64)
		threads = MAX_THREADS;
	else
		threads = div64_u64((u64)nr_pages * (u64)PAGE_SIZE,
				    (u64)THREAD_SIZE * 8UL);
	if (threads > MAX_THREADS)
		threads = MAX_THREADS;
	max_threads = clamp_t(u64, threads, MIN_THREADS, MAX_THREADS);

	init_task.signal->rlim[RLIMIT_NPROC].rlim_cur = max_threads / 2;
	init_task.signal->rlim[RLIMIT_NPROC].rlim_max = max_threads / 2;
	init_task.signal->rlim[RLIMIT_SIGPENDING] =
		init_task.signal->rlim[RLIMIT_NPROC];

	for (i = 0; i < MAX_PER_NAMESPACE_UCOUNTS; i++)
		init_user_ns.ucount_max[i] = max_threads / 2;

	set_rlimit_ucount_max(&init_user_ns, UCOUNT_RLIMIT_NPROC,
			      RLIM_INFINITY);
	set_rlimit_ucount_max(&init_user_ns, UCOUNT_RLIMIT_MSGQUEUE,
			      RLIM_INFINITY);
	set_rlimit_ucount_max(&init_user_ns, UCOUNT_RLIMIT_SIGPENDING,
			      RLIM_INFINITY);
	set_rlimit_ucount_max(&init_user_ns, UCOUNT_RLIMIT_MEMLOCK,
			      RLIM_INFINITY);
	/* lockdep_init_task removed - empty stub */
}

/* arch_dup_task_struct provided by arch/x86/kernel/process.c */

void set_task_stack_end_magic(struct task_struct *tsk)
{
	unsigned long *stackend;

	stackend = end_of_stack(tsk);
	*stackend = STACK_END_MAGIC;
}

static struct task_struct *dup_task_struct(struct task_struct *orig, int node)
{
	struct task_struct *tsk;
	int err;

	/* tsk_fork_get_node call removed - always returns NUMA_NO_NODE, so this is a no-op */
	tsk = kmem_cache_alloc_node(task_struct_cachep, GFP_KERNEL, node);
	if (!tsk)
		return NULL;

	err = arch_dup_task_struct(tsk, orig);
	if (err)
		goto free_tsk;

	err = alloc_thread_stack_node(tsk, node);
	if (err)
		goto free_tsk;

	refcount_set(&tsk->stack_refcount, 1);
	account_kernel_stack(tsk, 1);
	/* scs_prepare, clear_user_return_notifier, setup_thread_stack removed - empty stubs */
	clear_tsk_need_resched(tsk);
	set_task_stack_end_magic(tsk);
	clear_syscall_work_syscall_user_dispatch(tsk);

	if (orig->cpus_ptr == &orig->cpus_mask)
		tsk->cpus_ptr = &tsk->cpus_mask;

	refcount_set(&tsk->rcu_users, 2);

	refcount_set(&tsk->usage, 1);
	/* splice_pipe, task_frag removed - never used */
	tsk->wake_q.next = NULL;
	tsk->worker_private = NULL;

	kmap_local_fork(tsk);

	return tsk;

free_tsk:
	free_task_struct(tsk);
	return NULL;
}

__cacheline_aligned_in_smp DEFINE_SPINLOCK(mmlist_lock);

static unsigned long default_dump_filter = MMF_DUMP_FILTER_DEFAULT;

/* duplicate init_task.h include removed - already included at line 67 */

/* mm_init_aio, mm_clear_owner, mm_init_owner, mm_init_uprobes_state removed - empty stubs */

static struct mm_struct *mm_init(struct mm_struct *mm, struct task_struct *p,
				 struct user_namespace *user_ns)
{
	mm->mmap = NULL;
	mm->mm_rb = RB_ROOT;
	mm->vmacache_seqnum = 0;
	atomic_set(&mm->mm_users, 1);
	atomic_set(&mm->mm_count, 1);
	/* write_protect_seq init removed - field unused */
	mmap_init_lock(mm);
	INIT_LIST_HEAD(&mm->mmlist);
	mm_pgtables_bytes_init(mm);
	mm->map_count = 0;
	memset(&mm->rss_stat, 0, sizeof(mm->rss_stat));
	spin_lock_init(&mm->page_table_lock);
	spin_lock_init(&mm->arg_lock);
	mm_init_cpumask(mm);
	RCU_INIT_POINTER(mm->exe_file, NULL);
	mmu_notifier_subscriptions_init(mm);
	init_tlb_flush_pending(mm);
	hugetlb_count_init(mm);

	if (current->mm) {
		mm->flags = current->mm->flags & MMF_INIT_MASK;
		mm->def_flags = current->mm->def_flags & VM_INIT_DEF_MASK;
	} else {
		mm->flags = default_dump_filter;
		mm->def_flags = 0;
	}

	mm->pgd = pgd_alloc(mm);
	if (unlikely(!mm->pgd))
		goto fail_nopgd;

	if (init_new_context(p, mm))
		goto fail_nocontext;

	mm->user_ns = get_user_ns(user_ns);
	return mm;

fail_nocontext:
	pgd_free(mm, mm->pgd);
fail_nopgd:
	free_mm(mm);
	return NULL;
}

struct mm_struct *mm_alloc(void)
{
	struct mm_struct *mm;

	mm = allocate_mm();
	if (!mm)
		return NULL;

	memset(mm, 0, sizeof(*mm));
	return mm_init(mm, current, current_user_ns());
}

void mmput(struct mm_struct *mm)
{
	might_sleep();

	if (atomic_dec_and_test(&mm->mm_users)) {
		VM_BUG_ON(atomic_read(&mm->mm_users));

		exit_mmap(mm);
		set_mm_exe_file(mm, NULL);
		if (!list_empty(&mm->mmlist)) {
			spin_lock(&mmlist_lock);
			list_del(&mm->mmlist);
			spin_unlock(&mmlist_lock);
		}
		if (mm->binfmt)
			module_put(mm->binfmt->module);
		mmdrop(mm);
	}
}

int set_mm_exe_file(struct mm_struct *mm, struct file *new_exe_file)
{
	struct file *old_exe_file;

	old_exe_file = rcu_dereference_raw(mm->exe_file);

	if (new_exe_file) {
		if (unlikely(deny_write_access(new_exe_file)))
			return -EACCES;
		get_file(new_exe_file);
	}
	rcu_assign_pointer(mm->exe_file, new_exe_file);
	if (old_exe_file) {
		allow_write_access(old_exe_file);
		fput(old_exe_file);
	}
	return 0;
}

/* get_mm_exe_file removed - no callers after dup_mmap simplification */

static void mm_release(struct task_struct *tsk, struct mm_struct *mm)
{
	deactivate_mm(tsk, mm);

	if (tsk->clear_child_tid) {
		if (atomic_read(&mm->mm_users) > 1) {
			put_user(0, tsk->clear_child_tid);
			do_futex(tsk->clear_child_tid, FUTEX_WAKE, 1, NULL,
				 NULL, 0, 0);
		}
		tsk->clear_child_tid = NULL;
	}

	if (tsk->vfork_done) {
		struct completion *vfork;

		task_lock(tsk);
		vfork = tsk->vfork_done;
		if (likely(vfork)) {
			tsk->vfork_done = NULL;
			complete(vfork);
		}
		task_unlock(tsk);
	}
}

void exit_mm_release(struct task_struct *tsk, struct mm_struct *mm)
{
	mm_release(tsk, mm);
}

void exec_mm_release(struct task_struct *tsk, struct mm_struct *mm)
{
	mm_release(tsk, mm);
}

static struct mm_struct *dup_mm(struct task_struct *tsk,
				struct mm_struct *oldmm)
{
	struct mm_struct *mm;
	int err;

	mm = allocate_mm();
	if (!mm)
		goto fail_nomem;

	memcpy(mm, oldmm, sizeof(*mm));

	if (!mm_init(mm, tsk, mm->user_ns))
		goto fail_nomem;

	err = dup_mmap(mm, oldmm);
	if (err)
		goto free_pt;
	/* hiwater_rss, hiwater_vm init removed - fields removed */

	/* try_module_get always returns true - dead check removed */

	return mm;

free_pt:

	mm->binfmt = NULL;
	mmput(mm);

fail_nomem:
	return NULL;
}

/* copy_mm, copy_fs, copy_files and copy_sighand inlined into copy_process */

void __cleanup_sighand(struct sighand_struct *sighand)
{
	if (refcount_dec_and_test(&sighand->count)) {
		kmem_cache_free(sighand_cachep, sighand);
	}
}

/* posix_cpu_timers_init_group removed - was empty stub */

/* copy_signal inlined into copy_process */

/* Stub: set_tid_address not needed for Hello World */
SYSCALL_DEFINE1(set_tid_address, int __user *, tidptr)
{
	return 1;
}

/* init_task_pid_links inlined into copy_process */

static inline void init_task_pid(struct task_struct *task, enum pid_type type,
				 struct pid *pid)
{
	if (type == PIDTYPE_PID)
		task->thread_pid = pid;
	else
		task->signal->pids[type] = pid;
}

/* pidfd_pid removed - never called */

static int pidfd_release(struct inode *inode, struct file *file)
{
	struct pid *pid = file->private_data;

	file->private_data = NULL;
	put_pid(pid);
	return 0;
}

/* pidfd_poll removed - poll/select syscalls return ENOSYS */

const struct file_operations pidfd_fops = {
	.release = pidfd_release,
	/* poll removed - poll/select syscalls return ENOSYS */
};

/* copy_oom_score_adj removed - empty stub */

static __latent_entropy struct task_struct *
copy_process(struct pid *pid, int trace, int node,
	     struct kernel_clone_args *args)
{
	int pidfd = -1, retval;
	struct task_struct *p;
	struct multiprocess_signals delayed;
	struct file *pidfile = NULL;
	const u64 clone_flags = args->flags;
	struct nsproxy *nsp = current->nsproxy;

	if ((clone_flags & (CLONE_NEWNS | CLONE_FS)) ==
	    (CLONE_NEWNS | CLONE_FS))
		return ERR_PTR(-EINVAL);

	if ((clone_flags & (CLONE_NEWUSER | CLONE_FS)) ==
	    (CLONE_NEWUSER | CLONE_FS))
		return ERR_PTR(-EINVAL);

	if ((clone_flags & CLONE_THREAD) && !(clone_flags & CLONE_SIGHAND))
		return ERR_PTR(-EINVAL);

	if ((clone_flags & CLONE_SIGHAND) && !(clone_flags & CLONE_VM))
		return ERR_PTR(-EINVAL);

	if ((clone_flags & CLONE_PARENT) &&
	    current->signal->flags & SIGNAL_UNKILLABLE)
		return ERR_PTR(-EINVAL);

	if (clone_flags & CLONE_THREAD) {
		if ((clone_flags & (CLONE_NEWUSER | CLONE_NEWPID)) ||
		    (task_active_pid_ns(current) != nsp->pid_ns_for_children))
			return ERR_PTR(-EINVAL);
	}

	if (clone_flags & (CLONE_THREAD | CLONE_VM)) {
		if (nsp->time_ns != nsp->time_ns_for_children)
			return ERR_PTR(-EINVAL);
	}

	if (clone_flags & CLONE_PIDFD) {
		if (clone_flags & (CLONE_DETACHED | CLONE_THREAD))
			return ERR_PTR(-EINVAL);
	}

	sigemptyset(&delayed.signal);
	INIT_HLIST_NODE(&delayed.node);

	spin_lock_irq(&current->sighand->siglock);
	if (!(clone_flags & CLONE_THREAD))
		hlist_add_head(&delayed.node, &current->signal->multiprocess);
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);
	retval = -ERESTARTNOINTR;
	if (task_sigpending(current))
		goto fork_out;

	retval = -ENOMEM;
	p = dup_task_struct(current, node);
	if (!p)
		goto fork_out;
	p->flags &= ~PF_KTHREAD;
	if (args->kthread)
		p->flags |= PF_KTHREAD;
	if (args->io_thread) {
		p->flags |= PF_IO_WORKER;
		siginitsetinv(&p->blocked, sigmask(SIGKILL) | sigmask(SIGSTOP));
	}

	p->set_child_tid =
		(clone_flags & CLONE_CHILD_SETTID) ? args->child_tid : NULL;

	p->clear_child_tid =
		(clone_flags & CLONE_CHILD_CLEARTID) ? args->child_tid : NULL;

	raw_spin_lock_init(&p->pi_lock);

	retval = copy_creds(p, clone_flags);
	if (retval < 0)
		goto bad_fork_free;

	/* PF_NPROC_EXCEEDED clearing removed - flag never tested */

	retval = -EAGAIN;
	if (data_race(nr_threads >= max_threads))
		goto bad_fork_cleanup_count;
	/* delayacct_tsk_init removed - empty stub */
	p->flags &=
		~(PF_SUPERPRIV | PF_WQ_WORKER | PF_IDLE | PF_NO_SETAFFINITY);
	p->flags |= PF_FORKNOEXEC;
	INIT_LIST_HEAD(&p->children);
	INIT_LIST_HEAD(&p->sibling);
	p->vfork_done = NULL;
	spin_lock_init(&p->alloc_lock);

	init_sigpending(&p->pending);
	/* utime, stime init removed - fields removed */

	/* prev_cputime_init, task_io_accounting_init, acct_clear_integrals, posix_cputimers_init, audit_set_context removed - empty stubs */
	if (args->kthread) {
		if (!set_kthread_struct(p))
			goto bad_fork_cleanup_count;
	}

	p->pagefault_disabled = 0;

	retval = sched_fork(clone_flags, p);
	if (retval)
		goto bad_fork_cleanup_count;
	/* perf_event_init_task removed - empty stub that always returns 0 */
	/* security_task_alloc always returns 0 - dead code removed */
	/* Inlined copy_files */
	{
		struct files_struct *oldf = current->files;
		if (oldf) {
			if (clone_flags & CLONE_FILES) {
				atomic_inc(&oldf->count);
			} else {
				struct files_struct *newf =
					dup_fd(oldf, NR_OPEN_MAX, &retval);
				if (!newf)
					goto bad_fork_cleanup_count;
				p->files = newf;
			}
		}
	}
	/* Inlined copy_fs */
	{
		struct fs_struct *fs = current->fs;
		if (clone_flags & CLONE_FS) {
			spin_lock(&fs->lock);
			if (fs->in_exec) {
				spin_unlock(&fs->lock);
				retval = -EAGAIN;
				goto bad_fork_cleanup_files;
			}
			fs->users++;
			spin_unlock(&fs->lock);
		} else {
			p->fs = copy_fs_struct(fs);
			if (!p->fs) {
				retval = -ENOMEM;
				goto bad_fork_cleanup_files;
			}
		}
	}
	/* Inlined copy_sighand */
	if (clone_flags & CLONE_SIGHAND) {
		refcount_inc(&current->sighand->count);
	} else {
		struct sighand_struct *sig;
		sig = kmem_cache_alloc(sighand_cachep, GFP_KERNEL);
		RCU_INIT_POINTER(p->sighand, sig);
		if (!sig) {
			retval = -ENOMEM;
			goto bad_fork_cleanup_fs;
		}
		refcount_set(&sig->count, 1);
		spin_lock_irq(&current->sighand->siglock);
		memcpy(sig->action, current->sighand->action,
		       sizeof(sig->action));
		spin_unlock_irq(&current->sighand->siglock);
		if (clone_flags & CLONE_CLEAR_SIGHAND)
			flush_signal_handlers(p, 0);
	}
	/* Inlined copy_signal */
	if (!(clone_flags & CLONE_THREAD)) {
		struct signal_struct *sig;
		sig = kmem_cache_zalloc(signal_cachep, GFP_KERNEL);
		p->signal = sig;
		if (!sig) {
			retval = -ENOMEM;
			goto bad_fork_cleanup_sighand;
		}
		sig->nr_threads = 1;
		atomic_set(&sig->live, 1);
		refcount_set(&sig->sigcnt, 1);
		sig->thread_head =
			(struct list_head)LIST_HEAD_INIT(p->thread_node);
		p->thread_node =
			(struct list_head)LIST_HEAD_INIT(sig->thread_head);
		sig->curr_target = p;
		init_sigpending(&sig->shared_pending);
		INIT_HLIST_HEAD(&sig->multiprocess);
		task_lock(current->group_leader);
		memcpy(sig->rlim, current->signal->rlim, sizeof sig->rlim);
		task_unlock(current->group_leader);
		mutex_init(&sig->cred_guard_mutex);
		init_rwsem(&sig->exec_update_lock);
	}
	/* Inlined copy_mm */
	{
		struct mm_struct *mm, *oldmm;
		/* p->min_flt/maj_flt/nvcsw/nivcsw init removed - write-only fields */
		p->mm = NULL;
		p->active_mm = NULL;
		oldmm = current->mm;
		if (oldmm) {
			vmacache_flush(p);
			if (clone_flags & CLONE_VM) {
				mmget(oldmm);
				mm = oldmm;
			} else {
				mm = dup_mm(p, current->mm);
				if (!mm) {
					retval = -ENOMEM;
					goto bad_fork_cleanup_signal;
				}
			}
			p->mm = mm;
			p->active_mm = mm;
		}
	}
	retval = copy_namespaces(clone_flags, p);
	if (retval)
		goto bad_fork_cleanup_mm;
	retval = copy_thread(p, args);
	if (retval)
		goto bad_fork_cleanup_namespaces;

	if (pid != &init_struct_pid) {
		pid = alloc_pid(p->nsproxy->pid_ns_for_children, args->set_tid,
				args->set_tid_size);
		if (IS_ERR(pid)) {
			retval = PTR_ERR(pid);
			goto bad_fork_cleanup_thread;
		}
	}

	if (clone_flags & CLONE_PIDFD) {
		retval = get_unused_fd_flags(O_RDWR | O_CLOEXEC);
		if (retval < 0)
			goto bad_fork_free_pid;

		pidfd = retval;

		pidfile = anon_inode_getfile("[pidfd]", &pidfd_fops, pid,
					     O_RDWR | O_CLOEXEC);
		if (IS_ERR(pidfile)) {
			put_unused_fd(pidfd);
			retval = PTR_ERR(pidfile);
			goto bad_fork_free_pid;
		}
		get_pid(pid);

		retval = put_user(pidfd, args->pidfd);
		if (retval)
			goto bad_fork_put_pidfd;
	}

	if ((clone_flags & (CLONE_VM | CLONE_VFORK)) == CLONE_VM)
		sas_ss_reset(p);

	clear_task_syscall_work(p, SYSCALL_TRACE);
	clear_task_syscall_work(p, SYSCALL_EMU);

	p->pid = pid_nr(pid);
	if (clone_flags & CLONE_THREAD) {
		p->group_leader = current->group_leader;
		p->tgid = current->tgid;
	} else {
		p->group_leader = p;
		p->tgid = p->pid;
	}

	/* p->nr_dirtied removed - write-only */
	p->pdeath_signal = 0;
	INIT_LIST_HEAD(&p->thread_group);
	p->task_works = NULL;

	sched_cgroup_fork(p, args);
	/* start_time removed - write-only field */

	write_lock_irq(&tasklist_lock);

	if (clone_flags & (CLONE_PARENT | CLONE_THREAD)) {
		p->real_parent = current->real_parent;
		/* parent_exec_id removed - write-only field */
		if (clone_flags & CLONE_THREAD)
			p->exit_signal = -1;
		else
			p->exit_signal = current->group_leader->exit_signal;
	} else {
		p->real_parent = current;
		/* parent_exec_id removed - write-only field */
		p->exit_signal = args->exit_signal;
	}

	spin_lock(&current->sighand->siglock);

	if (unlikely(!(ns_of_pid(pid)->pid_allocated & PIDNS_ADDING))) {
		retval = -ENOMEM;
		goto bad_fork_cancel_cgroup;
	}

	if (fatal_signal_pending(current)) {
		retval = -EINTR;
		goto bad_fork_cancel_cgroup;
	}

	{
		enum pid_type type;
		for (type = PIDTYPE_PID; type < PIDTYPE_MAX; ++type)
			INIT_HLIST_NODE(&p->pid_links[type]);
	}
	if (likely(p->pid)) {
		ptrace_init_task(p, (clone_flags & CLONE_PTRACE) || trace);

		init_task_pid(p, PIDTYPE_PID, pid);
		if (thread_group_leader(p)) {
			init_task_pid(p, PIDTYPE_TGID, pid);
			init_task_pid(p, PIDTYPE_PGID, task_pgrp(current));
			init_task_pid(p, PIDTYPE_SID, task_session(current));

			if (is_child_reaper(pid)) {
				ns_of_pid(pid)->child_reaper = p;
				p->signal->flags |= SIGNAL_UNKILLABLE;
			}
			p->signal->shared_pending.signal = delayed.signal;
			p->signal->tty = tty_kref_get(current->signal->tty);

			p->signal->has_child_subreaper =
				p->real_parent->signal->has_child_subreaper ||
				p->real_parent->signal->is_child_subreaper;
			list_add_tail(&p->sibling, &p->real_parent->children);
			list_add_tail_rcu(&p->tasks, &init_task.tasks);
			attach_pid(p, PIDTYPE_TGID);
			attach_pid(p, PIDTYPE_PGID);
			attach_pid(p, PIDTYPE_SID);
			/* process_counts increment removed */
		} else {
			current->signal->nr_threads++;
			atomic_inc(&current->signal->live);
			refcount_inc(&current->signal->sigcnt);
			/* task_join_group_stop removed - empty stub */
			list_add_tail_rcu(&p->thread_group,
					  &p->group_leader->thread_group);
			list_add_tail_rcu(&p->thread_node,
					  &p->signal->thread_head);
		}
		attach_pid(p, PIDTYPE_PID);
		nr_threads++;
	}
	/* total_forks++ removed */
	hlist_del_init(&delayed.node);
	spin_unlock(&current->sighand->siglock);
	write_unlock_irq(&tasklist_lock);

	if (pidfile)
		fd_install(pidfd, pidfile);
	sched_post_fork(p);

	return p;

bad_fork_cancel_cgroup:
	spin_unlock(&current->sighand->siglock);
	write_unlock_irq(&tasklist_lock);
bad_fork_put_pidfd:
	if (clone_flags & CLONE_PIDFD) {
		fput(pidfile);
		put_unused_fd(pidfd);
	}
bad_fork_free_pid:
	if (pid != &init_struct_pid)
		free_pid(pid);
bad_fork_cleanup_thread:
	exit_thread(p);
bad_fork_cleanup_namespaces:
	exit_task_namespaces(p);
bad_fork_cleanup_mm:
	if (p->mm)
		mmput(p->mm);
bad_fork_cleanup_signal:
	if (!(clone_flags & CLONE_THREAD))
		free_signal_struct(p->signal);
bad_fork_cleanup_sighand:
	__cleanup_sighand(p->sighand);
bad_fork_cleanup_fs:
	exit_fs(p);
bad_fork_cleanup_files:
	exit_files(p);
	/* bad_fork_cleanup_security, bad_fork_cleanup_policy, bad_fork_cleanup_delayacct
	 * labels consolidated here - they had no cleanup code */
bad_fork_cleanup_count:
	dec_rlimit_ucounts(task_ucounts(p), UCOUNT_RLIMIT_NPROC, 1);
	exit_creds(p);
bad_fork_free:
	WRITE_ONCE(p->__state, TASK_DEAD);
	exit_task_stack_account(p);
	put_task_stack(p);
	free_task(p);
fork_out:
	spin_lock_irq(&current->sighand->siglock);
	hlist_del_init(&delayed.node);
	spin_unlock_irq(&current->sighand->siglock);
	return ERR_PTR(retval);
}

struct mm_struct *copy_init_mm(void)
{
	return dup_mm(NULL, &init_mm);
}

pid_t kernel_clone(struct kernel_clone_args *args)
{
	u64 clone_flags = args->flags;
	struct completion vfork;
	struct pid *pid;
	struct task_struct *p;
	int trace = 0;
	pid_t nr;

	if ((args->flags & CLONE_PIDFD) &&
	    (args->flags & CLONE_PARENT_SETTID) &&
	    (args->pidfd == args->parent_tid))
		return -EINVAL;

	if (!(clone_flags & CLONE_UNTRACED)) {
		if (clone_flags & CLONE_VFORK)
			trace = PTRACE_EVENT_VFORK;
		else if (args->exit_signal != SIGCHLD)
			trace = PTRACE_EVENT_CLONE;
		else
			trace = PTRACE_EVENT_FORK;

		if (likely(!ptrace_event_enabled(current, trace)))
			trace = 0;
	}

	p = copy_process(NULL, trace, NUMA_NO_NODE, args);

	if (IS_ERR(p))
		return PTR_ERR(p);

	pid = get_task_pid(p, PIDTYPE_PID);
	nr = pid_vnr(pid);

	if (clone_flags & CLONE_PARENT_SETTID)
		put_user(nr, args->parent_tid);

	if (clone_flags & CLONE_VFORK) {
		p->vfork_done = &vfork;
		init_completion(&vfork);
		get_task_struct(p);
	}

	wake_up_new_task(p);

	if (unlikely(trace))
		ptrace_event_pid(trace, pid);

	if (clone_flags & CLONE_VFORK) {
		/* Inlined wait_for_vfork_done */
		int killed = wait_for_completion_killable(&vfork);
		if (killed) {
			task_lock(p);
			p->vfork_done = NULL;
			task_unlock(p);
		}
		put_task_struct(p);
		if (!killed)
			ptrace_event_pid(PTRACE_EVENT_VFORK_DONE, pid);
	}

	put_pid(pid);
	return nr;
}

pid_t kernel_thread(int (*fn)(void *), void *arg, unsigned long flags)
{
	struct kernel_clone_args args = {
		.flags = ((lower_32_bits(flags) | CLONE_VM | CLONE_UNTRACED) &
			  ~CSIGNAL),
		.exit_signal = (lower_32_bits(flags) & CSIGNAL),
		.fn = fn,
		.fn_arg = arg,
		.kthread = 1,
	};

	return kernel_clone(&args);
}

pid_t user_mode_thread(int (*fn)(void *), void *arg, unsigned long flags)
{
	struct kernel_clone_args args = {
		.flags = ((lower_32_bits(flags) | CLONE_VM | CLONE_UNTRACED) &
			  ~CSIGNAL),
		.exit_signal = (lower_32_bits(flags) & CSIGNAL),
		.fn = fn,
		.fn_arg = arg,
	};

	return kernel_clone(&args);
}

/* Stub: fork syscalls not needed for Hello World kernel */
#ifdef __ARCH_WANT_SYS_FORK
SYSCALL_DEFINE0(fork)
{
	return -ENOSYS;
}
#endif

#ifdef __ARCH_WANT_SYS_VFORK
SYSCALL_DEFINE0(vfork)
{
	return -ENOSYS;
}
#endif

#ifdef __ARCH_WANT_SYS_CLONE
SYSCALL_DEFINE5(clone, unsigned long, clone_flags, unsigned long, newsp,
		int __user *, parent_tidptr, unsigned long, tls, int __user *,
		child_tidptr)
{
	return -ENOSYS;
}
#endif

#ifdef __ARCH_WANT_SYS_CLONE3

/* Stub: clone3 syscall not needed for minimal kernel - use clone() instead */
SYSCALL_DEFINE2(clone3, struct clone_args __user *, uargs, size_t, size)
{
	return -ENOSYS;
}
#endif

#ifndef ARCH_MIN_MMSTRUCT_ALIGN
#define ARCH_MIN_MMSTRUCT_ALIGN 0
#endif

static void sighand_ctor(void *data)
{
	struct sighand_struct *sighand = data;

	spin_lock_init(&sighand->siglock);
	/* init_waitqueue_head(&sighand->signalfd_wqh) removed - field removed */
}

void __init proc_caches_init(void)
{
	unsigned int mm_size;

	sighand_cachep = kmem_cache_create(
		"sighand_cache", sizeof(struct sighand_struct), 0,
		SLAB_HWCACHE_ALIGN | SLAB_PANIC | SLAB_TYPESAFE_BY_RCU |
			SLAB_ACCOUNT,
		sighand_ctor);
	signal_cachep = kmem_cache_create(
		"signal_cache", sizeof(struct signal_struct), 0,
		SLAB_HWCACHE_ALIGN | SLAB_PANIC | SLAB_ACCOUNT, NULL);
	files_cachep = kmem_cache_create(
		"files_cache", sizeof(struct files_struct), 0,
		SLAB_HWCACHE_ALIGN | SLAB_PANIC | SLAB_ACCOUNT, NULL);
	fs_cachep = kmem_cache_create(
		"fs_cache", sizeof(struct fs_struct), 0,
		SLAB_HWCACHE_ALIGN | SLAB_PANIC | SLAB_ACCOUNT, NULL);

	mm_size = sizeof(struct mm_struct) + cpumask_size();

	mm_cachep = kmem_cache_create_usercopy(
		"mm_struct", mm_size, ARCH_MIN_MMSTRUCT_ALIGN,
		SLAB_HWCACHE_ALIGN | SLAB_PANIC | SLAB_ACCOUNT,
		offsetof(struct mm_struct, saved_auxv),
		sizeof_field(struct mm_struct, saved_auxv), NULL);
	vm_area_cachep = KMEM_CACHE(vm_area_struct, SLAB_PANIC | SLAB_ACCOUNT);
	mmap_init();
	nsproxy_cache_init();
}

SYSCALL_DEFINE1(unshare, unsigned long, unshare_flags)
{
	/* Stubbed: namespace unsharing not needed for minimal boot */
	return -EINVAL;
}

int unshare_files(void)
{
	/* unshare_fd always returns 0 with copy=NULL, so condition true */
	return 0;
}
