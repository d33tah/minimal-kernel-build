#include <linux/slab.h>
#include <linux/sched/mm.h>
#include <linux/sched/coredump.h>
#include <linux/sched/user.h>
#include <linux/sched/task.h>
#include <linux/sched/task_stack.h>
#include <linux/sched/signal.h>
#include <linux/init.h>
#include <asm/unistd.h>
#include <linux/module.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/binfmts.h>
#include <linux/mman.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/mm_inline.h>
#include <linux/vmacache.h>
#include <linux/nsproxy.h>
#include <linux/cpu.h>
#include <linux/cgroup.h>
#include <linux/security.h>
#include <linux/syscalls.h>
#define FUTEX_TID_MASK 0x3fffffff
#include <linux/kthread.h>
#include <linux/rcupdate.h>
#include <linux/ptrace.h>
#include <linux/fs_struct.h>
#define STACK_END_MAGIC 0x57AC6E9D
#include <linux/compiler.h>
#include <linux/init_task.h>
#include <linux/thread_info.h>

#include <asm/pgalloc.h>
#include <linux/uaccess.h>
#include <asm/mmu_context.h>
#include <asm/tlbflush.h>

#define MAX_THREADS FUTEX_TID_MASK

int nr_threads;

static int max_threads;

__cacheline_aligned DEFINE_RWLOCK(tasklist_lock);

static struct kmem_cache *task_struct_cachep;

static inline void free_task_struct(struct task_struct *tsk)
{
	kmem_cache_free(task_struct_cachep, tsk);
}

static void thread_stack_free_rcu(struct rcu_head *rh)
{
}

static struct kmem_cache *signal_cachep;

struct kmem_cache *sighand_cachep;

struct kmem_cache *files_cachep;

struct kmem_cache *fs_cachep;

static struct kmem_cache *vm_area_cachep;

static struct kmem_cache *mm_cachep;

struct vm_area_struct *vm_area_alloc(struct mm_struct *mm)
{
	struct vm_area_struct *vma;

	static const struct vm_operations_struct dummy_vm_ops = {};

	vma = kmem_cache_alloc(vm_area_cachep, GFP_KERNEL);
	if (vma) {
		memset(vma, 0, sizeof(*vma));
		vma->vm_mm = mm;
		vma->vm_ops = &dummy_vm_ops;
		INIT_LIST_HEAD(&vma->anon_vma_chain);
	}
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
	}
	return new;
}

void vm_area_free(struct vm_area_struct *vma)
{
	kmem_cache_free(vm_area_cachep, vma);
}

void exit_task_stack_account(struct task_struct *tsk)
{
	void *stack = task_stack_page(tsk);
	mod_lruvec_kmem_state(stack, NR_KERNEL_STACK_KB,
			      -1 * (THREAD_SIZE / 1024));
}

void put_task_stack(struct task_struct *tsk)
{
	if (refcount_dec_and_test(&tsk->stack_refcount)) {
		struct rcu_head *rh;
		if (WARN_ON(READ_ONCE(tsk->__state) != TASK_DEAD))
			return;
		rh = tsk->stack;
		call_rcu(rh, thread_stack_free_rcu);
		tsk->stack = NULL;
	}
}

static void free_task(struct task_struct *tsk)
{
	WARN_ON_ONCE(refcount_read(&tsk->stack_refcount) != 0);
	if (tsk->flags & PF_KTHREAD)
		free_kthread_struct(tsk);
	free_task_struct(tsk);
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
	put_user_ns(mm->user_ns);
	free_mm(mm);
}

static inline void free_signal_struct(struct signal_struct *sig)
{
	kmem_cache_free(signal_cachep, sig);
}

int arch_task_struct_size __read_mostly;

void __init fork_init(void)
{
#ifndef ARCH_MIN_TASKALIGN
#define ARCH_MIN_TASKALIGN 0
#endif
	int align = max_t(int, L1_CACHE_BYTES, ARCH_MIN_TASKALIGN);
	unsigned long useroffset, usersize;

	arch_thread_struct_whitelist(&useroffset, &usersize);
	if (unlikely(usersize == 0))
		useroffset = 0;
	else
		useroffset += offsetof(struct task_struct, thread);
	task_struct_cachep = kmem_cache_create_usercopy(
		"task_struct", arch_task_struct_size, align,
		SLAB_PANIC | SLAB_ACCOUNT, useroffset, usersize, NULL);

	/* Hardcode max_threads - no need to compute from RAM for hello-world */
	max_threads = MAX_THREADS;
}

/* arch_dup_task_struct provided by arch/x86/kernel/process.c */

void set_task_stack_end_magic(struct task_struct *tsk)
{
	unsigned long *stackend;

	stackend = end_of_stack(tsk);
	*stackend = STACK_END_MAGIC;
}

static struct mm_struct *mm_init(struct mm_struct *mm, struct task_struct *p,
				 struct user_namespace *user_ns)
{
	mm->mmap = NULL;
	mm->mm_rb = RB_ROOT;
	mm->vmacache_seqnum = 0;
	atomic_set(&mm->mm_users, 1);
	atomic_set(&mm->mm_count, 1);
	init_rwsem(&mm->mmap_lock); /* mmap_init_lock inlined */
	INIT_LIST_HEAD(&mm->mmlist);
	atomic_long_set(&mm->pgtables_bytes,
			0); /* mm_pgtables_bytes_init inlined */
	mm->map_count = 0;
	spin_lock_init(&mm->page_table_lock);
	cpumask_clear(
		(struct cpumask *)((unsigned long)mm +
				   offsetof(struct mm_struct, cpu_bitmap)));
	RCU_INIT_POINTER(mm->exe_file, NULL);
	atomic_set(&mm->tlb_flush_pending,
		   0); /* init_tlb_flush_pending inlined */

	if (current->mm) {
		mm->flags = current->mm->flags & MMF_INIT_MASK;
		mm->def_flags = current->mm->def_flags & VM_INIT_DEF_MASK;
	} else {
		mm->flags = MMF_DUMP_FILTER_DEFAULT;
		mm->def_flags = 0;
	}

	mm->pgd = pgd_alloc(mm);
	if (unlikely(!mm->pgd)) {
		free_mm(mm);
		return NULL;
	}

	init_new_context(p, mm);

	mm->user_ns = get_user_ns(user_ns);
	return mm;
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
	if (atomic_dec_and_test(&mm->mm_users)) {
		VM_BUG_ON(atomic_read(&mm->mm_users));

		exit_mmap(mm);
		set_mm_exe_file(mm, NULL);
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

void exec_mm_release(struct task_struct *tsk, struct mm_struct *mm)
{
	deactivate_mm(tsk, mm);
}

void __cleanup_sighand(struct sighand_struct *sighand)
{
	if (refcount_dec_and_test(&sighand->count)) {
		kmem_cache_free(sighand_cachep, sighand);
	}
}

static inline void init_task_pid(struct task_struct *task, enum pid_type type,
				 struct pid *pid)
{
	if (type == PIDTYPE_PID)
		task->thread_pid = pid;
	else
		task->signal->pids[type] = pid;
}

static __latent_entropy struct task_struct *
copy_process(int node, struct kernel_clone_args *args)
{
	int retval;
	struct task_struct *p;
	struct pid *pid;
	const u64 clone_flags = args->flags;

	retval = -ENOMEM;
	p = kmem_cache_alloc_node(task_struct_cachep, GFP_KERNEL, node);
	if (!p)
		goto fork_out;
	arch_dup_task_struct(p, current);
	{
		struct page *page = alloc_pages_node(node, THREADINFO_GFP,
						     THREAD_SIZE_ORDER);
		if (!page)
			goto bad_fork_free_tsk;
		p->stack = page_address(page);
	}
	refcount_set(&p->stack_refcount, 1);
	mod_lruvec_kmem_state(p->stack, NR_KERNEL_STACK_KB, THREAD_SIZE / 1024);
	clear_tsk_need_resched(p);
	set_task_stack_end_magic(p);
	if (current->cpus_ptr == &current->cpus_mask)
		p->cpus_ptr = &p->cpus_mask;
	refcount_set(&p->rcu_users, 2);
	refcount_set(&p->usage, 1);
	p->worker_private = NULL;
	kmap_local_fork(p);

	p->flags &= ~PF_KTHREAD;
	if (args->kthread)
		p->flags |= PF_KTHREAD;

	p->set_child_tid = NULL;

	raw_spin_lock_init(&p->pi_lock);

	retval = copy_creds(p);
	if (retval < 0)
		goto bad_fork_free;

	retval = -EAGAIN;
	if (data_race(nr_threads >= max_threads))
		goto bad_fork_cleanup_count;
	p->flags &=
		~(PF_SUPERPRIV | PF_WQ_WORKER | PF_IDLE | PF_NO_SETAFFINITY);
	p->flags |= PF_FORKNOEXEC;
	spin_lock_init(&p->alloc_lock);

	init_sigpending(&p->pending);

	if (args->kthread) {
		if (!set_kthread_struct(p))
			goto bad_fork_cleanup_count;
	}

	p->pagefault_disabled = 0;

	retval = sched_fork(clone_flags, p);
	if (retval)
		goto bad_fork_cleanup_count;
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
	{
		struct fs_struct *fs = current->fs;
		spin_lock(&fs->lock);
		if (fs->in_exec) {
			spin_unlock(&fs->lock);
			retval = -EAGAIN;
			goto bad_fork_cleanup_files;
		}
		fs->users++;
		spin_unlock(&fs->lock);
	}
	{
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
	}
	{
		struct signal_struct *sig;
		sig = kmem_cache_zalloc(signal_cachep, GFP_KERNEL);
		p->signal = sig;
		if (!sig) {
			retval = -ENOMEM;
			goto bad_fork_cleanup_sighand;
		}
		atomic_set(&sig->live, 1);
		refcount_set(&sig->sigcnt, 1);
		sig->thread_head =
			(struct list_head)LIST_HEAD_INIT(p->thread_node);
		p->thread_node =
			(struct list_head)LIST_HEAD_INIT(sig->thread_head);
		init_sigpending(&sig->shared_pending);
		task_lock(current->group_leader);
		memcpy(sig->rlim, current->signal->rlim, sizeof sig->rlim);
		task_unlock(current->group_leader);
		mutex_init(&sig->cred_guard_mutex);
		init_rwsem(&sig->exec_update_lock);
	}
	{
		struct mm_struct *oldmm;
		p->mm = NULL;
		p->active_mm = NULL;
		oldmm = current->mm;
		if (oldmm) {
			vmacache_flush(p);
			mmget(oldmm);
			p->mm = oldmm;
			p->active_mm = oldmm;
		}
	}
	retval = copy_namespaces(p);
	if (retval)
		goto bad_fork_cleanup_mm;
	retval = copy_thread(p, args);
	if (retval)
		goto bad_fork_cleanup_namespaces;

	/* Always allocate new pid (pid parameter was always NULL) */
	pid = alloc_pid(p->nsproxy->pid_ns_for_children, args->set_tid,
			args->set_tid_size);
	if (IS_ERR(pid)) {
		retval = PTR_ERR(pid);
		goto bad_fork_cleanup_thread;
	}

	clear_task_syscall_work(p, SYSCALL_TRACE);
	clear_task_syscall_work(p, SYSCALL_EMU);

	p->pid = pid_nr(pid);
	p->group_leader = p;
	p->tgid = p->pid;

	INIT_LIST_HEAD(&p->thread_group);
	p->task_works = NULL;

	sched_cgroup_fork(p, args);

	write_lock_irq(&tasklist_lock);

	p->real_parent = current;
	p->exit_signal = args->exit_signal;

	spin_lock(&current->sighand->siglock);

	if (unlikely(!(ns_of_pid(pid)->pid_allocated & PIDNS_ADDING))) {
		retval = -ENOMEM;
		goto bad_fork_cancel_cgroup;
	}

	{
		enum pid_type type;
		for (type = PIDTYPE_PID; type < PIDTYPE_MAX; ++type)
			INIT_HLIST_NODE(&p->pid_links[type]);
	}
	if (likely(p->pid)) {
		p->jobctl = 0;

		init_task_pid(p, PIDTYPE_PID, pid);
		init_task_pid(p, PIDTYPE_TGID, pid);
		init_task_pid(p, PIDTYPE_PGID, task_pgrp(current));
		init_task_pid(p, PIDTYPE_SID, task_session(current));

		if (is_child_reaper(pid))
			ns_of_pid(pid)->child_reaper = p;
		sigemptyset(&p->signal->shared_pending.signal);
		list_add_tail_rcu(&p->tasks, &init_task.tasks);
		attach_pid(p, PIDTYPE_TGID);
		attach_pid(p, PIDTYPE_PGID);
		attach_pid(p, PIDTYPE_SID);
		attach_pid(p, PIDTYPE_PID);
		nr_threads++;
	}
	spin_unlock(&current->sighand->siglock);
	write_unlock_irq(&tasklist_lock);

	return p;

bad_fork_cancel_cgroup:
	spin_unlock(&current->sighand->siglock);
	write_unlock_irq(&tasklist_lock);
	/* Always free pid (was always allocated, never init_struct_pid) */
	free_pid(pid);
bad_fork_cleanup_thread:
	exit_thread(p);
bad_fork_cleanup_namespaces:
	exit_task_namespaces(p);
bad_fork_cleanup_mm:
	if (p->mm)
		mmput(p->mm);
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
	goto fork_out;
bad_fork_free_tsk:
	free_task_struct(p);
fork_out:
	return ERR_PTR(retval);
}

struct mm_struct *copy_init_mm(void)
{
	struct mm_struct *mm;

	mm = allocate_mm();
	if (!mm)
		return NULL;
	memcpy(mm, &init_mm, sizeof(*mm));
	if (!mm_init(mm, NULL, mm->user_ns))
		return NULL;
	return mm;
}

static pid_t kernel_clone(struct kernel_clone_args *args)
{
	struct pid *pid;
	struct task_struct *p;
	pid_t nr;

	p = copy_process(NUMA_NO_NODE, args);

	if (IS_ERR(p))
		return PTR_ERR(p);

	pid = get_task_pid(p, PIDTYPE_PID);
	nr = pid_vnr(pid);
	wake_up_new_task(p);

	put_pid(pid);
	return nr;
}

static pid_t __kernel_thread(int (*fn)(void *), void *arg, unsigned long flags,
			     int kthread)
{
	struct kernel_clone_args args = {
		.flags = ((lower_32_bits(flags) | CLONE_VM | CLONE_UNTRACED) &
			  ~CSIGNAL),
		.exit_signal = (lower_32_bits(flags) & CSIGNAL),
		.fn = fn,
		.fn_arg = arg,
		.kthread = kthread,
	};
	return kernel_clone(&args);
}

pid_t kernel_thread(int (*fn)(void *), void *arg, unsigned long flags)
{
	return __kernel_thread(fn, arg, flags, 1);
}

pid_t user_mode_thread(int (*fn)(void *), void *arg, unsigned long flags)
{
	return __kernel_thread(fn, arg, flags, 0);
}

/* fork/vfork/clone/clone3 syscalls replaced with COND_SYSCALL */

#ifndef ARCH_MIN_MMSTRUCT_ALIGN
#define ARCH_MIN_MMSTRUCT_ALIGN 0
#endif

static void sighand_ctor(void *data)
{
	struct sighand_struct *sighand = data;

	spin_lock_init(&sighand->siglock);
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
