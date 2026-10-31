
#include <linux/sched/mm.h>
#include <linux/sched/task_stack.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/swap.h>
#include <linux/ptrace.h>
#include <linux/fs_struct.h>
/* end scs.h */

#include <asm/pgalloc.h>
#include <asm/mmu_context.h>

static struct kmem_cache *task_struct_cachep;

static inline struct task_struct *alloc_task_struct_node(int node)
{
	return kmem_cache_alloc_node(task_struct_cachep, GFP_KERNEL, node);
}

static inline void free_task_struct(struct task_struct *tsk)
{
	kmem_cache_free(task_struct_cachep, tsk);
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

static struct kmem_cache *signal_cachep;

struct kmem_cache *files_cachep;

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

void vm_area_free(struct vm_area_struct *vma)
{
	kmem_cache_free(vm_area_cachep, vma);
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

#define allocate_mm() (kmem_cache_alloc(mm_cachep, GFP_KERNEL))
#define free_mm(mm) (kmem_cache_free(mm_cachep, (mm)))

void __mmdrop(struct mm_struct *mm)
{
	BUG_ON(mm == &init_mm);
	mm_free_pgd(mm);
	free_mm(mm);
}

void __init fork_init(void)
{
	task_struct_cachep = kmem_cache_create("task_struct",
					       sizeof(struct task_struct),
					       L1_CACHE_BYTES,
					       SLAB_PANIC | SLAB_ACCOUNT, NULL);
}

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

static void mm_release(struct task_struct *tsk, struct mm_struct *mm)
{
	deactivate_mm(tsk, mm);
}

void exec_mm_release(struct task_struct *tsk, struct mm_struct *mm)
{
	mm_release(tsk, mm);
}

static int copy_mm(unsigned long clone_flags, struct task_struct *tsk)
{
	struct mm_struct *mm, *oldmm;

	tsk->mm = NULL;
	tsk->active_mm = NULL;

	oldmm = current->mm;
	if (!oldmm)
		return 0;

	/*
	 * The only spawner (user_mode_thread) always sets
	 * CLONE_VM, so the address space is always shared -- the dup_mm()
	 * (else) arm is statically dead.
	 */
	mmget(oldmm);
	mm = oldmm;

	tsk->mm = mm;
	tsk->active_mm = mm;
	return 0;
}

static int copy_files(unsigned long clone_flags, struct task_struct *tsk)
{
	struct files_struct *oldf, *newf;
	int error = 0;

	oldf = current->files;
	if (!oldf)
		goto out;

	if (clone_flags & CLONE_FILES) {
		atomic_inc(&oldf->count);
		goto out;
	}

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

	/* CLONE_THREAD is never set on this build: always a fresh signal. */
	sig = kmem_cache_zalloc(signal_cachep, GFP_KERNEL);
	tsk->signal = sig;
	if (!sig)
		return -ENOMEM;

	task_lock(current->group_leader);
	memcpy(sig->rlim, current->signal->rlim, sizeof sig->rlim);
	task_unlock(current->group_leader);

	/* sched_autogroup_fork - stubbed */

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

	/*
	 * The CLONE_NEWNS/NEWUSER/THREAD/SIGHAND/PARENT/NEWPID validation
	 * checks here are dead on this build: none of those flags is ever
	 * set (the only spawns pass CLONE_FS|CLONE_FILES|CLONE_VM|
	 * CLONE_UNTRACED|SIGCHLD), so all the early -EINVAL returns are
	 * unreachable.
	 */

	retval = -ENOMEM;
	p = dup_task_struct(current, node);
	if (!p)
		goto fork_out;
	p->flags &= ~PF_KTHREAD;

	rt_mutex_init_task(p);

	retval = copy_creds(p, clone_flags);
	if (retval < 0)
		goto bad_fork_free;

	p->flags &= ~PF_IDLE;
	spin_lock_init(&p->alloc_lock);

	p->pagefault_disabled = 0;

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

	if (pid != &init_struct_pid) {
		pid = alloc_pid(p->nsproxy->pid_ns_for_children);
		if (IS_ERR(pid)) {
			retval = PTR_ERR(pid);
			goto bad_fork_cleanup_io;
		}
	}

	p->pid = pid_nr(pid);
	/* CLONE_THREAD never set -> always a new thread-group leader */
	p->group_leader = p;

	if (likely(p->pid))
		init_task_pid(p, PIDTYPE_PID, pid);

	return p;

bad_fork_cleanup_io:
bad_fork_cleanup_mm:
	if (p->mm) {
		mmput(p->mm);
	}
bad_fork_cleanup_signal:
	/* CLONE_THREAD never set -> signal_struct is always private here */
	kmem_cache_free(signal_cachep, p->signal);
bad_fork_cleanup_fs:
bad_fork_cleanup_semundo:
bad_fork_cleanup_policy:
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

	/*
	 * The only spawner (user_mode_thread) always sets
	 * CLONE_UNTRACED and there is no fork/clone/vfork syscall, so the
	 * ptrace-event clone notification (PTRACE_EVENT_CLONE/FORK) is dead.
	 */
	p = copy_process(NULL, NUMA_NO_NODE, args);

	if (IS_ERR(p))
		return PTR_ERR(p);

	pid = get_task_pid(p, PIDTYPE_PID);
	nr = pid_vnr(pid);

	wake_up_new_task(p);

	put_pid(pid);
	return nr;
}

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

void __init proc_caches_init(void)
{
	unsigned int mm_size;

	signal_cachep = kmem_cache_create(
		"signal_cache", sizeof(struct signal_struct), 0,
		SLAB_HWCACHE_ALIGN | SLAB_PANIC | SLAB_ACCOUNT, NULL);
	files_cachep = kmem_cache_create(
		"files_cache", sizeof(struct files_struct), 0,
		SLAB_HWCACHE_ALIGN | SLAB_PANIC | SLAB_ACCOUNT, NULL);

	mm_size = sizeof(struct mm_struct);

	mm_cachep = kmem_cache_create_usercopy(
		"mm_struct", mm_size, ARCH_MIN_MMSTRUCT_ALIGN,
		SLAB_HWCACHE_ALIGN | SLAB_PANIC | SLAB_ACCOUNT,
		offsetof(struct mm_struct, saved_auxv),
		sizeof_field(struct mm_struct, saved_auxv), NULL);
	vm_area_cachep = KMEM_CACHE(vm_area_struct, SLAB_PANIC | SLAB_ACCOUNT);
}

/*
 * Removed: SYSCALL_DEFINE1(unshare) - unreachable. The init ELF issues only
 * write(2)+exit(2) via int 0x80; unshare(2) is never invoked and nothing in the
 * boot/exec path calls it internally. syscall_32.tbl entry 310 routes to sys_ni.
 */
