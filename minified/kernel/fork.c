#include <linux/sched/mm.h>
/* coredump.h removed: MMF_INIT_MASK=0x7FF, MMF_DUMP_FILTER_DEFAULT=0x8C */
#include <linux/sched/task_stack.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/binfmts.h>
#include <linux/cpu.h>
#include <linux/memcontrol.h>
#define FUTEX_TID_MASK 0x3fffffff
#include <linux/kthread.h>
#include <linux/fs_struct.h>
#define STACK_END_MAGIC 0x57AC6E9D
#include <linux/init_task.h>

#include <asm/pgalloc.h>
#include <asm/mmu_context.h>

#define MAX_THREADS FUTEX_TID_MASK

int nr_threads;

static int max_threads;

__cacheline_aligned DEFINE_RWLOCK(tasklist_lock);

static struct kmem_cache *task_struct_cachep;

static struct kmem_cache *signal_cachep;

struct kmem_cache *sighand_cachep;

struct kmem_cache *files_cachep;

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

void vm_area_free(struct vm_area_struct *vma)
{
	kmem_cache_free(vm_area_cachep, vma);
}

#define allocate_mm() (kmem_cache_alloc(mm_cachep, GFP_KERNEL))
#define free_mm(mm) (kmem_cache_free(mm_cachep, (mm)))

void __mmdrop(struct mm_struct *mm)
{
	pgd_free(mm, mm->pgd);
	destroy_context(mm);
	put_user_ns(mm->user_ns);
	free_mm(mm);
}

int arch_task_struct_size __read_mostly;

void __init fork_init(void)
{
	int align = max_t(int, L1_CACHE_BYTES, ARCH_MIN_TASKALIGN);

	task_struct_cachep = kmem_cache_create("task_struct",
					       arch_task_struct_size, align,
					       SLAB_PANIC | SLAB_ACCOUNT, NULL);

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
		mm->flags = current->mm->flags & 0x7FF;
		mm->def_flags = current->mm->def_flags & VM_INIT_DEF_MASK;
	} else {
		mm->flags = 0x8C;
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
		exit_mmap(mm);
		set_mm_exe_file(mm, NULL);
		if (mm->binfmt)
			module_put(mm->binfmt->module);
		mmdrop(mm);
	}
}

int set_mm_exe_file(struct mm_struct *mm, struct file *new_exe_file)
{
	if (new_exe_file)
		get_file(new_exe_file);
	rcu_assign_pointer(mm->exe_file, new_exe_file);
	return 0;
}

void exec_mm_release(struct task_struct *tsk, struct mm_struct *mm)
{
	deactivate_mm(tsk, mm);
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
	int retval = 0;
	struct task_struct *p;
	struct pid *pid;
	const u64 clone_flags = args->flags;

	p = kmem_cache_alloc_node(task_struct_cachep, GFP_KERNEL, node);
	if (!p)
		return ERR_PTR(-ENOMEM);
	arch_dup_task_struct(p, current);
	{
		struct page *page = alloc_pages_node(node, THREADINFO_GFP,
						     THREAD_SIZE_ORDER);
		if (!page)
			goto bad_fork;
		p->stack = page_address(page);
	}
	mod_lruvec_kmem_state(p->stack, NR_KERNEL_STACK_KB, THREAD_SIZE / 1024);
	clear_tsk_need_resched(p);
	set_task_stack_end_magic(p);
	refcount_set(&p->usage, 1);
	p->worker_private = NULL;
	kmap_local_fork(p);

	p->flags &= ~PF_KTHREAD;
	if (args->kthread)
		p->flags |= PF_KTHREAD;

	raw_spin_lock_init(&p->pi_lock);

	retval = copy_creds(p);
	if (retval < 0)
		goto bad_fork;

	retval = -EAGAIN;
	if (data_race(nr_threads >= max_threads))
		goto bad_fork;
	p->flags &= ~(PF_IDLE | PF_NO_SETAFFINITY);
	p->flags |= PF_FORKNOEXEC;
	spin_lock_init(&p->alloc_lock);

	init_sigpending(&p->pending);

	if (args->kthread) {
		if (!set_kthread_struct(p))
			goto bad_fork;
	}

	p->pagefault_disabled = 0;

	retval = sched_fork(clone_flags, p);
	if (retval)
		goto bad_fork;
	{
		struct files_struct *oldf = current->files;
		if (oldf) {
			if (clone_flags & CLONE_FILES) {
				atomic_inc(&oldf->count);
			} else {
				struct files_struct *newf =
					dup_fd(oldf, NR_OPEN_MAX, &retval);
				if (!newf)
					goto bad_fork;
				p->files = newf;
			}
		}
	}
	{
		struct fs_struct *fs = current->fs;
		spin_lock(&fs->lock);
		fs->users++;
		spin_unlock(&fs->lock);
	}
	{
		struct sighand_struct *sig;
		sig = kmem_cache_zalloc(sighand_cachep, GFP_KERNEL);
		RCU_INIT_POINTER(p->sighand, sig);
		if (!sig) {
			retval = -ENOMEM;
			goto bad_fork;
		}
		refcount_set(&sig->count, 1);
	}
	{
		struct signal_struct *sig;
		sig = kmem_cache_zalloc(signal_cachep, GFP_KERNEL);
		p->signal = sig;
		if (!sig) {
			retval = -ENOMEM;
			goto bad_fork;
		}
		init_sigpending(&sig->shared_pending);
		task_lock(current->group_leader);
		memcpy(sig->rlim, current->signal->rlim, sizeof sig->rlim);
		task_unlock(current->group_leader);
		init_rwsem(&sig->exec_update_lock);
	}
	p->mm = NULL;
	p->active_mm = NULL;
	retval = copy_namespaces(p);
	if (retval)
		goto bad_fork;
	retval = copy_thread(p, args);
	if (retval)
		goto bad_fork;

	/* Always allocate new pid (pid parameter was always NULL) */
	pid = alloc_pid(p->nsproxy->pid_ns_for_children, args->set_tid,
			args->set_tid_size);
	if (IS_ERR(pid)) {
		retval = PTR_ERR(pid);
		goto bad_fork;
	}

	clear_task_syscall_work(p, SYSCALL_TRACE);
	clear_task_syscall_work(p, SYSCALL_EMU);

	p->pid = pid_nr(pid);
	p->group_leader = p;

	p->task_works = NULL;

	sched_cgroup_fork(p, args);

	write_lock_irq(&tasklist_lock);

	spin_lock(&current->sighand->siglock);

	if (unlikely(!(ns_of_pid(pid)->pid_allocated & PIDNS_ADDING))) {
		retval = -ENOMEM;
		goto bad_fork;
	}

	{
		enum pid_type type;
		for (type = PIDTYPE_PID; type < PIDTYPE_MAX; ++type)
			INIT_HLIST_NODE(&p->pid_links[type]);
	}
	if (likely(p->pid)) {
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

bad_fork:
	panic("copy_process failed (retval=%d)\n", retval);
}

static pid_t __kernel_thread(int (*fn)(void *), void *arg, unsigned long flags,
			     int kthread)
{
	struct kernel_clone_args args = {
		.flags = ((lower_32_bits(flags) | CLONE_VM | CLONE_UNTRACED) &
			  ~CSIGNAL),
		.fn = fn,
		.fn_arg = arg,
		.kthread = kthread,
	};
	struct task_struct *p;
	struct pid *pid;
	pid_t nr;

	p = copy_process(NUMA_NO_NODE, &args);
	if (IS_ERR(p))
		return PTR_ERR(p);

	pid = get_task_pid(p, PIDTYPE_PID);
	nr = pid_vnr(pid);
	wake_up_new_task(p);
	put_pid(pid);
	return nr;
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

void __init proc_caches_init(void)
{
	unsigned int mm_size;

	sighand_cachep = kmem_cache_create(
		"sighand_cache", sizeof(struct sighand_struct), 0,
		SLAB_HWCACHE_ALIGN | SLAB_PANIC | SLAB_TYPESAFE_BY_RCU |
			SLAB_ACCOUNT,
		NULL);
	signal_cachep = kmem_cache_create(
		"signal_cache", sizeof(struct signal_struct), 0,
		SLAB_HWCACHE_ALIGN | SLAB_PANIC | SLAB_ACCOUNT, NULL);
	files_cachep = kmem_cache_create(
		"files_cache", sizeof(struct files_struct), 0,
		SLAB_HWCACHE_ALIGN | SLAB_PANIC | SLAB_ACCOUNT, NULL);
	mm_size = sizeof(struct mm_struct) + cpumask_size();

	mm_cachep = kmem_cache_create_usercopy(
		"mm_struct", mm_size, ARCH_MIN_MMSTRUCT_ALIGN,
		SLAB_HWCACHE_ALIGN | SLAB_PANIC | SLAB_ACCOUNT,
		offsetof(struct mm_struct, saved_auxv),
		sizeof_field(struct mm_struct, saved_auxv), NULL);
	vm_area_cachep = KMEM_CACHE(vm_area_struct, SLAB_PANIC | SLAB_ACCOUNT);
	mmap_init();
}
